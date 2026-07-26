#include "optimizer_internal.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace optiroute::detail {
namespace {

std::size_t random_index(
    std::mt19937& generator,
    std::size_t upper_bound
) {
    return std::uniform_int_distribution<std::size_t>(
        0,
        upper_bound - 1
    )(generator);
}

bool propose_annealing_neighbor(
    RouteAssignments& routes,
    std::mt19937& generator
) {
    const bool has_multi_stop_route = std::any_of(
        routes.begin(),
        routes.end(),
        [](const auto& route) {
            return route.size() > 1;
        }
    );
    if (!has_multi_stop_route) {
        return false;
    }

    for (int attempt = 0; attempt < 32; ++attempt) {
        const std::size_t move = random_index(generator, 3);
        if (move == 0 && routes.size() > 1) {
            const std::size_t source = random_index(generator, routes.size());
            if (routes[source].size() <= 1) {
                continue;
            }
            std::size_t target = random_index(generator, routes.size() - 1);
            if (target >= source) {
                ++target;
            }
            const std::size_t source_position =
                random_index(generator, routes[source].size());
            const std::size_t target_position =
                random_index(generator, routes[target].size() + 1);
            relocate_stop(
                routes,
                source,
                source_position,
                target,
                target_position
            );
            return true;
        }

        if (move == 1 && routes.size() > 1) {
            const std::size_t first = random_index(generator, routes.size());
            std::size_t second = random_index(generator, routes.size() - 1);
            if (second >= first) {
                ++second;
            }
            const std::size_t first_position =
                random_index(generator, routes[first].size());
            const std::size_t second_position =
                random_index(generator, routes[second].size());
            swap_stops(
                routes,
                first,
                first_position,
                second,
                second_position
            );
            return true;
        }

        if (move == 2) {
            const std::size_t route = random_index(generator, routes.size());
            if (routes[route].size() <= 1) {
                continue;
            }
            std::size_t first =
                random_index(generator, routes[route].size());
            std::size_t second =
                random_index(generator, routes[route].size() - 1);
            if (second >= first) {
                ++second;
            }
            if (first > second) {
                std::swap(first, second);
            }
            reverse_segment(routes, route, first, second);
            return true;
        }
    }
    return false;
}

double annealing_objective_change(
    const Objective& candidate,
    const Objective& current
) {
    if (
        std::abs(candidate.max_distance - current.max_distance) >
        kComparisonEpsilon
    ) {
        return
            (candidate.max_distance - current.max_distance) /
            std::max(current.max_distance, 1.0);
    }
    return
        (candidate.total_distance - current.total_distance) /
        std::max(current.total_distance, 1.0);
}

}  // namespace

void improve_with_simulated_annealing(
    const std::vector<Coordinate>& stops,
    const DistanceMatrix& distances,
    SolutionState& state
) {
    const int iterations =
        1000 + static_cast<int>(stops.size()) * 200;
    std::mt19937 generator(
        kAnnealingSeed ^
        static_cast<std::uint32_t>(stops.size()) ^
        (static_cast<std::uint32_t>(state.routes.size()) << 16U)
    );
    std::uniform_real_distribution<double> probability(0.0, 1.0);

    auto current_routes = state.routes;
    auto current_distances = state.distances;
    Objective current_objective = summarize_distances(current_distances);
    auto best_routes = current_routes;
    auto best_distances = current_distances;
    Objective best_objective = current_objective;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        auto candidate_routes = current_routes;
        if (!propose_annealing_neighbor(candidate_routes, generator)) {
            break;
        }
        auto candidate_distances =
            calculate_route_distances(distances, candidate_routes);
        const Objective candidate_objective =
            summarize_distances(candidate_distances);
        const double progress =
            static_cast<double>(iteration) /
            static_cast<double>(iterations - 1);
        const double temperature =
            kInitialAnnealingTemperature *
            std::pow(
                kFinalAnnealingTemperature /
                    kInitialAnnealingTemperature,
                progress
            );
        const double change =
            annealing_objective_change(candidate_objective, current_objective);
        const bool accept =
            change <= 0.0 ||
            probability(generator) < std::exp(-change / temperature);
        if (!accept) {
            continue;
        }

        current_routes = std::move(candidate_routes);
        current_distances = std::move(candidate_distances);
        current_objective = candidate_objective;
        if (is_better_objective(current_objective, best_objective)) {
            best_routes = current_routes;
            best_distances = current_distances;
            best_objective = current_objective;
        }
    }

    state.routes = std::move(best_routes);
    state.distances = std::move(best_distances);
}

}  // namespace optiroute::detail
