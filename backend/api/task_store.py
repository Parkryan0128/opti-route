"""Redis-backed persistence for optimization task state."""

from __future__ import annotations

import json
from collections.abc import Mapping
from datetime import datetime, timezone
from functools import lru_cache
from typing import Any

from django.conf import settings
from redis import Redis

TASK_KEY_PREFIX = "task:"
VALID_STATUSES = frozenset({"PENDING", "PROCESSING", "SUCCESS", "FAILED"})

_ALLOWED_STATUS_TRANSITIONS = {
    "PENDING": frozenset({"PENDING", "PROCESSING", "FAILED"}),
    "PROCESSING": frozenset({"PROCESSING", "SUCCESS", "FAILED"}),
    "SUCCESS": frozenset({"SUCCESS"}),
    "FAILED": frozenset({"FAILED"}),
}
_UNSET = object()


class TaskStoreError(RuntimeError):
    """Base exception for task-store operations."""


class TaskAlreadyExistsError(TaskStoreError):
    """Raised when a task key already exists."""


class TaskNotFoundError(TaskStoreError):
    """Raised when updating a task that does not exist."""


class InvalidTaskStatusError(TaskStoreError):
    """Raised when a task status is unknown."""


class InvalidTaskTransitionError(TaskStoreError):
    """Raised when a task attempts an invalid lifecycle transition."""


class CorruptTaskDataError(TaskStoreError):
    """Raised when stored task data is not a valid task JSON object."""


@lru_cache(maxsize=1)
def get_redis_client() -> Redis:
    """Return the process-wide Redis connection pool client."""
    return Redis.from_url(settings.REDIS_URL, decode_responses=True)


def task_key(task_id: str) -> str:
    """Build the namespaced Redis key for a task identifier."""
    normalized_id = str(task_id).strip()
    if not normalized_id:
        raise ValueError("task_id cannot be empty")
    return f"{TASK_KEY_PREFIX}{normalized_id}"


def create_task(
    task_id: str,
    input_data: Mapping[str, Any],
    *,
    client: Redis | None = None,
) -> dict[str, Any]:
    """Create a PENDING task without overwriting an existing task."""
    task = {
        "status": "PENDING",
        "input_data": dict(input_data),
        "result_data": None,
        "error_message": None,
        "created_at": _utc_now_iso(),
    }
    redis_client = client or get_redis_client()
    created = redis_client.set(
        task_key(task_id),
        _serialize_task(task),
        nx=True,
    )
    if not created:
        raise TaskAlreadyExistsError(f"task {task_id} already exists")
    return _copy_task(task)


def get_task(
    task_id: str,
    *,
    client: Redis | None = None,
) -> dict[str, Any] | None:
    """Return a task record, or None when the key does not exist."""
    redis_client = client or get_redis_client()
    raw_task = redis_client.get(task_key(task_id))
    if raw_task is None:
        return None
    return _deserialize_task(raw_task)


def update_task(
    task_id: str,
    *,
    status: str | None = None,
    result_data: Mapping[str, Any] | None | object = _UNSET,
    error_message: str | None | object = _UNSET,
    client: Redis | None = None,
) -> dict[str, Any]:
    """Update mutable task fields while preserving input and creation time."""
    redis_client = client or get_redis_client()
    existing = get_task(task_id, client=redis_client)
    if existing is None:
        raise TaskNotFoundError(f"task {task_id} was not found")

    if status is not None:
        _validate_status(status)
        current_status = existing["status"]
        if status not in _ALLOWED_STATUS_TRANSITIONS[current_status]:
            raise InvalidTaskTransitionError(
                f"cannot transition task from {current_status} to {status}"
            )
        existing["status"] = status

    if result_data is not _UNSET:
        existing["result_data"] = (
            None if result_data is None else dict(result_data)
        )
    if error_message is not _UNSET:
        if error_message is not None and not isinstance(error_message, str):
            raise TypeError("error_message must be a string or None")
        existing["error_message"] = error_message

    redis_client.set(task_key(task_id), _serialize_task(existing))
    return _copy_task(existing)


def _validate_status(status: Any) -> None:
    if status not in VALID_STATUSES:
        raise InvalidTaskStatusError(f"unknown task status: {status!r}")


def _serialize_task(task: Mapping[str, Any]) -> str:
    try:
        return json.dumps(
            task,
            allow_nan=False,
            separators=(",", ":"),
            sort_keys=True,
        )
    except (TypeError, ValueError) as error:
        raise ValueError("task data must be JSON serializable") from error


def _deserialize_task(raw_task: str | bytes) -> dict[str, Any]:
    try:
        task = json.loads(raw_task)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise CorruptTaskDataError("stored task contains invalid JSON") from error

    required_fields = {
        "status",
        "input_data",
        "result_data",
        "error_message",
        "created_at",
    }
    if not isinstance(task, dict) or set(task) != required_fields:
        raise CorruptTaskDataError("stored task has an invalid schema")
    try:
        _validate_status(task["status"])
    except InvalidTaskStatusError as error:
        raise CorruptTaskDataError("stored task has an invalid status") from error
    if not isinstance(task["input_data"], dict):
        raise CorruptTaskDataError("stored task has invalid input_data")
    if task["result_data"] is not None and not isinstance(
        task["result_data"],
        dict,
    ):
        raise CorruptTaskDataError("stored task has invalid result_data")
    if task["error_message"] is not None and not isinstance(
        task["error_message"],
        str,
    ):
        raise CorruptTaskDataError("stored task has invalid error_message")
    if not isinstance(task["created_at"], str):
        raise CorruptTaskDataError("stored task has invalid created_at")
    try:
        created_at = datetime.fromisoformat(
            task["created_at"].replace("Z", "+00:00")
        )
    except ValueError as error:
        raise CorruptTaskDataError(
            "stored task has invalid created_at"
        ) from error
    if created_at.tzinfo is None:
        raise CorruptTaskDataError("stored task created_at must include timezone")
    return task


def _copy_task(task: Mapping[str, Any]) -> dict[str, Any]:
    return json.loads(_serialize_task(task))


def _utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
