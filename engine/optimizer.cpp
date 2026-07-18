#include "optimizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace optiroute {
namespace {

constexpr double kEarthRadiusKm = 6371.0088;
constexpr double kComparisonEpsilon = 1e-12;

double degrees_to_radians(double degrees) {
    constexpr double kPi = 3.14159265358979323846;
    return degrees * kPi / 180.0;
}

void validate_coordinate(const Coordinate& coordinate, const std::string& name) {
    if (!std::isfinite(coordinate.lat) || !std::isfinite(coordinate.lng)) {
        throw std::invalid_argument(name + " must contain finite coordinates");
    }
    if (coordinate.lat < -90.0 || coordinate.lat > 90.0) {
        throw std::invalid_argument(name + " latitude must be between -90 and 90");
    }
    if (coordinate.lng < -180.0 || coordinate.lng > 180.0) {
        throw std::invalid_argument(name + " longitude must be between -180 and 180");
    }
}

double closed_route_distance(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const std::vector<std::size_t>& stop_order
) {
    double distance = 0.0;
    const Coordinate* previous = &depot;

    for (const std::size_t stop_index : stop_order) {
        distance += haversine_distance(*previous, stops[stop_index]);
        previous = &stops[stop_index];
    }

    distance += haversine_distance(*previous, depot);
    return distance;
}

void improve_with_two_opt(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    std::vector<std::size_t>& stop_order
) {
    if (stop_order.size() < 3) {
        return;
    }

    bool improved = true;
    while (improved) {
        improved = false;

        for (std::size_t start = 0; start + 1 < stop_order.size(); ++start) {
            for (std::size_t end = start + 1; end < stop_order.size(); ++end) {
                const Coordinate& before =
                    start == 0 ? depot : stops[stop_order[start - 1]];
                const Coordinate& first = stops[stop_order[start]];
                const Coordinate& last = stops[stop_order[end]];
                const Coordinate& after =
                    end + 1 == stop_order.size() ? depot : stops[stop_order[end + 1]];

                const double current_edges =
                    haversine_distance(before, first) +
                    haversine_distance(last, after);
                const double swapped_edges =
                    haversine_distance(before, last) +
                    haversine_distance(first, after);

                if (swapped_edges + kComparisonEpsilon < current_edges) {
                    std::reverse(
                        stop_order.begin() + static_cast<std::ptrdiff_t>(start),
                        stop_order.begin() + static_cast<std::ptrdiff_t>(end + 1)
                    );
                    improved = true;
                }
            }
        }
    }
}

}  // namespace

double haversine_distance(const Coordinate& from, const Coordinate& to) {
    const double from_lat = degrees_to_radians(from.lat);
    const double to_lat = degrees_to_radians(to.lat);
    const double delta_lat = to_lat - from_lat;
    const double delta_lng = degrees_to_radians(to.lng - from.lng);

    const double sin_lat = std::sin(delta_lat / 2.0);
    const double sin_lng = std::sin(delta_lng / 2.0);
    const double haversine =
        sin_lat * sin_lat +
        std::cos(from_lat) * std::cos(to_lat) * sin_lng * sin_lng;
    const double central_angle =
        2.0 * std::asin(std::sqrt(std::clamp(haversine, 0.0, 1.0)));

    return kEarthRadiusKm * central_angle;
}

OptimizationResult optimize_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    int num_vehicles
) {
    validate_coordinate(depot, "depot");
    if (stops.empty()) {
        throw std::invalid_argument("at least one stop is required");
    }
    if (stops.size() > 100) {
        throw std::invalid_argument("no more than 100 stops are allowed");
    }
    if (num_vehicles < 1) {
        throw std::invalid_argument("num_vehicles must be at least 1");
    }
    if (static_cast<std::size_t>(num_vehicles) > stops.size()) {
        throw std::invalid_argument("num_vehicles cannot exceed number of stops");
    }

    for (std::size_t index = 0; index < stops.size(); ++index) {
        validate_coordinate(stops[index], "stop[" + std::to_string(index) + "]");
    }

    std::vector<std::vector<std::size_t>> assignments(
        static_cast<std::size_t>(num_vehicles)
    );
    std::vector<Coordinate> endpoints(
        static_cast<std::size_t>(num_vehicles),
        depot
    );
    std::vector<bool> assigned(stops.size(), false);

    for (std::size_t assigned_count = 0; assigned_count < stops.size();
         ++assigned_count) {
        double best_distance = std::numeric_limits<double>::infinity();
        std::size_t best_vehicle = 0;
        std::size_t best_stop = 0;

        // Iteration order gives deterministic ties: lower vehicle ID, then
        // lower stop index.
        for (std::size_t vehicle = 0; vehicle < assignments.size(); ++vehicle) {
            for (std::size_t stop = 0; stop < stops.size(); ++stop) {
                if (assigned[stop]) {
                    continue;
                }

                const double distance =
                    haversine_distance(endpoints[vehicle], stops[stop]);
                if (distance + kComparisonEpsilon < best_distance) {
                    best_distance = distance;
                    best_vehicle = vehicle;
                    best_stop = stop;
                }
            }
        }

        assignments[best_vehicle].push_back(best_stop);
        endpoints[best_vehicle] = stops[best_stop];
        assigned[best_stop] = true;
    }

    OptimizationResult result;
    result.total_distance_km = 0.0;
    result.routes.reserve(assignments.size());

    for (std::size_t vehicle = 0; vehicle < assignments.size(); ++vehicle) {
        auto& stop_order = assignments[vehicle];
        improve_with_two_opt(depot, stops, stop_order);

        Route route;
        route.vehicle_id = static_cast<int>(vehicle + 1);
        route.stop_order = std::move(stop_order);
        route.route_coordinates.reserve(route.stop_order.size() + 2);
        route.route_coordinates.push_back(depot);
        for (const std::size_t stop_index : route.stop_order) {
            route.route_coordinates.push_back(stops[stop_index]);
        }
        route.route_coordinates.push_back(depot);
        route.distance_km =
            closed_route_distance(depot, stops, route.stop_order);

        result.total_distance_km += route.distance_km;
        result.routes.push_back(std::move(route));
    }

    return result;
}

}  // namespace optiroute
