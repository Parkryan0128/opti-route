#include "optimizer_internal.h"

#include <utility>

namespace optiroute {
namespace {

std::vector<Coordinate> build_route_coordinates(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const detail::StopOrder& stop_order
) {
    std::vector<Coordinate> coordinates;
    coordinates.reserve(stop_order.size() + 2);
    coordinates.push_back(depot);
    for (const std::size_t stop_index : stop_order) {
        coordinates.push_back(stops[stop_index]);
    }
    coordinates.push_back(depot);
    return coordinates;
}

OptimizationResult build_result(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    detail::SolutionState&& state
) {
    const detail::Objective objective =
        detail::summarize_distances(state.distances);
    OptimizationResult result;
    result.total_distance_km = objective.total_distance;
    result.max_distance_km = objective.max_distance;
    result.routes.reserve(state.routes.size());

    for (std::size_t vehicle = 0; vehicle < state.routes.size(); ++vehicle) {
        Route route;
        route.vehicle_id = static_cast<int>(vehicle + 1);
        route.stop_order = std::move(state.routes[vehicle]);
        route.route_coordinates =
            build_route_coordinates(depot, stops, route.stop_order);
        route.distance_km = state.distances[vehicle];
        result.routes.push_back(std::move(route));
    }
    return result;
}

}  // namespace

OptimizationResult optimize_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    int num_vehicles
) {
    detail::validate_inputs(depot, stops, num_vehicles);
    const detail::DistanceMatrix distances(depot, stops);
    detail::SolutionState state = detail::construct_initial_solution(
        depot,
        stops,
        static_cast<std::size_t>(num_vehicles)
    );
    detail::improve_routes_locally(depot, stops, distances, state);
    detail::improve_with_simulated_annealing(stops, distances, state);
    detail::improve_routes_locally(depot, stops, distances, state);
    return build_result(depot, stops, std::move(state));
}

}  // namespace optiroute
