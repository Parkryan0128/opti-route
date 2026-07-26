"""Request validation for the optimization API."""

from __future__ import annotations

import math
from collections.abc import Mapping
from typing import Any

from rest_framework import serializers


class StrictSerializer(serializers.Serializer):
    """Serializer that rejects fields not declared by the API contract."""

    def to_internal_value(self, data: Any) -> dict[str, Any]:
        if isinstance(data, Mapping):
            unknown_fields = set(data) - set(self.fields)
            if unknown_fields:
                errors = {
                    field: ["Unknown field."]
                    for field in sorted(unknown_fields)
                }
                raise serializers.ValidationError(errors)
        return super().to_internal_value(data)


class FiniteFloatField(serializers.FloatField):
    """Float field that does not coerce strings, booleans, or infinities."""

    def to_internal_value(self, data: Any) -> float:
        if isinstance(data, bool) or not isinstance(data, (int, float)):
            raise serializers.ValidationError("Must be a number.")
        value = super().to_internal_value(data)
        if not math.isfinite(value):
            raise serializers.ValidationError("Must be a finite number.")
        return value


class StrictIntegerField(serializers.IntegerField):
    """Integer field that does not coerce floats, strings, or booleans."""

    def to_internal_value(self, data: Any) -> int:
        if isinstance(data, bool) or not isinstance(data, int):
            raise serializers.ValidationError("Must be an integer.")
        return super().to_internal_value(data)


class CoordinateSerializer(StrictSerializer):
    lat = FiniteFloatField(min_value=-90.0, max_value=90.0)
    lng = FiniteFloatField(min_value=-180.0, max_value=180.0)


class OptimizationRequestSerializer(StrictSerializer):
    depot = CoordinateSerializer()
    stops = CoordinateSerializer(
        many=True,
        allow_empty=False,
        max_length=100,
    )
    num_vehicles = StrictIntegerField(min_value=1, max_value=100)

    def validate(self, attrs: dict[str, Any]) -> dict[str, Any]:
        if attrs["num_vehicles"] > len(attrs["stops"]):
            raise serializers.ValidationError(
                {
                    "num_vehicles": [
                        "Cannot exceed the number of stops."
                    ]
                }
            )
        return attrs
