import json
from datetime import datetime, timedelta, timezone
from unittest.mock import Mock, patch

from django.test import SimpleTestCase, override_settings
from rest_framework.test import APIClient

from api import task_store, tasks
from api.admission import QueueBusyError, check_capacity
from api.tests.helpers import patch_task_store_redis, sample_input_data


class RetentionTests(SimpleTestCase):
    def setUp(self):
        patch_task_store_redis(self)
        self.task = task_store.create_task("demo", sample_input_data())

    @override_settings(TASK_RETENTION_SECONDS=86400)
    def test_updates_keep_a_finite_retention_period(self):
        self.assertEqual(self.redis.expirations["task:demo"], 86400)
        task_store.update_task("demo", status="PROCESSING")
        task_store.update_task("demo", status="SUCCESS", result_data={"routes": []})
        self.assertEqual(self.redis.expirations["task:demo"], 86400)

    def test_abandoned_task_fails_without_running_engine(self):
        self.task["created_at"] = (datetime.now(timezone.utc) - timedelta(hours=1)).isoformat()
        self.redis.data["task:demo"] = json.dumps(self.task)
        with patch("api.tasks._run_engine") as engine:
            tasks.optimize_routes_task.run("demo")
        engine.assert_not_called()
        self.assertEqual(task_store.get_task("demo")["status"], "FAILED")

    def test_successful_redelivery_keeps_original_result(self):
        task_store.update_task("demo", status="PROCESSING")
        task_store.update_task("demo", status="SUCCESS", result_data={"routes": [1]})
        with patch("api.tasks._run_engine") as engine:
            result = tasks.optimize_routes_task.run("demo")
        engine.assert_not_called()
        self.assertEqual(result, {"routes": [1]})

    def test_hard_timeout_records_failure_from_parent(self):
        task_store.update_task("demo", status="PROCESSING")
        request = object.__new__(tasks.OptimizationRequest)
        request._args = ("demo",)
        with patch("celery.worker.request.Request.on_timeout"):
            request.on_timeout(False, 45)
        stored = task_store.get_task("demo")
        self.assertEqual(stored["status"], "FAILED")
        self.assertIn("time limit", stored["error_message"])


@override_settings(OPTIMIZATION_QUEUE_LIMIT=20, OPTIMIZATION_MIN_INTERVAL_SECONDS=1)
class AdmissionTests(SimpleTestCase):
    def test_full_queue_does_not_create_or_publish_a_task(self):
        redis = Mock()
        redis.llen.return_value = 20
        with (
            patch("api.admission.get_redis_client", return_value=redis),
            patch("api.views.task_store.create_task") as create,
            patch("api.views.optimize_routes_task.delay") as publish,
        ):
            response = APIClient().post("/api/v1/optimize/", sample_input_data(), format="json")
        self.assertEqual(response.status_code, 429)
        self.assertEqual(response["Retry-After"], "1")
        create.assert_not_called()
        publish.assert_not_called()

    def test_global_cooldown_rejects_repeated_submission(self):
        redis = Mock()
        redis.llen.return_value = 0
        redis.set.side_effect = [True, False]
        with patch("api.admission.get_redis_client", return_value=redis):
            check_capacity()
            with self.assertRaises(QueueBusyError):
                check_capacity()
