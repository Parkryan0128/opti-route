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


class CoordinateSerializer(StrictSerializer):
    lat = serializers.FloatField(min_value=-90.0, max_value=90.0)
    lng = serializers.FloatField(min_value=-180.0, max_value=180.0)

    def to_internal_value(self, data: Any) -> dict[str, float]:
        if isinstance(data, Mapping):
            errors = {}
            for field in ("lat", "lng"):
                value = data.get(field)
                if field in data and (
                    isinstance(value, bool)
                    or not isinstance(value, (int, float))
                ):
                    errors[field] = ["Must be a number."]
            if errors:
                raise serializers.ValidationError(errors)
        return super().to_internal_value(data)

    def validate_lat(self, value: float) -> float:
        if not math.isfinite(value):
            raise serializers.ValidationError("Must be a finite number.")
        return value

    def validate_lng(self, value: float) -> float:
        if not math.isfinite(value):
            raise serializers.ValidationError("Must be a finite number.")
        return value


class OptimizationRequestSerializer(StrictSerializer):
    depot = CoordinateSerializer()
    stops = CoordinateSerializer(
        many=True,
        allow_empty=False,
        max_length=100,
    )
    num_vehicles = serializers.IntegerField(min_value=1, max_value=100)

    def to_internal_value(self, data: Any) -> dict[str, Any]:
        if isinstance(data, Mapping):
            num_vehicles = data.get("num_vehicles")
            if "num_vehicles" in data and (
                isinstance(num_vehicles, bool)
                or not isinstance(num_vehicles, int)
            ):
                raise serializers.ValidationError(
                    {"num_vehicles": ["Must be an integer."]}
                )
        return super().to_internal_value(data)

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
