import json
import uuid
from unittest.mock import patch

from django.urls import reverse
from rest_framework import status
from rest_framework.test import APISimpleTestCase

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


class OptimizationApiTests(APISimpleTestCase):
    def setUp(self):
        self.redis = FakeRedis()
        self.redis_patcher = patch(
            "api.task_store.get_redis_client",
            return_value=self.redis,
        )
        self.redis_patcher.start()
        self.addCleanup(self.redis_patcher.stop)
        self.list_url = reverse("api:optimization-list")
        self.valid_payload = {
            "depot": {"lat": 37.77, "lng": -122.42},
            "stops": [
                {"lat": 37.78, "lng": -122.43},
                {"lat": 37.79, "lng": -122.41},
            ],
            "num_vehicles": 2,
        }

    @patch("api.views.optimize_routes_task.delay")
    def test_post_creates_and_enqueues_task(self, delay):
        response = self.client.post(
            self.list_url,
            self.valid_payload,
            format="json",
        )

        self.assertEqual(response.status_code, status.HTTP_202_ACCEPTED)
        task_id = response.data["task_id"]
        uuid.UUID(task_id)
        stored = task_store.get_task(task_id, client=self.redis)
        self.assertEqual(stored["status"], "PENDING")
        self.assertEqual(stored["input_data"], self.valid_payload)
        delay.assert_called_once_with(task_id)

    @patch("api.views.optimize_routes_task.delay")
    def test_post_allows_duplicate_coordinates(self, delay):
        payload = {
            "depot": {"lat": 0, "lng": 0},
            "stops": [
                {"lat": 1, "lng": 1},
                {"lat": 1, "lng": 1},
            ],
            "num_vehicles": 1,
        }

        response = self.client.post(self.list_url, payload, format="json")

        self.assertEqual(response.status_code, status.HTTP_202_ACCEPTED)
        delay.assert_called_once()

    @patch("api.views.optimize_routes_task.delay")
    def test_post_rejects_invalid_payloads(self, delay):
        cases = [
            ({}, "depot"),
            (
                {
                    **self.valid_payload,
                    "depot": {"lat": 91, "lng": 0},
                },
                "depot.lat",
            ),
            (
                {
                    **self.valid_payload,
                    "depot": {"lat": 0, "lng": -181},
                },
                "depot.lng",
            ),
            (
                {
                    **self.valid_payload,
                    "depot": {"lat": 0},
                },
                "depot.lng",
            ),
            (
                {
                    **self.valid_payload,
                    "depot": [37.77, -122.42],
                },
                "depot",
            ),
            (
                {
                    **self.valid_payload,
                    "stops": [],
                },
                "stops",
            ),
            (
                {
                    **self.valid_payload,
                    "stops": {"lat": 1, "lng": 1},
                },
                "stops",
            ),
            (
                {
                    **self.valid_payload,
                    "stops": [None],
                },
                "stops[0]",
            ),
            (
                {
                    **self.valid_payload,
                    "num_vehicles": 0,
                },
                "num_vehicles",
            ),
            (
                {
                    **self.valid_payload,
                    "num_vehicles": 3,
                },
                "num_vehicles",
            ),
            (
                {
                    **self.valid_payload,
                    "num_vehicles": True,
                },
                "num_vehicles",
            ),
            (
                {
                    **self.valid_payload,
                    "num_vehicles": 1.0,
                },
                "num_vehicles",
            ),
            (
                {
                    **self.valid_payload,
                    "num_vehicles": "1",
                },
                "num_vehicles",
            ),
            (
                {
                    **self.valid_payload,
                    "depot": {"lat": "37.77", "lng": -122.42},
                },
                "depot.lat",
            ),
            (
                {
                    **self.valid_payload,
                    "unexpected": "field",
                },
                "unexpected",
            ),
            (
                {
                    **self.valid_payload,
                    "stops": [{"lat": 1, "lng": 2, "name": "extra"}],
                    "num_vehicles": 1,
                },
                "stops[0].name",
            ),
            (
                {
                    **self.valid_payload,
                    "stops": [
                        {"lat": float(index % 90), "lng": 0}
                        for index in range(101)
                    ],
                },
                "stops",
            ),
        ]

        for payload, expected_error_path in cases:
            with self.subTest(expected_error_path=expected_error_path):
                response = self.client.post(
                    self.list_url,
                    payload,
                    format="json",
                )
                self.assertEqual(
                    response.status_code,
                    status.HTTP_400_BAD_REQUEST,
                )
                self.assertIn(
                    expected_error_path,
                    response.data["error_message"],
                )

        delay.assert_not_called()
        self.assertEqual(self.redis.data, {})

    @patch("api.views.optimize_routes_task.delay")
    def test_post_accepts_exact_limits(self, delay):
        payload = {
            "depot": {"lat": 90, "lng": 180},
            "stops": [
                {
                    "lat": -90 + (index % 181),
                    "lng": -180 + (index % 361),
                }
                for index in range(100)
            ],
            "num_vehicles": 100,
        }

        response = self.client.post(self.list_url, payload, format="json")

        self.assertEqual(response.status_code, status.HTTP_202_ACCEPTED)
        delay.assert_called_once()

    def test_post_rejects_non_object_body(self):
        response = self.client.post(
            self.list_url,
            [self.valid_payload],
            format="json",
        )

        self.assertEqual(response.status_code, status.HTTP_400_BAD_REQUEST)
        self.assertIn("error_message", response.data)
        self.assertEqual(self.redis.data, {})

    def test_malformed_json_uses_error_contract(self):
        response = self.client.generic(
            "POST",
            self.list_url,
            data='{"depot":',
            content_type="application/json",
        )

        self.assertEqual(response.status_code, status.HTTP_400_BAD_REQUEST)
        self.assertEqual(set(response.data), {"error_message"})
        self.assertIn("JSON", response.data["error_message"])

    def test_unsupported_media_type_uses_error_contract(self):
        response = self.client.post(
            self.list_url,
            data="plain text",
            content_type="text/plain",
        )

        self.assertEqual(
            response.status_code,
            status.HTTP_415_UNSUPPORTED_MEDIA_TYPE,
        )
        self.assertEqual(set(response.data), {"error_message"})

    def test_unsupported_method_uses_error_contract(self):
        response = self.client.put(
            self.list_url,
            self.valid_payload,
            format="json",
        )

        self.assertEqual(
            response.status_code,
            status.HTTP_405_METHOD_NOT_ALLOWED,
        )
        self.assertEqual(set(response.data), {"error_message"})

    @patch("api.views.optimize_routes_task.delay")
    def test_post_returns_503_and_marks_failed_when_enqueue_fails(self, delay):
        delay.side_effect = ConnectionError("broker unavailable")

        with self.assertLogs("api.views", level="ERROR"):
            response = self.client.post(
                self.list_url,
                self.valid_payload,
                format="json",
            )

        self.assertEqual(
            response.status_code,
            status.HTTP_503_SERVICE_UNAVAILABLE,
        )
        self.assertEqual(
            response.data,
            {"error_message": "Optimization service unavailable"},
        )
        self.assertEqual(len(self.redis.data), 1)
        stored = json.loads(next(iter(self.redis.data.values())))
        self.assertEqual(stored["status"], "FAILED")
        self.assertEqual(
            stored["error_message"],
            "Could not enqueue optimization task",
        )

    @patch("api.views.task_store.create_task")
    def test_post_returns_503_when_redis_is_unavailable(self, create_task):
        create_task.side_effect = ConnectionError("Redis unavailable")

        with self.assertLogs("api.views", level="ERROR"):
            response = self.client.post(
                self.list_url,
                self.valid_payload,
                format="json",
            )

        self.assertEqual(
            response.status_code,
            status.HTTP_503_SERVICE_UNAVAILABLE,
        )

    def test_get_returns_each_task_status_shape(self):
        task_id = "550e8400-e29b-41d4-a716-446655440000"
        detail_url = reverse(
            "api:optimization-detail",
            kwargs={"task_id": task_id},
        )
        task_store.create_task(
            task_id,
            self.valid_payload,
            client=self.redis,
        )

        pending = self.client.get(detail_url)
        self.assertEqual(pending.data, {"status": "PENDING"})

        task_store.update_task(
            task_id,
            status="PROCESSING",
            client=self.redis,
        )
        processing = self.client.get(detail_url)
        self.assertEqual(processing.data, {"status": "PROCESSING"})

        result = {"routes": [], "total_distance_km": 0.0}
        task_store.update_task(
            task_id,
            status="SUCCESS",
            result_data=result,
            client=self.redis,
        )
        succeeded = self.client.get(detail_url)
        self.assertEqual(
            succeeded.data,
            {"status": "SUCCESS", "result": result},
        )

    def test_get_returns_failed_shape(self):
        task_id = "failed-task"
        detail_url = reverse(
            "api:optimization-detail",
            kwargs={"task_id": task_id},
        )
        task_store.create_task(
            task_id,
            self.valid_payload,
            client=self.redis,
        )
        task_store.update_task(
            task_id,
            status="FAILED",
            error_message="engine failed",
            client=self.redis,
        )

        response = self.client.get(detail_url)

        self.assertEqual(response.status_code, status.HTTP_200_OK)
        self.assertEqual(
            response.data,
            {"status": "FAILED", "error_message": "engine failed"},
        )

    def test_get_unknown_task_returns_404(self):
        response = self.client.get(
            reverse(
                "api:optimization-detail",
                kwargs={"task_id": "unknown"},
            )
        )

        self.assertEqual(response.status_code, status.HTTP_404_NOT_FOUND)
        self.assertEqual(
            response.data,
            {"error_message": "Task not found"},
        )

    @patch("api.views.task_store.get_task")
    def test_get_returns_503_when_redis_is_unavailable(self, get_task):
        get_task.side_effect = ConnectionError("Redis unavailable")

        with self.assertLogs("api.views", level="ERROR"):
            response = self.client.get(
                reverse(
                    "api:optimization-detail",
                    kwargs={"task_id": "any"},
                )
            )

        self.assertEqual(
            response.status_code,
            status.HTTP_503_SERVICE_UNAVAILABLE,
        )
        self.assertEqual(
            response.data,
            {"error_message": "Optimization service unavailable"},
        )

    def test_get_returns_503_for_corrupt_task_data(self):
        task_id = "corrupt"
        self.redis.data[task_store.task_key(task_id)] = "not-json"

        with self.assertLogs("api.views", level="ERROR"):
            response = self.client.get(
                reverse(
                    "api:optimization-detail",
                    kwargs={"task_id": task_id},
                )
            )

        self.assertEqual(
            response.status_code,
            status.HTTP_503_SERVICE_UNAVAILABLE,
        )
        self.assertEqual(
            response.data,
            {"error_message": "Optimization service unavailable"},
        )
