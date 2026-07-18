"""DRF exception normalization for the public JSON API."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any

from rest_framework.response import Response
from rest_framework.views import exception_handler


def api_exception_handler(
    exception: Exception,
    context: dict[str, Any],
) -> Response | None:
    """Ensure framework-generated API errors use ``error_message``."""
    response = exception_handler(exception, context)
    if response is None or response.status_code < 400:
        return response
    if isinstance(response.data, Mapping) and "error_message" in response.data:
        return response

    response.data = {"error_message": _first_error(response.data)}
    return response


def _first_error(value: Any) -> str:
    if isinstance(value, Mapping):
        if "detail" in value:
            return _first_error(value["detail"])
        for field, error in value.items():
            return f"{field}: {_first_error(error)}"

    if isinstance(value, Sequence) and not isinstance(value, (str, bytes)):
        for error in value:
            if error:
                return _first_error(error)

    return str(value)
