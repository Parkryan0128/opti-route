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
// Ignore millimeter-scale objective changes to avoid floating-point move cycles.
constexpr double kComparisonEpsilon = 1e-6;

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

struct Objective {
    double max_distance;
    double total_distance;
};

bool is_better_objective(
    const Objective& candidate,
    const Objective& current
) {
    if (candidate.max_distance + kComparisonEpsilon < current.max_distance) {
        return true;
    }
    return
        std::abs(candidate.max_distance - current.max_distance) <=
            kComparisonEpsilon &&
        candidate.total_distance + kComparisonEpsilon < current.total_distance;
}

Objective summarize_distances(const std::vector<double>& route_distances) {
    Objective objective{0.0, 0.0};
    for (const double distance : route_distances) {
        objective.max_distance = std::max(objective.max_distance, distance);
        objective.total_distance += distance;
    }
    return objective;
}

Objective objective_with_changed_routes(
    const std::vector<double>& route_distances,
    std::size_t first_route,
    double first_distance,
    std::size_t second_route,
    double second_distance
) {
    Objective objective{0.0, 0.0};
    for (std::size_t route = 0; route < route_distances.size(); ++route) {
        double distance = route_distances[route];
        if (route == first_route) {
            distance = first_distance;
        } else if (route == second_route) {
            distance = second_distance;
        }
        objective.max_distance = std::max(objective.max_distance, distance);
        objective.total_distance += distance;
    }
    return objective;
}

class DistanceMatrix {
public:
    DistanceMatrix(
        const Coordinate& depot,
        const std::vector<Coordinate>& stops
    ) : size_(stops.size() + 1), distances_(size_ * size_, 0.0) {
        std::vector<Coordinate> coordinates;
        coordinates.reserve(size_);
        coordinates.push_back(depot);
        coordinates.insert(coordinates.end(), stops.begin(), stops.end());

        for (std::size_t first = 0; first < size_; ++first) {
            for (std::size_t second = first + 1; second < size_; ++second) {
                const double distance =
                    haversine_distance(coordinates[first], coordinates[second]);
                distances_[first * size_ + second] = distance;
                distances_[second * size_ + first] = distance;
            }
        }
    }

    double between_nodes(std::size_t first, std::size_t second) const {
        return distances_[first * size_ + second];
    }

    static std::size_t stop_node(std::size_t stop_index) {
        return stop_index + 1;
    }

private:
    std::size_t size_;
    std::vector<double> distances_;
};

std::size_t node_before(
    const std::vector<std::size_t>& route,
    std::size_t position
) {
    return position == 0
        ? 0
        : DistanceMatrix::stop_node(route[position - 1]);
}

std::size_t node_after(
    const std::vector<std::size_t>& route,
    std::size_t position
) {
    return position + 1 == route.size()
        ? 0
        : DistanceMatrix::stop_node(route[position + 1]);
}

double removal_delta(
    const DistanceMatrix& distances,
    const std::vector<std::size_t>& route,
    std::size_t position
) {
    const std::size_t before = node_before(route, position);
    const std::size_t removed =
        DistanceMatrix::stop_node(route[position]);
    const std::size_t after = node_after(route, position);
    return
        distances.between_nodes(before, after) -
        distances.between_nodes(before, removed) -
        distances.between_nodes(removed, after);
}

double insertion_delta(
    const DistanceMatrix& distances,
    const std::vector<std::size_t>& route,
    std::size_t stop_index,
    std::size_t position
) {
    const std::size_t before = position == 0
        ? 0
        : DistanceMatrix::stop_node(route[position - 1]);
    const std::size_t inserted = DistanceMatrix::stop_node(stop_index);
    const std::size_t after = position == route.size()
        ? 0
        : DistanceMatrix::stop_node(route[position]);
    return
        distances.between_nodes(before, inserted) +
        distances.between_nodes(inserted, after) -
        distances.between_nodes(before, after);
}

double replacement_delta(
    const DistanceMatrix& distances,
    const std::vector<std::size_t>& route,
    std::size_t position,
    std::size_t replacement_stop
) {
    const std::size_t before = node_before(route, position);
    const std::size_t current =
        DistanceMatrix::stop_node(route[position]);
    const std::size_t replacement =
        DistanceMatrix::stop_node(replacement_stop);
    const std::size_t after = node_after(route, position);
    return
        distances.between_nodes(before, replacement) +
        distances.between_nodes(replacement, after) -
        distances.between_nodes(before, current) -
        distances.between_nodes(current, after);
}

enum class MoveType {
    relocate,
    swap,
};

struct CrossRouteMove {
    bool found = false;
    MoveType type = MoveType::relocate;
    std::size_t first_route = 0;
    std::size_t first_position = 0;
    std::size_t second_route = 0;
    std::size_t second_position = 0;
    Objective objective{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
};

void consider_cross_route_move(
    CrossRouteMove& best,
    MoveType type,
    std::size_t first_route,
    std::size_t first_position,
    std::size_t second_route,
    std::size_t second_position,
    const Objective& objective
) {
    if (!is_better_objective(objective, best.objective)) {
        return;
    }
    best = {
        true,
        type,
        first_route,
        first_position,
        second_route,
        second_position,
        objective,
    };
}

void apply_cross_route_move(
    const CrossRouteMove& move,
    std::vector<std::vector<std::size_t>>& assignments
) {
    if (move.type == MoveType::relocate) {
        const std::size_t stop =
            assignments[move.first_route][move.first_position];
        assignments[move.first_route].erase(
            assignments[move.first_route].begin() +
            static_cast<std::ptrdiff_t>(move.first_position)
        );
        assignments[move.second_route].insert(
            assignments[move.second_route].begin() +
                static_cast<std::ptrdiff_t>(move.second_position),
            stop
        );
        return;
    }

    std::swap(
        assignments[move.first_route][move.first_position],
        assignments[move.second_route][move.second_position]
    );
}

void refresh_changed_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const CrossRouteMove& move,
    std::vector<std::vector<std::size_t>>& assignments,
    std::vector<double>& route_distances
) {
    for (const std::size_t route : {
             move.first_route,
             move.second_route,
         }) {
        improve_with_two_opt(depot, stops, assignments[route]);
        route_distances[route] =
            closed_route_distance(depot, stops, assignments[route]);
    }
}

void improve_across_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    std::vector<std::vector<std::size_t>>& assignments,
    std::vector<double>& route_distances
) {
    const DistanceMatrix distances(depot, stops);

    while (true) {
        const Objective current = summarize_distances(route_distances);
        CrossRouteMove best;
        best.objective = current;

        for (std::size_t source = 0; source < assignments.size(); ++source) {
            if (assignments[source].size() <= 1) {
                continue;
            }
            for (std::size_t source_position = 0;
                 source_position < assignments[source].size();
                 ++source_position) {
                const std::size_t stop =
                    assignments[source][source_position];
                const double source_distance = std::max(
                    0.0,
                    route_distances[source] +
                        removal_delta(
                            distances,
                            assignments[source],
                            source_position
                        )
                );

                for (std::size_t target = 0;
                     target < assignments.size();
                     ++target) {
                    if (target == source) {
                        continue;
                    }
                    for (std::size_t target_position = 0;
                         target_position <= assignments[target].size();
                         ++target_position) {
                        const double target_distance =
                            route_distances[target] +
                            insertion_delta(
                                distances,
                                assignments[target],
                                stop,
                                target_position
                            );
                        const Objective candidate =
                            objective_with_changed_routes(
                                route_distances,
                                source,
                                source_distance,
                                target,
                                target_distance
                            );

                        consider_cross_route_move(
                            best,
                            MoveType::relocate,
                            source,
                            source_position,
                            target,
                            target_position,
                            candidate
                        );
                    }
                }
            }
        }

        for (std::size_t first = 0; first < assignments.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < assignments.size();
                 ++second) {
                for (std::size_t first_position = 0;
                     first_position < assignments[first].size();
                     ++first_position) {
                    for (std::size_t second_position = 0;
                         second_position < assignments[second].size();
                         ++second_position) {
                        const std::size_t first_stop =
                            assignments[first][first_position];
                        const std::size_t second_stop =
                            assignments[second][second_position];
                        const double first_distance =
                            route_distances[first] +
                            replacement_delta(
                                distances,
                                assignments[first],
                                first_position,
                                second_stop
                            );
                        const double second_distance =
                            route_distances[second] +
                            replacement_delta(
                                distances,
                                assignments[second],
                                second_position,
                                first_stop
                            );
                        const Objective candidate =
                            objective_with_changed_routes(
                                route_distances,
                                first,
                                first_distance,
                                second,
                                second_distance
                            );

                        consider_cross_route_move(
                            best,
                            MoveType::swap,
                            first,
                            first_position,
                            second,
                            second_position,
                            candidate
                        );
                    }
                }
            }
        }

        if (!best.found) {
            return;
        }

        apply_cross_route_move(best, assignments);
        refresh_changed_routes(
            depot,
            stops,
            best,
            assignments,
            route_distances
        );
    }
}

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

struct AssignmentState {
    explicit AssignmentState(
        std::size_t num_vehicles,
        std::size_t num_stops,
        const Coordinate& depot
    ) :
        routes(num_vehicles),
        endpoints(num_vehicles, depot),
        distances(num_vehicles, 0.0),
        assigned(num_stops, false) {}

    std::vector<std::vector<std::size_t>> routes;
    std::vector<Coordinate> endpoints;
    std::vector<double> distances;
    std::vector<bool> assigned;
};

void seed_vehicle_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    AssignmentState& state
) {
    for (std::size_t vehicle = 0; vehicle < state.routes.size(); ++vehicle) {
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
                        state.routes[seeded_vehicle].front();
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

        state.routes[vehicle].push_back(best_stop);
        state.endpoints[vehicle] = stops[best_stop];
        state.distances[vehicle] =
            2.0 * haversine_distance(depot, stops[best_stop]);
        state.assigned[best_stop] = true;
    }
}

void assign_remaining_stops(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    AssignmentState& state
) {
    double current_total_distance =
        summarize_distances(state.distances).total_distance;

    for (std::size_t assigned_count = state.routes.size();
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
             vehicle < state.routes.size();
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
                    state.distances[vehicle] + added_distance;
                Objective candidate{
                    projected_route_distance,
                    current_total_distance + added_distance,
                };
                for (std::size_t other = 0;
                     other < state.distances.size();
                     ++other) {
                    if (other != vehicle) {
                        candidate.max_distance = std::max(
                            candidate.max_distance,
                            state.distances[other]
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

        state.routes[selected_vehicle].push_back(selected_stop);
        state.endpoints[selected_vehicle] = stops[selected_stop];
        state.distances[selected_vehicle] += selected_added_distance;
        current_total_distance += selected_added_distance;
        state.assigned[selected_stop] = true;
    }
}

void improve_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    AssignmentState& state
) {
    for (std::size_t vehicle = 0; vehicle < state.routes.size(); ++vehicle) {
        improve_with_two_opt(depot, stops, state.routes[vehicle]);
        state.distances[vehicle] =
            closed_route_distance(depot, stops, state.routes[vehicle]);
    }
    improve_across_routes(depot, stops, state.routes, state.distances);
}

std::vector<Coordinate> build_route_coordinates(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const std::vector<std::size_t>& stop_order
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
    AssignmentState&& state
) {
    const Objective objective = summarize_distances(state.distances);
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
    validate_inputs(depot, stops, num_vehicles);
    AssignmentState state(
        static_cast<std::size_t>(num_vehicles),
        stops.size(),
        depot
    );
    seed_vehicle_routes(depot, stops, state);
    assign_remaining_stops(depot, stops, state);
    improve_routes(depot, stops, state);
    return build_result(depot, stops, std::move(state));
}

}  // namespace optiroute
