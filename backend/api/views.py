"""HTTP endpoints for creating and polling optimization tasks."""

from __future__ import annotations

import logging
import uuid

from rest_framework import status
from rest_framework.request import Request
from rest_framework.response import Response
from rest_framework.views import APIView

from . import task_store
from .errors import first_error
from .serializers import OptimizationRequestSerializer
from .tasks import optimize_routes_task

logger = logging.getLogger(__name__)


class OptimizationListView(APIView):
    def post(self, request: Request) -> Response:
        serializer = OptimizationRequestSerializer(data=request.data)
        if not serializer.is_valid():
            return Response(
                {
                    "error_message": first_error(
                        serializer.errors,
                        indexed_paths=True,
                    )
                },
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
