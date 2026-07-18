import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "backend"))
os.environ.setdefault(
    "DJANGO_SETTINGS_MODULE",
    "optiroute_config.settings",
)
os.environ.setdefault("ALLOWED_HOSTS", "testserver")

import django

django.setup()

from django.urls import reverse
from rest_framework import status
from rest_framework.test import APIClient

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


class FullStackOptimizationTests(unittest.TestCase):
    def setUp(self):
        self.redis = FakeRedis()
        self.client = APIClient()
        self.redis_patcher = patch(
            "api.task_store.get_redis_client",
            return_value=self.redis,
        )
        self.redis_patcher.start()
        self.addCleanup(self.redis_patcher.stop)

    def test_api_to_cpp_engine_to_polling_response(self):
        payload = {
            "depot": {"lat": 37.77, "lng": -122.42},
            "stops": [
                {"lat": 37.78, "lng": -122.43},
                {"lat": 37.79, "lng": -122.41},
                {"lat": 37.76, "lng": -122.40},
            ],
            "num_vehicles": 2,
        }

        with patch(
            "api.views.optimize_routes_task.delay",
            side_effect=tasks.optimize_routes_task.run,
        ):
            create_response = self.client.post(
                reverse("api:optimization-list"),
                payload,
                format="json",
            )

        self.assertEqual(
            create_response.status_code,
            status.HTTP_202_ACCEPTED,
        )
        task_id = create_response.data["task_id"]
        poll_response = self.client.get(
            reverse(
                "api:optimization-detail",
                kwargs={"task_id": task_id},
            )
        )

        self.assertEqual(poll_response.status_code, status.HTTP_200_OK)
        self.assertEqual(poll_response.data["status"], "SUCCESS")
        result = poll_response.data["result"]
        self.assertEqual(len(result["routes"]), 2)
        visited = [
            stop
            for route in result["routes"]
            for stop in route["stop_order"]
        ]
        self.assertEqual(sorted(visited), [0, 1, 2])
        self.assertGreater(result["total_distance_km"], 0)

    def test_cpp_validation_failure_reaches_failed_polling_response(self):
        task_id = "invalid-engine-input"
        task_store.create_task(
            task_id,
            {
                "depot": {"lat": 0, "lng": 0},
                "stops": [{"lat": 1, "lng": 1}],
                "num_vehicles": 2,
            },
            client=self.redis,
        )

        with self.assertRaisesRegex(ValueError, "cannot exceed"):
            tasks.optimize_routes_task.run(task_id)

        response = self.client.get(
            reverse(
                "api:optimization-detail",
                kwargs={"task_id": task_id},
            )
        )
        self.assertEqual(response.status_code, status.HTTP_200_OK)
        self.assertEqual(response.data["status"], "FAILED")
        self.assertIn("cannot exceed", response.data["error_message"])


if __name__ == "__main__":
    unittest.main()
