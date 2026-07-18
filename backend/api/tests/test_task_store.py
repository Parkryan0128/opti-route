import json
from datetime import datetime
from unittest.mock import patch

from django.test import SimpleTestCase, override_settings

from api import task_store


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


class TaskStoreTests(SimpleTestCase):
    def setUp(self):
        self.redis = FakeRedis()
        self.task_id = "550e8400-e29b-41d4-a716-446655440000"
        self.input_data = {
            "depot": {"lat": 37.77, "lng": -122.42},
            "stops": [{"lat": 37.78, "lng": -122.43}],
            "num_vehicles": 1,
        }

    def test_create_and_get_task(self):
        created = task_store.create_task(
            self.task_id,
            self.input_data,
            client=self.redis,
        )

        self.assertEqual(created["status"], "PENDING")
        self.assertEqual(created["input_data"], self.input_data)
        self.assertIsNone(created["result_data"])
        self.assertIsNone(created["error_message"])
        timestamp = datetime.fromisoformat(created["created_at"])
        self.assertIsNotNone(timestamp.tzinfo)
        self.assertEqual(
            task_store.get_task(self.task_id, client=self.redis),
            created,
        )

    def test_create_does_not_overwrite_existing_task(self):
        task_store.create_task(self.task_id, self.input_data, client=self.redis)

        with self.assertRaises(task_store.TaskAlreadyExistsError):
            task_store.create_task(
                self.task_id,
                {"different": "input"},
                client=self.redis,
            )

        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(stored["input_data"], self.input_data)

    def test_task_data_is_isolated_from_caller_mutation(self):
        created = task_store.create_task(
            self.task_id,
            self.input_data,
            client=self.redis,
        )
        self.input_data["depot"]["lat"] = 0
        created["input_data"]["depot"]["lng"] = 0

        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(stored["input_data"]["depot"]["lat"], 37.77)
        self.assertEqual(stored["input_data"]["depot"]["lng"], -122.42)

    def test_task_key_is_namespaced_and_normalized(self):
        self.assertEqual(
            task_store.task_key(f"  {self.task_id}  "),
            f"task:{self.task_id}",
        )

    def test_get_accepts_bytes_from_redis_client(self):
        created = task_store.create_task(
            self.task_id,
            self.input_data,
            client=self.redis,
        )
        key = task_store.task_key(self.task_id)
        self.redis.data[key] = self.redis.data[key].encode()

        self.assertEqual(
            task_store.get_task(self.task_id, client=self.redis),
            created,
        )

    def test_get_missing_task_returns_none(self):
        self.assertIsNone(
            task_store.get_task(self.task_id, client=self.redis)
        )

    def test_update_task_through_success_lifecycle(self):
        original = task_store.create_task(
            self.task_id,
            self.input_data,
            client=self.redis,
        )
        processing = task_store.update_task(
            self.task_id,
            status="PROCESSING",
            client=self.redis,
        )
        result_data = {"routes": [], "total_distance_km": 0.0}
        succeeded = task_store.update_task(
            self.task_id,
            status="SUCCESS",
            result_data=result_data,
            client=self.redis,
        )

        self.assertEqual(processing["status"], "PROCESSING")
        self.assertEqual(succeeded["status"], "SUCCESS")
        self.assertEqual(succeeded["result_data"], result_data)
        self.assertIsNone(succeeded["error_message"])
        self.assertEqual(succeeded["input_data"], original["input_data"])
        self.assertEqual(succeeded["created_at"], original["created_at"])

    def test_update_task_through_failure_lifecycle(self):
        task_store.create_task(self.task_id, self.input_data, client=self.redis)

        failed = task_store.update_task(
            self.task_id,
            status="FAILED",
            error_message="engine failed",
            client=self.redis,
        )

        self.assertEqual(failed["status"], "FAILED")
        self.assertEqual(failed["error_message"], "engine failed")

    def test_update_rejects_unknown_status(self):
        task_store.create_task(self.task_id, self.input_data, client=self.redis)

        with self.assertRaises(task_store.InvalidTaskStatusError):
            task_store.update_task(
                self.task_id,
                status="RUNNING",
                client=self.redis,
            )

    def test_update_rejects_invalid_status_transition(self):
        task_store.create_task(self.task_id, self.input_data, client=self.redis)
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

        with self.assertRaises(task_store.InvalidTaskTransitionError):
            task_store.update_task(
                self.task_id,
                status="PROCESSING",
                client=self.redis,
            )

    def test_terminal_status_updates_are_idempotent(self):
        task_store.create_task(self.task_id, self.input_data, client=self.redis)
        task_store.update_task(
            self.task_id,
            status="FAILED",
            error_message="failed",
            client=self.redis,
        )

        repeated = task_store.update_task(
            self.task_id,
            status="FAILED",
            error_message="failed",
            client=self.redis,
        )

        self.assertEqual(repeated["status"], "FAILED")

    def test_pending_cannot_skip_directly_to_success(self):
        task_store.create_task(self.task_id, self.input_data, client=self.redis)

        with self.assertRaises(task_store.InvalidTaskTransitionError):
            task_store.update_task(
                self.task_id,
                status="SUCCESS",
                result_data={"routes": []},
                client=self.redis,
            )

    def test_result_data_is_isolated_from_mutation(self):
        task_store.create_task(self.task_id, self.input_data, client=self.redis)
        task_store.update_task(
            self.task_id,
            status="PROCESSING",
            client=self.redis,
        )
        result = {"routes": [{"stop_order": [0]}]}
        updated = task_store.update_task(
            self.task_id,
            status="SUCCESS",
            result_data=result,
            client=self.redis,
        )
        result["routes"][0]["stop_order"].append(1)
        updated["result_data"]["routes"].clear()

        stored = task_store.get_task(self.task_id, client=self.redis)
        self.assertEqual(
            stored["result_data"],
            {"routes": [{"stop_order": [0]}]},
        )

    def test_update_missing_task_raises(self):
        with self.assertRaises(task_store.TaskNotFoundError):
            task_store.update_task(
                self.task_id,
                status="PROCESSING",
                client=self.redis,
            )

    def test_rejects_non_json_task_data(self):
        invalid_input = {"distance": float("nan")}

        with self.assertRaisesRegex(ValueError, "JSON serializable"):
            task_store.create_task(
                self.task_id,
                invalid_input,
                client=self.redis,
            )

    def test_rejects_invalid_error_message_type(self):
        task_store.create_task(self.task_id, self.input_data, client=self.redis)

        with self.assertRaisesRegex(TypeError, "error_message"):
            task_store.update_task(
                self.task_id,
                error_message=123,
                client=self.redis,
            )

    def test_detects_corrupt_stored_data(self):
        key = task_store.task_key(self.task_id)
        self.redis.data[key] = "not JSON"
        with self.assertRaises(task_store.CorruptTaskDataError):
            task_store.get_task(self.task_id, client=self.redis)

        self.redis.data[key] = json.dumps({"status": "PENDING"})
        with self.assertRaises(task_store.CorruptTaskDataError):
            task_store.get_task(self.task_id, client=self.redis)

    def test_detects_invalid_stored_field_types(self):
        valid_task = task_store.create_task(
            self.task_id,
            self.input_data,
            client=self.redis,
        )
        key = task_store.task_key(self.task_id)
        invalid_fields = {
            "status": "UNKNOWN",
            "input_data": [],
            "result_data": [],
            "error_message": 123,
            "created_at": "not-a-timestamp",
        }

        for field, value in invalid_fields.items():
            with self.subTest(field=field):
                corrupt = {**valid_task, field: value}
                self.redis.data[key] = json.dumps(corrupt)
                with self.assertRaises(task_store.CorruptTaskDataError):
                    task_store.get_task(self.task_id, client=self.redis)

    def test_detects_timestamp_without_timezone(self):
        stored = task_store.create_task(
            self.task_id,
            self.input_data,
            client=self.redis,
        )
        stored["created_at"] = "2026-07-17T12:00:00"
        self.redis.data[task_store.task_key(self.task_id)] = json.dumps(stored)

        with self.assertRaises(task_store.CorruptTaskDataError):
            task_store.get_task(self.task_id, client=self.redis)

    def test_rejects_empty_task_id(self):
        with self.assertRaisesRegex(ValueError, "cannot be empty"):
            task_store.create_task("  ", self.input_data, client=self.redis)


class RedisClientTests(SimpleTestCase):
    def tearDown(self):
        task_store.get_redis_client.cache_clear()

    @override_settings(REDIS_URL="redis://example:6379/5")
    @patch("api.task_store.Redis.from_url")
    def test_client_uses_settings_and_is_cached(self, from_url):
        expected_client = object()
        from_url.return_value = expected_client
        task_store.get_redis_client.cache_clear()

        first = task_store.get_redis_client()
        second = task_store.get_redis_client()

        self.assertIs(first, expected_client)
        self.assertIs(second, expected_client)
        from_url.assert_called_once_with(
            "redis://example:6379/5",
            decode_responses=True,
        )
