"""HTTP endpoints for creating and polling optimization tasks."""

from __future__ import annotations

import logging
import uuid
from collections.abc import Mapping, Sequence
from typing import Any

from rest_framework import status
from rest_framework.permissions import AllowAny
from rest_framework.request import Request
from rest_framework.response import Response
from rest_framework.views import APIView

from . import task_store
from .serializers import OptimizationRequestSerializer
from .tasks import optimize_routes_task

logger = logging.getLogger(__name__)


class OptimizationListView(APIView):
    permission_classes = [AllowAny]

    def post(self, request: Request) -> Response:
        serializer = OptimizationRequestSerializer(data=request.data)
        if not serializer.is_valid():
            return Response(
                {"error_message": _first_validation_error(serializer.errors)},
                status=status.HTTP_400_BAD_REQUEST,
            )

        task_id = str(uuid.uuid4())
        try:
            task_store.create_task(task_id, serializer.validated_data)
        except Exception:
            logger.exception("Could not create optimization task")
            return _service_unavailable_response()

        try:
            optimize_routes_task.delay(task_id)
        except Exception:
            logger.exception("Could not enqueue optimization task %s", task_id)
            try:
                task_store.update_task(
                    task_id,
                    status="FAILED",
                    error_message="Could not enqueue optimization task",
                )
            except Exception:
                logger.exception(
                    "Could not mark unqueued task %s as failed",
                    task_id,
                )
            return _service_unavailable_response()

        return Response(
            {"task_id": task_id},
            status=status.HTTP_202_ACCEPTED,
        )


class OptimizationDetailView(APIView):
    permission_classes = [AllowAny]

    def get(self, request: Request, task_id: str) -> Response:
        del request
        try:
            task = task_store.get_task(task_id)
        except Exception:
            logger.exception("Could not read optimization task %s", task_id)
            return _service_unavailable_response()

        if task is None:
            return Response(
                {"error_message": "Task not found"},
                status=status.HTTP_404_NOT_FOUND,
            )

        task_status = task["status"]
        if task_status in {"PENDING", "PROCESSING"}:
            body = {"status": task_status}
        elif task_status == "SUCCESS":
            body = {
                "status": task_status,
                "result": task["result_data"],
            }
        else:
            body = {
                "status": task_status,
                "error_message": task["error_message"],
            }
        return Response(body, status=status.HTTP_200_OK)


def _service_unavailable_response() -> Response:
    return Response(
        {"error_message": "Optimization service unavailable"},
        status=status.HTTP_503_SERVICE_UNAVAILABLE,
    )


def _first_validation_error(errors: Any, path: str = "") -> str:
    if isinstance(errors, Mapping):
        for field, value in errors.items():
            field_path = f"{path}.{field}" if path else str(field)
            return _first_validation_error(value, field_path)

    if (
        isinstance(errors, Sequence)
        and not isinstance(errors, (str, bytes))
    ):
        for index, value in enumerate(errors):
            if not value:
                continue
            if isinstance(value, (Mapping, list, tuple)):
                indexed_path = f"{path}[{index}]"
                return _first_validation_error(value, indexed_path)
            return f"{path}: {value}" if path else str(value)

    return f"{path}: {errors}" if path else str(errors)
