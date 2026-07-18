#pragma once

#include <cstddef>
#include <vector>

namespace optiroute {

struct Coordinate {
    double lat;
    double lng;
};

struct Route {
    int vehicle_id;
    std::vector<std::size_t> stop_order;
    std::vector<Coordinate> route_coordinates;
    double distance_km;
};

struct OptimizationResult {
    std::vector<Route> routes;
    double total_distance_km;
};

// Calculates straight-line distance over the Earth's surface in kilometers.
double haversine_distance(const Coordinate& from, const Coordinate& to);

// Solves the MVP's single-depot, closed-route, uncapacitated VRP.
//
// Stops are assigned with the globally nearest endpoint heuristic, then each
// resulting closed route is improved independently with 2-opt.
OptimizationResult optimize_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    int num_vehicles
);

}  // namespace optiroute
