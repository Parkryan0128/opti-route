#include "optimizer_internal.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace optiroute::detail {
namespace {

double closed_route_distance(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const StopOrder& stop_order
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
    StopOrder& stop_order
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
                    end + 1 == stop_order.size()
                    ? depot
                    : stops[stop_order[end + 1]];
                const double current_edges =
                    haversine_distance(before, first) +
                    haversine_distance(last, after);
                const double swapped_edges =
                    haversine_distance(before, last) +
                    haversine_distance(first, after);

                if (swapped_edges + kComparisonEpsilon < current_edges) {
                    std::reverse(
                        stop_order.begin() +
                            static_cast<std::ptrdiff_t>(start),
                        stop_order.begin() +
                            static_cast<std::ptrdiff_t>(end + 1)
                    );
                    improved = true;
                }
            }
        }
    }
}

std::size_t node_before(const StopOrder& route, std::size_t position) {
    return position == 0
        ? 0
        : DistanceMatrix::stop_node(route[position - 1]);
}

std::size_t node_after(const StopOrder& route, std::size_t position) {
    return position + 1 == route.size()
        ? 0
        : DistanceMatrix::stop_node(route[position + 1]);
}

double removal_delta(
    const DistanceMatrix& distances,
    const StopOrder& route,
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
    const StopOrder& route,
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
    const StopOrder& route,
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

void consider_move(
    CrossRouteMove& best,
    MoveType type,
    std::size_t first_route,
    std::size_t first_position,
    std::size_t second_route,
    std::size_t second_position,
    const Objective& objective
) {
    if (is_better_objective(objective, best.objective)) {
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
}

void search_relocate_moves(
    const DistanceMatrix& distances,
    const RouteAssignments& assignments,
    const std::vector<double>& route_distances,
    CrossRouteMove& best
) {
    for (std::size_t source = 0; source < assignments.size(); ++source) {
        if (assignments[source].size() <= 1) {
            continue;
        }
        for (std::size_t source_position = 0;
             source_position < assignments[source].size();
             ++source_position) {
            const std::size_t stop = assignments[source][source_position];
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
                    consider_move(
                        best,
                        MoveType::relocate,
                        source,
                        source_position,
                        target,
                        target_position,
                        objective_with_changed_routes(
                            route_distances,
                            source,
                            source_distance,
                            target,
                            target_distance
                        )
                    );
                }
            }
        }
    }
}

void search_swap_moves(
    const DistanceMatrix& distances,
    const RouteAssignments& assignments,
    const std::vector<double>& route_distances,
    CrossRouteMove& best
) {
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
                    consider_move(
                        best,
                        MoveType::swap,
                        first,
                        first_position,
                        second,
                        second_position,
                        objective_with_changed_routes(
                            route_distances,
                            first,
                            first_distance,
                            second,
                            second_distance
                        )
                    );
                }
            }
        }
    }
}

void refresh_changed_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const CrossRouteMove& move,
    SolutionState& state
) {
    for (const std::size_t route : {
             move.first_route,
             move.second_route,
         }) {
        improve_with_two_opt(depot, stops, state.routes[route]);
        state.distances[route] =
            closed_route_distance(depot, stops, state.routes[route]);
    }
}

void improve_across_routes(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const DistanceMatrix& distances,
    SolutionState& state
) {
    while (true) {
        CrossRouteMove best;
        best.objective = summarize_distances(state.distances);
        search_relocate_moves(
            distances,
            state.routes,
            state.distances,
            best
        );
        search_swap_moves(
            distances,
            state.routes,
            state.distances,
            best
        );
        if (!best.found) {
            return;
        }

        if (best.type == MoveType::relocate) {
            relocate_stop(
                state.routes,
                best.first_route,
                best.first_position,
                best.second_route,
                best.second_position
            );
        } else {
            swap_stops(
                state.routes,
                best.first_route,
                best.first_position,
                best.second_route,
                best.second_position
            );
        }
        refresh_changed_routes(depot, stops, best, state);
    }
}

void improve_each_route(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    SolutionState& state
) {
    for (std::size_t vehicle = 0; vehicle < state.routes.size(); ++vehicle) {
        improve_with_two_opt(depot, stops, state.routes[vehicle]);
        state.distances[vehicle] =
            closed_route_distance(depot, stops, state.routes[vehicle]);
    }
}

}  // namespace

void relocate_stop(
    RouteAssignments& routes,
    std::size_t source_route,
    std::size_t source_position,
    std::size_t target_route,
    std::size_t target_position
) {
    const std::size_t stop = routes[source_route][source_position];
    routes[source_route].erase(
        routes[source_route].begin() +
            static_cast<std::ptrdiff_t>(source_position)
    );
    routes[target_route].insert(
        routes[target_route].begin() +
            static_cast<std::ptrdiff_t>(target_position),
        stop
    );
}

void swap_stops(
    RouteAssignments& routes,
    std::size_t first_route,
    std::size_t first_position,
    std::size_t second_route,
    std::size_t second_position
) {
    std::swap(
        routes[first_route][first_position],
        routes[second_route][second_position]
    );
}

void reverse_segment(
    RouteAssignments& routes,
    std::size_t route,
    std::size_t first,
    std::size_t last
) {
    std::reverse(
        routes[route].begin() + static_cast<std::ptrdiff_t>(first),
        routes[route].begin() + static_cast<std::ptrdiff_t>(last + 1)
    );
}

void improve_routes_locally(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const DistanceMatrix& distances,
    SolutionState& state
) {
    improve_each_route(depot, stops, state);
    improve_across_routes(depot, stops, distances, state);
}

}  // namespace optiroute::detail
