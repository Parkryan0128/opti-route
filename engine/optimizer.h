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
    double max_distance_km;
};

// Calculates straight-line distance over the Earth's surface in kilometers.
double haversine_distance(const Coordinate& from, const Coordinate& to);

// Solves the single-depot, closed-route, uncapacitated VRP.
//
// Every vehicle is seeded with a geographically separated stop. Remaining
// stops are assigned to minimize projected longest-route distance, with total
// distance as a tie-breaker. Local search improves each route with 2-opt and
// relocates or swaps stops across routes while preserving non-empty vehicles.
OptimizationResult optimize_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    int num_vehicles
);

}  // namespace optiroute
