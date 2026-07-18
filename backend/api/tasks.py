"""Celery tasks for running route optimization jobs."""

from __future__ import annotations

import logging
from collections.abc import Mapping
from typing import Any

from celery import shared_task

from . import task_store

logger = logging.getLogger(__name__)


@shared_task(name="api.optimize_routes")
def optimize_routes_task(task_id: str) -> dict[str, Any]:
    """Run the C++ optimizer and persist the task lifecycle in Redis."""
    try:
        task = task_store.get_task(task_id)
        if task is None:
            raise task_store.TaskNotFoundError(
                f"task {task_id} was not found"
            )

        task_store.update_task(task_id, status="PROCESSING")
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
