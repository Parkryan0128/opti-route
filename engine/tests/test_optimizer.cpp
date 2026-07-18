#include "../optimizer.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using optiroute::Coordinate;

bool nearly_equal(double left, double right, double tolerance = 1e-6) {
    return std::abs(left - right) <= tolerance;
}

bool same_coordinate(const Coordinate& left, const Coordinate& right) {
    return nearly_equal(left.lat, right.lat) &&
           nearly_equal(left.lng, right.lng);
}

template <typename Function>
void assert_invalid_argument(Function&& function) {
    bool thrown = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    assert(thrown);
}

void test_haversine_distance() {
    const Coordinate origin{0.0, 0.0};
    const Coordinate one_degree_east{0.0, 1.0};

    assert(nearly_equal(
        optiroute::haversine_distance(origin, origin),
        0.0
    ));
    assert(nearly_equal(
        optiroute::haversine_distance(origin, one_degree_east),
        111.195080,
        0.001
    ));
}

void test_result_contract_and_assignment() {
    const Coordinate depot{37.77, -122.42};
    const std::vector<Coordinate> stops{
        {37.78, -122.43},
        {37.79, -122.41},
        {37.76, -122.40},
        {37.75, -122.44},
    };

    const auto result = optiroute::optimize_routes(depot, stops, 2);

    assert(result.routes.size() == 2);
    std::vector<int> visit_count(stops.size(), 0);
    double route_distance_sum = 0.0;

    for (std::size_t index = 0; index < result.routes.size(); ++index) {
        const auto& route = result.routes[index];
        assert(route.vehicle_id == static_cast<int>(index + 1));
        assert(route.route_coordinates.size() == route.stop_order.size() + 2);
        assert(same_coordinate(route.route_coordinates.front(), depot));
        assert(same_coordinate(route.route_coordinates.back(), depot));

        for (std::size_t position = 0; position < route.stop_order.size();
             ++position) {
            const std::size_t stop_index = route.stop_order[position];
            assert(stop_index < stops.size());
            ++visit_count[stop_index];
            assert(same_coordinate(
                route.route_coordinates[position + 1],
                stops[stop_index]
            ));
        }

        assert(route.distance_km >= 0.0);
        route_distance_sum += route.distance_km;
    }

    for (const int count : visit_count) {
        assert(count == 1);
    }
    assert(nearly_equal(result.total_distance_km, route_distance_sum));
}

void test_validation() {
    const Coordinate depot{0.0, 0.0};
    const std::vector<Coordinate> one_stop{{1.0, 1.0}};

    assert_invalid_argument([&] {
        optiroute::optimize_routes(depot, {}, 1);
    });
    assert_invalid_argument([&] {
        optiroute::optimize_routes(depot, one_stop, 0);
    });
    assert_invalid_argument([&] {
        optiroute::optimize_routes(depot, one_stop, 2);
    });
    assert_invalid_argument([&] {
        optiroute::optimize_routes({91.0, 0.0}, one_stop, 1);
    });
    assert_invalid_argument([&] {
        optiroute::optimize_routes(depot, {{0.0, 181.0}}, 1);
    });
}

}  // namespace

int main() {
    test_haversine_distance();
    test_result_contract_and_assignment();
    test_validation();
    std::cout << "All optimizer tests passed.\n";
    return 0;
}
