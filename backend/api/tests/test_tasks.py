import sys
from unittest.mock import Mock, patch

from django.test import SimpleTestCase

from api import task_store
from api import tasks


class FakeRedis:
    def __init__(self):
        self.data = {}

    def set(self, key, value, nx=False):
        if nx and key in self.data:
            return False
        self.data[key] = value
        return True

    def get(self, key):
        return self.data.get(key)


class OptimizationTaskTests(SimpleTestCase):
    def setUp(self):
        self.redis = FakeRedis()
        self.task_id = "550e8400-e29b-41d4-a716-446655440000"
        self.input_data = {
            "depot": {"lat": 37.77, "lng": -122.42},
            "stops": [{"lat": 37.78, "lng": -122.43}],
            "num_vehicles": 1,
        }
        task_store.create_task(
            self.task_id,
            self.input_data,
            client=self.redis,
        )

    def test_successful_task_persists_result(self):
        expected_result = {
            "routes": [
                {
                    "vehicle_id": 1,
                    "stop_order": [0],
                    "route_coordinates": [
                        self.input_data["depot"],
                        self.input_data["stops"][0],
                        self.input_data["depot"],
                    ],
                    "distance_km": 2.0,
                }
            ],
            "total_distance_km": 2.0,
        }

        with (
            patch(
                "api.task_store.get_redis_client",
                return_value=self.redis,
            ),
            patch(
                "api.tasks._run_engine",
                return_value=expected_result,
            ) as run_engine,
        ):
            returned_result = tasks.optimize_routes_task.run(self.task_id)

        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(returned_result, expected_result)
        self.assertEqual(stored["status"], "SUCCESS")
        self.assertEqual(stored["result_data"], expected_result)
        self.assertIsNone(stored["error_message"])
        run_engine.assert_called_once_with(self.input_data)

    def test_engine_failure_is_persisted_and_reraised(self):
        engine_error = RuntimeError("optimizer crashed")

        with (
            patch(
                "api.task_store.get_redis_client",
                return_value=self.redis,
            ),
            patch("api.tasks._run_engine", side_effect=engine_error),
            self.assertRaisesRegex(RuntimeError, "optimizer crashed"),
        ):
            tasks.optimize_routes_task.run(self.task_id)

        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(stored["status"], "FAILED")
        self.assertIsNone(stored["result_data"])
        self.assertEqual(stored["error_message"], "optimizer crashed")

    def test_empty_exception_message_uses_exception_class_name(self):
        with (
            patch(
                "api.task_store.get_redis_client",
                return_value=self.redis,
            ),
            patch("api.tasks._run_engine", side_effect=RuntimeError()),
            self.assertRaises(RuntimeError),
        ):
            tasks.optimize_routes_task.run(self.task_id)

        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(stored["status"], "FAILED")
        self.assertEqual(stored["error_message"], "RuntimeError")

    def test_existing_processing_task_can_resume_idempotently(self):
        task_store.update_task(
            self.task_id,
            status="PROCESSING",
            client=self.redis,
        )
        result = {"routes": [], "total_distance_km": 0.0}

        with (
            patch(
                "api.task_store.get_redis_client",
                return_value=self.redis,
            ),
            patch("api.tasks._run_engine", return_value=result),
        ):
            returned = tasks.optimize_routes_task.run(self.task_id)

        self.assertEqual(returned, result)
        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(stored["status"], "SUCCESS")

    def test_non_serializable_engine_result_marks_task_failed(self):
        invalid_result = {"total_distance_km": float("nan")}

        with (
            patch(
                "api.task_store.get_redis_client",
                return_value=self.redis,
            ),
            patch("api.tasks._run_engine", return_value=invalid_result),
            self.assertRaisesRegex(ValueError, "JSON serializable"),
        ):
            tasks.optimize_routes_task.run(self.task_id)

        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(stored["status"], "FAILED")
        self.assertIsNone(stored["result_data"])
        self.assertIn("JSON serializable", stored["error_message"])

    def test_missing_task_is_not_sent_to_engine(self):
        missing_id = "missing"

        with (
            patch(
                "api.task_store.get_redis_client",
                return_value=self.redis,
            ),
            patch("api.tasks._run_engine") as run_engine,
            self.assertRaises(task_store.TaskNotFoundError),
        ):
            tasks.optimize_routes_task.run(missing_id)

        run_engine.assert_not_called()
        self.assertIsNone(
            task_store.get_task(missing_id, client=self.redis)
        )

    def test_failure_does_not_regress_terminal_task(self):
        task_store.update_task(
            self.task_id,
            status="PROCESSING",
            client=self.redis,
        )
        task_store.update_task(
            self.task_id,
            status="SUCCESS",
            result_data={"routes": []},
            client=self.redis,
        )

        with patch(
            "api.task_store.get_redis_client",
            return_value=self.redis,
        ):
            tasks._record_failure(self.task_id, RuntimeError("late error"))

        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(stored["status"], "SUCCESS")
        self.assertIsNone(stored["error_message"])

    def test_failure_recording_errors_do_not_escape(self):
        with (
            patch(
                "api.task_store.get_task",
                side_effect=ConnectionError("Redis unavailable"),
            ),
            self.assertLogs("api.tasks", level="ERROR"),
        ):
            tasks._record_failure(self.task_id, RuntimeError("engine error"))

    def test_terminal_task_is_not_executed_again(self):
        task_store.update_task(
            self.task_id,
            status="FAILED",
            error_message="already failed",
            client=self.redis,
        )

        with (
            patch(
                "api.task_store.get_redis_client",
                return_value=self.redis,
            ),
            patch("api.tasks._run_engine") as run_engine,
            self.assertRaises(task_store.InvalidTaskTransitionError),
        ):
            tasks.optimize_routes_task.run(self.task_id)

        run_engine.assert_not_called()
        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(stored["status"], "FAILED")
        self.assertEqual(stored["error_message"], "already failed")

    def test_task_has_stable_celery_name(self):
        self.assertEqual(tasks.optimize_routes_task.name, "api.optimize_routes")


class EngineAdapterTests(SimpleTestCase):
    def test_run_engine_forwards_contract_fields(self):
        engine_module = Mock()
        engine_module.optimize_routes.return_value = {"routes": []}
        input_data = {
            "depot": {"lat": 0, "lng": 0},
            "stops": [{"lat": 1, "lng": 1}],
            "num_vehicles": 1,
        }

        with patch.dict(sys.modules, {"optiroute_cpp": engine_module}):
            result = tasks._run_engine(input_data)

        self.assertEqual(result, {"routes": []})
        engine_module.optimize_routes.assert_called_once_with(
            input_data["depot"],
            input_data["stops"],
            input_data["num_vehicles"],
        )
