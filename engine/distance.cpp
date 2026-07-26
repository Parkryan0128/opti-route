#include "optimizer_internal.h"

#include <algorithm>
#include <cmath>

namespace optiroute {
namespace {

constexpr double kEarthRadiusKm = 6371.0088;

double degrees_to_radians(double degrees) {
    constexpr double kPi = 3.14159265358979323846;
    return degrees * kPi / 180.0;
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

namespace detail {

DistanceMatrix::DistanceMatrix(
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

double DistanceMatrix::between_nodes(
    std::size_t first,
    std::size_t second
) const {
    return distances_[first * size_ + second];
}

std::size_t DistanceMatrix::stop_node(std::size_t stop_index) {
    return stop_index + 1;
}

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

double matrix_route_distance(
    const DistanceMatrix& distances,
    const StopOrder& route
) {
    double total = 0.0;
    std::size_t previous = 0;
    for (const std::size_t stop : route) {
        const std::size_t current = DistanceMatrix::stop_node(stop);
        total += distances.between_nodes(previous, current);
        previous = current;
    }
    return total + distances.between_nodes(previous, 0);
}

std::vector<double> calculate_route_distances(
    const DistanceMatrix& distances,
    const RouteAssignments& routes
) {
    std::vector<double> route_distances;
    route_distances.reserve(routes.size());
    for (const auto& route : routes) {
        route_distances.push_back(matrix_route_distance(distances, route));
    }
    return route_distances;
}

}  // namespace detail
}  // namespace optiroute
