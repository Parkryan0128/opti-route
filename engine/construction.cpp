#include "optimizer_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace optiroute::detail {
namespace {

struct ConstructionState {
    ConstructionState(
        std::size_t num_vehicles,
        std::size_t num_stops,
        const Coordinate& depot
    ) :
        solution{RouteAssignments(num_vehicles), std::vector<double>(
            num_vehicles,
            0.0
        )},
        endpoints(num_vehicles, depot),
        assigned(num_stops, false) {}

    SolutionState solution;
    std::vector<Coordinate> endpoints;
    std::vector<bool> assigned;
};

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

void seed_vehicle_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    ConstructionState& state
) {
    for (std::size_t vehicle = 0;
         vehicle < state.solution.routes.size();
         ++vehicle) {
        double best_separation = -1.0;
        std::size_t best_stop = 0;

        for (std::size_t stop = 0; stop < stops.size(); ++stop) {
            if (state.assigned[stop]) {
                continue;
            }

            double separation = haversine_distance(depot, stops[stop]);
            if (vehicle > 0) {
                separation = std::numeric_limits<double>::infinity();
                for (std::size_t seeded_vehicle = 0;
                     seeded_vehicle < vehicle;
                     ++seeded_vehicle) {
                    const std::size_t seed =
                        state.solution.routes[seeded_vehicle].front();
                    separation = std::min(
                        separation,
                        haversine_distance(stops[seed], stops[stop])
                    );
                }
            }

            if (separation > best_separation + kComparisonEpsilon) {
                best_separation = separation;
                best_stop = stop;
            }
        }

        state.solution.routes[vehicle].push_back(best_stop);
        state.endpoints[vehicle] = stops[best_stop];
        state.solution.distances[vehicle] =
            2.0 * haversine_distance(depot, stops[best_stop]);
        state.assigned[best_stop] = true;
    }
}

void assign_remaining_stops(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    ConstructionState& state
) {
    double current_total_distance =
        summarize_distances(state.solution.distances).total_distance;

    for (std::size_t assigned_count = state.solution.routes.size();
         assigned_count < stops.size();
         ++assigned_count) {
        Objective best_objective{
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
        };
        double selected_added_distance = 0.0;
        std::size_t selected_vehicle = 0;
        std::size_t selected_stop = 0;

        for (std::size_t vehicle = 0;
             vehicle < state.solution.routes.size();
             ++vehicle) {
            for (std::size_t stop = 0; stop < stops.size(); ++stop) {
                if (state.assigned[stop]) {
                    continue;
                }

                const double added_distance =
                    haversine_distance(state.endpoints[vehicle], stops[stop]) +
                    haversine_distance(stops[stop], depot) -
                    haversine_distance(state.endpoints[vehicle], depot);
                const double projected_route_distance =
                    state.solution.distances[vehicle] + added_distance;
                Objective candidate{
                    projected_route_distance,
                    current_total_distance + added_distance,
                };
                for (std::size_t other = 0;
                     other < state.solution.distances.size();
                     ++other) {
                    if (other != vehicle) {
                        candidate.max_distance = std::max(
                            candidate.max_distance,
                            state.solution.distances[other]
                        );
                    }
                }

                if (is_better_objective(candidate, best_objective)) {
                    best_objective = candidate;
                    selected_added_distance = added_distance;
                    selected_vehicle = vehicle;
                    selected_stop = stop;
                }
            }
        }

        state.solution.routes[selected_vehicle].push_back(selected_stop);
        state.endpoints[selected_vehicle] = stops[selected_stop];
        state.solution.distances[selected_vehicle] += selected_added_distance;
        current_total_distance += selected_added_distance;
        state.assigned[selected_stop] = true;
    }
}

}  // namespace

void validate_inputs(
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
        throw std::invalid_argument(
            "num_vehicles cannot exceed number of stops"
        );
    }

    for (std::size_t index = 0; index < stops.size(); ++index) {
        validate_coordinate(
            stops[index],
            "stop[" + std::to_string(index) + "]"
        );
    }
}

SolutionState construct_initial_solution(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    std::size_t num_vehicles
) {
    ConstructionState state(num_vehicles, stops.size(), depot);
    seed_vehicle_routes(depot, stops, state);
    assign_remaining_stops(depot, stops, state);
    return std::move(state.solution);
}

}  // namespace optiroute::detail
