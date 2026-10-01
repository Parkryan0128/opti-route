"""Celery tasks for running route optimization jobs."""

from __future__ import annotations

import logging
from collections.abc import Mapping
from typing import Any

from celery import shared_task
from celery import Task
from celery.worker.request import Request as WorkerRequest

from . import task_store

logger = logging.getLogger(__name__)

class OptimizationRequest(WorkerRequest):
    """Record hard limits and lost child processes from the surviving worker parent."""
    def on_failure(self, exc_info, send_failed_event=True, return_ok=False):
        super().on_failure(exc_info, send_failed_event, return_ok)
        if self.args:
            _record_failure(self.args[0], RuntimeError("Optimization worker failed. Submit the task again."))

    def on_timeout(self, soft, timeout):
        super().on_timeout(soft, timeout)
        if not soft and self.args:
            _record_failure(self.args[0], TimeoutError("Optimization exceeded its execution time limit."))


class OptimizationTask(Task):
    Request = OptimizationRequest


@shared_task(name="api.optimize_routes", ignore_result=True, base=OptimizationTask)
def optimize_routes_task(task_id: str) -> dict[str, Any]:
    """Run the C++ optimizer and persist the task lifecycle in Redis."""
    try:
        existing = task_store.get_task(task_id)
        if existing is None:
            return {}
        existing = task_store.fail_stale_task(task_id, existing)
        if existing["status"] in {"SUCCESS", "FAILED"}:
            return existing.get("result_data") or {}
        task = task_store.update_task(task_id, status="PROCESSING")
        result = _run_engine(task["input_data"])
        task_store.update_task(
            task_id,
            status="SUCCESS",
            result_data=result,
            error_message=None,
        )
        return result
    except Exception as error:
        _record_failure(task_id, error)
        raise


def _run_engine(input_data: Mapping[str, Any]) -> dict[str, Any]:
    """Load the compiled module lazily and invoke its public API."""
    import optiroute_cpp

    return optiroute_cpp.optimize_routes(
        input_data["depot"],
        input_data["stops"],
        input_data["num_vehicles"],
    )


def _record_failure(task_id: str, error: Exception) -> None:
    """Best-effort failure persistence without masking the original error."""
    try:
        task = task_store.get_task(task_id)
        if task is None or task["status"] not in {"PENDING", "PROCESSING"}:
            return

        message = str(error).strip() or error.__class__.__name__
        task_store.update_task(
            task_id,
            status="FAILED",
            result_data=None,
            error_message=message,
        )
    except Exception:
        logger.exception("Could not record failure for task %s", task_id)
