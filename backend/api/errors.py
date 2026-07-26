"""Helpers for reducing nested DRF errors to one public message."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any


def first_error(
    value: Any,
    *,
    indexed_paths: bool = False,
    path: str = "",
) -> str:
    """Return the first nested error using the requested path style."""
    if isinstance(value, Mapping):
        if not indexed_paths and "detail" in value:
            return first_error(value["detail"])
        for field, error in value.items():
            separator = "." if indexed_paths and path else ": "
            field_path = f"{path}{separator}{field}" if path else str(field)
            return first_error(
                error,
                indexed_paths=indexed_paths,
                path=field_path,
            )

    if isinstance(value, Sequence) and not isinstance(value, (str, bytes)):
        for index, error in enumerate(value):
            if not error:
                continue
            error_path = path
            if indexed_paths and isinstance(error, (Mapping, list, tuple)):
                error_path = f"{path}[{index}]"
            return first_error(
                error,
                indexed_paths=indexed_paths,
                path=error_path,
            )

    return f"{path}: {value}" if path else str(value)
