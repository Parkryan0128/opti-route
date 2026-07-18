#include "../optimizer.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <random>
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

double route_distance(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const std::vector<std::size_t>& stop_order
) {
    double total = 0.0;
    Coordinate previous = depot;
    for (const std::size_t index : stop_order) {
        total += optiroute::haversine_distance(previous, stops[index]);
        previous = stops[index];
    }
    return total + optiroute::haversine_distance(previous, depot);
}

void assert_two_opt_local_optimum(
    const Coordinate& depot,
    const std::vector<Coordinate>& stops,
    const std::vector<std::size_t>& order
) {
    const double current_distance = route_distance(depot, stops, order);
    for (std::size_t start = 0; start + 1 < order.size(); ++start) {
        for (std::size_t end = start + 1; end < order.size(); ++end) {
            auto candidate = order;
            std::reverse(
                candidate.begin() + static_cast<std::ptrdiff_t>(start),
                candidate.begin() + static_cast<std::ptrdiff_t>(end + 1)
            );
            assert(
                route_distance(depot, stops, candidate) + 1e-6 >=
                current_distance
            );
        }
    }
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
    const Coordinate near_dateline_east{0.0, 179.0};
    const Coordinate near_dateline_west{0.0, -179.0};
    const Coordinate north_pole{90.0, 0.0};
    const Coordinate south_pole{-90.0, 0.0};

    assert(nearly_equal(
        optiroute::haversine_distance(origin, origin),
        0.0
    ));
    assert(nearly_equal(
        optiroute::haversine_distance(origin, one_degree_east),
        111.195080,
        0.001
    ));
    assert(nearly_equal(
        optiroute::haversine_distance(origin, one_degree_east),
        optiroute::haversine_distance(one_degree_east, origin)
    ));
    assert(nearly_equal(
        optiroute::haversine_distance(
            near_dateline_east,
            near_dateline_west
        ),
        222.390160,
        0.001
    ));
    assert(nearly_equal(
        optiroute::haversine_distance(north_pole, south_pole),
        20015.114442,
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

void test_single_stop_closed_route() {
    const Coordinate depot{0.0, 0.0};
    const std::vector<Coordinate> stops{{0.0, 1.0}};

    const auto result = optiroute::optimize_routes(depot, stops, 1);
    const auto& route = result.routes.front();

    assert(route.stop_order == std::vector<std::size_t>{0});
    assert(route.route_coordinates.size() == 3);
    assert(nearly_equal(
        route.distance_km,
        2.0 * optiroute::haversine_distance(depot, stops.front())
    ));
    assert(nearly_equal(result.total_distance_km, route.distance_km));
}

void test_two_opt_improves_greedy_route() {
    const Coordinate depot{0.0, 0.0};
    const std::vector<Coordinate> stops{
        {-0.3523344703, -0.6983016522},
        {0.3018689461, -0.8551274267},
        {0.0717640086, -0.2686221662},
        {-0.8840021505, 0.0148714664},
        {-0.9250086831, -0.1327086327},
        {-0.8602891529, -0.8185739733},
        {-0.1509616217, 0.6537042493},
    };
    const std::vector<std::size_t> greedy_order{2, 0, 5, 4, 3, 6, 1};

    const auto result = optiroute::optimize_routes(depot, stops, 1);
    const auto& optimized_route = result.routes.front();

    assert(optimized_route.stop_order != greedy_order);
    assert(
        optimized_route.distance_km + 1e-6 <
        route_distance(depot, stops, greedy_order)
    );
}

void test_duplicate_stops_are_preserved() {
    const Coordinate depot{0.0, 0.0};
    const std::vector<Coordinate> stops{
        {1.0, 1.0},
        {1.0, 1.0},
        {2.0, 2.0},
    };

    const auto result = optiroute::optimize_routes(depot, stops, 1);
    const auto& order = result.routes.front().stop_order;

    assert(order.size() == stops.size());
    std::vector<int> visit_count(stops.size(), 0);
    for (const std::size_t index : order) {
        ++visit_count[index];
    }
    for (const int count : visit_count) {
        assert(count == 1);
    }
}

void test_unused_vehicle_has_depot_only_route() {
    const Coordinate depot{0.0, 0.0};
    const std::vector<Coordinate> stops{
        {0.0, 1.0},
        {0.0, 2.0},
        {0.0, 3.0},
    };

    const auto result = optiroute::optimize_routes(depot, stops, 2);
    const auto& unused_route = result.routes[1];

    assert(unused_route.stop_order.empty());
    assert(unused_route.route_coordinates.size() == 2);
    assert(same_coordinate(unused_route.route_coordinates.front(), depot));
    assert(same_coordinate(unused_route.route_coordinates.back(), depot));
    assert(nearly_equal(unused_route.distance_km, 0.0));
}

void test_is_deterministic() {
    const Coordinate depot{0.0, 0.0};
    const std::vector<Coordinate> stops{
        {1.0, 0.0},
        {-1.0, 0.0},
        {0.0, 1.0},
        {0.0, -1.0},
    };

    const auto first = optiroute::optimize_routes(depot, stops, 2);
    const auto second = optiroute::optimize_routes(depot, stops, 2);

    assert(first.routes.size() == second.routes.size());
    assert(nearly_equal(first.total_distance_km, second.total_distance_km));
    for (std::size_t index = 0; index < first.routes.size(); ++index) {
        assert(
            first.routes[index].stop_order ==
            second.routes[index].stop_order
        );
        assert(nearly_equal(
            first.routes[index].distance_km,
            second.routes[index].distance_km
        ));
    }
}

void test_randomized_result_invariants() {
    std::mt19937 generator(20260717);
    std::uniform_real_distribution<double> latitude(-89.0, 89.0);
    std::uniform_real_distribution<double> longitude(-179.0, 179.0);
    std::uniform_int_distribution<int> stop_count_distribution(1, 20);

    for (int iteration = 0; iteration < 100; ++iteration) {
        const Coordinate depot{latitude(generator), longitude(generator)};
        const int stop_count = stop_count_distribution(generator);
        std::vector<Coordinate> stops;
        stops.reserve(static_cast<std::size_t>(stop_count));
        for (int index = 0; index < stop_count; ++index) {
            stops.push_back({latitude(generator), longitude(generator)});
        }

        std::uniform_int_distribution<int> vehicle_distribution(1, stop_count);
        const int num_vehicles = vehicle_distribution(generator);
        const auto result =
            optiroute::optimize_routes(depot, stops, num_vehicles);

        assert(result.routes.size() == static_cast<std::size_t>(num_vehicles));
        assert(std::isfinite(result.total_distance_km));
        std::vector<int> visit_count(stops.size(), 0);
        double total_distance = 0.0;

        for (const auto& route : result.routes) {
            assert(std::isfinite(route.distance_km));
            assert(route.distance_km >= 0.0);
            assert(route.route_coordinates.size() == route.stop_order.size() + 2);
            assert(same_coordinate(route.route_coordinates.front(), depot));
            assert(same_coordinate(route.route_coordinates.back(), depot));

            for (std::size_t position = 0;
                 position < route.stop_order.size();
                 ++position) {
                const std::size_t stop_index = route.stop_order[position];
                assert(stop_index < stops.size());
                ++visit_count[stop_index];
                assert(same_coordinate(
                    route.route_coordinates[position + 1],
                    stops[stop_index]
                ));
            }

            assert(nearly_equal(
                route.distance_km,
                route_distance(depot, stops, route.stop_order)
            ));
            assert_two_opt_local_optimum(depot, stops, route.stop_order);
            total_distance += route.distance_km;
        }

        for (const int count : visit_count) {
            assert(count == 1);
        }
        assert(nearly_equal(result.total_distance_km, total_distance));
    }
}

void test_maximum_stop_count() {
    const Coordinate depot{0.0, 0.0};
    std::vector<Coordinate> stops(100, {1.0, 1.0});

    const auto result = optiroute::optimize_routes(depot, stops, 1);
    assert(result.routes.front().stop_order.size() == 100);

    stops.push_back({1.0, 1.0});
    assert_invalid_argument([&] {
        optiroute::optimize_routes(depot, stops, 1);
    });
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
    assert_invalid_argument([&] {
        optiroute::optimize_routes({-91.0, 0.0}, one_stop, 1);
    });
    assert_invalid_argument([&] {
        optiroute::optimize_routes(depot, {{0.0, -181.0}}, 1);
    });
    assert_invalid_argument([&] {
        optiroute::optimize_routes(
            {std::numeric_limits<double>::quiet_NaN(), 0.0},
            one_stop,
            1
        );
    });
    assert_invalid_argument([&] {
        optiroute::optimize_routes(
            depot,
            {{std::numeric_limits<double>::infinity(), 0.0}},
            1
        );
    });
}

}  // namespace

int main() {
    test_haversine_distance();
    test_result_contract_and_assignment();
    test_single_stop_closed_route();
    test_two_opt_improves_greedy_route();
    test_duplicate_stops_are_preserved();
    test_unused_vehicle_has_depot_only_route();
    test_is_deterministic();
    test_randomized_result_invariants();
    test_maximum_stop_count();
    test_validation();
    std::cout << "All optimizer tests passed.\n";
    return 0;
}
