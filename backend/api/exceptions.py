"""DRF exception normalization for the public JSON API."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from rest_framework.response import Response
from rest_framework.views import exception_handler

from .errors import first_error


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

    response.data = {"error_message": first_error(response.data)}
    return response
