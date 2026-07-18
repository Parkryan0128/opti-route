import math
import unittest

import optiroute_cpp


class OptimizerBindingsTests(unittest.TestCase):
    def test_returns_api_contract_shape(self):
        depot = {"lat": 37.77, "lng": -122.42}
        stops = [
            {"lat": 37.78, "lng": -122.43},
            {"lat": 37.79, "lng": -122.41},
        ]

        result = optiroute_cpp.optimize_routes(depot, stops, 2)

        self.assertEqual(set(result), {"routes", "total_distance_km"})
        self.assertEqual(len(result["routes"]), 2)
        self.assertGreater(result["total_distance_km"], 0)

        visited_stops = []
        for vehicle_id, route in enumerate(result["routes"], start=1):
            self.assertEqual(route["vehicle_id"], vehicle_id)
            self.assertEqual(route["route_coordinates"][0], depot)
            self.assertEqual(route["route_coordinates"][-1], depot)
            self.assertEqual(
                len(route["route_coordinates"]),
                len(route["stop_order"]) + 2,
            )
            self.assertTrue(math.isfinite(route["distance_km"]))
            visited_stops.extend(route["stop_order"])

        self.assertEqual(sorted(visited_stops), [0, 1])

    def test_rejects_malformed_coordinate(self):
        with self.assertRaisesRegex(ValueError, "depot"):
            optiroute_cpp.optimize_routes({"lat": 37.77}, [{"lat": 1, "lng": 2}], 1)

        with self.assertRaisesRegex(ValueError, r"stops\[0\]"):
            optiroute_cpp.optimize_routes(
                {"lat": 37.77, "lng": -122.42},
                ["not-a-coordinate"],
                1,
            )

    def test_translates_optimizer_validation_to_value_error(self):
        with self.assertRaisesRegex(ValueError, "at least one stop"):
            optiroute_cpp.optimize_routes({"lat": 0, "lng": 0}, [], 1)


if __name__ == "__main__":
    unittest.main()
