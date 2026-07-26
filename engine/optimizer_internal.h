#pragma once

#include "optimizer.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace optiroute::detail {

inline constexpr double kComparisonEpsilon = 1e-6;
inline constexpr double kInitialAnnealingTemperature = 0.05;
inline constexpr double kFinalAnnealingTemperature = 0.0005;
inline constexpr std::uint32_t kAnnealingSeed = 0x4F505449U;

using StopOrder = std::vector<std::size_t>;
using RouteAssignments = std::vector<StopOrder>;

struct Objective {
    double max_distance;
    double total_distance;
};

struct SolutionState {
    RouteAssignments routes;
    std::vector<double> distances;
};

class DistanceMatrix {
public:
    DistanceMatrix(
        const Coordinate& depot,
        const std::vector<Coordinate>& stops
    );

    double between_nodes(std::size_t first, std::size_t second) const;
    static std::size_t stop_node(std::size_t stop_index);

private:
    std::size_t size_;
    std::vector<double> distances_;
};

void validate_inputs(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    int num_vehicles
);

Objective summarize_distances(const std::vector<double>& route_distances);
bool is_better_objective(
    const Objective& candidate,
    const Objective& current
);
double matrix_route_distance(
    const DistanceMatrix& distances,
    const StopOrder& route
);
std::vector<double> calculate_route_distances(
    const DistanceMatrix& distances,
    const RouteAssignments& routes
);

SolutionState construct_initial_solution(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    std::size_t num_vehicles
);

void relocate_stop(
    RouteAssignments& routes,
    std::size_t source_route,
    std::size_t source_position,
    std::size_t target_route,
    std::size_t target_position
);
void swap_stops(
    RouteAssignments& routes,
    std::size_t first_route,
    std::size_t first_position,
    std::size_t second_route,
    std::size_t second_position
);
void reverse_segment(
    RouteAssignments& routes,
    std::size_t route,
    std::size_t first,
    std::size_t last
);

void improve_routes_locally(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const DistanceMatrix& distances,
    SolutionState& state
);
void improve_with_simulated_annealing(
    const std::vector<Coordinate>& stops,
    const DistanceMatrix& distances,
    SolutionState& state
);

}  // namespace optiroute::detail
