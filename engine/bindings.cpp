#include "optimizer.h"

#include <cstddef>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace {

bool is_python_number(const py::handle& value) {
    return !py::isinstance<py::bool_>(value) &&
           (
               py::isinstance<py::int_>(value) ||
               py::isinstance<py::float_>(value)
           );
}

optiroute::Coordinate coordinate_from_dict(
    const py::dict& value,
    const std::string& field_name
) {
    if (!value.contains("lat") || !value.contains("lng")) {
        throw py::value_error(
            field_name + " must contain numeric 'lat' and 'lng' fields"
        );
    }

    const py::handle latitude = value["lat"];
    const py::handle longitude = value["lng"];
    if (!is_python_number(latitude) || !is_python_number(longitude)) {
        throw py::value_error(
            field_name + " must contain numeric 'lat' and 'lng' fields"
        );
    }

    try {
        return {
            py::cast<double>(latitude),
            py::cast<double>(longitude),
        };
    } catch (const py::cast_error&) {
        throw py::value_error(
            field_name + " must contain numeric 'lat' and 'lng' fields"
        );
    }
}

std::vector<optiroute::Coordinate> stops_from_list(const py::list& values) {
    std::vector<optiroute::Coordinate> stops;
    stops.reserve(values.size());

    for (std::size_t index = 0; index < values.size(); ++index) {
        py::handle value = values[index];
        if (!py::isinstance<py::dict>(value)) {
            throw py::value_error(
                "stops[" + std::to_string(index) + "] must be an object"
            );
        }
        stops.push_back(coordinate_from_dict(
            py::reinterpret_borrow<py::dict>(value),
            "stops[" + std::to_string(index) + "]"
        ));
    }

    return stops;
}

py::dict coordinate_to_dict(const optiroute::Coordinate& coordinate) {
    py::dict value;
    value["lat"] = coordinate.lat;
    value["lng"] = coordinate.lng;
    return value;
}

py::dict result_to_dict(const optiroute::OptimizationResult& result) {
    py::list routes;

    for (const auto& route : result.routes) {
        py::list route_coordinates;
        for (const auto& coordinate : route.route_coordinates) {
            route_coordinates.append(coordinate_to_dict(coordinate));
        }

        py::dict route_value;
        route_value["vehicle_id"] = route.vehicle_id;
        route_value["stop_order"] = route.stop_order;
        route_value["route_coordinates"] = std::move(route_coordinates);
        route_value["distance_km"] = route.distance_km;
        routes.append(std::move(route_value));
    }

    py::dict value;
    value["routes"] = std::move(routes);
    value["total_distance_km"] = result.total_distance_km;
    return value;
}

py::dict optimize_routes(
    const py::dict& depot_value,
    const py::list& stop_values,
    const py::object& num_vehicles_value
) {
    if (
        py::isinstance<py::bool_>(num_vehicles_value) ||
        !py::isinstance<py::int_>(num_vehicles_value)
    ) {
        throw py::value_error("num_vehicles must be an integer");
    }

    const auto depot = coordinate_from_dict(depot_value, "depot");
    const auto stops = stops_from_list(stop_values);
    const int num_vehicles = py::cast<int>(num_vehicles_value);

    // The optimizer is CPU-bound and does not touch Python objects. Releasing
    // the GIL lets other Python threads continue while C++ performs the work.
    optiroute::OptimizationResult result;
    {
        py::gil_scoped_release release;
        result = optiroute::optimize_routes(depot, stops, num_vehicles);
    }

    return result_to_dict(result);
}

}  // namespace

PYBIND11_MODULE(optiroute_cpp, module) {
    module.doc() = "C++ vehicle routing optimizer for OptiRoute";
    module.def(
        "optimize_routes",
        &optimize_routes,
        py::arg("depot"),
        py::arg("stops"),
        py::arg("num_vehicles"),
        R"doc(
Optimize closed vehicle routes from a single depot.

Args:
    depot: Mapping with numeric ``lat`` and ``lng`` fields.
    stops: List of mappings with numeric ``lat`` and ``lng`` fields.
    num_vehicles: Number of available vehicles.

Returns:
    A dictionary containing ``routes`` and ``total_distance_km``.

Raises:
    ValueError: If an input is malformed or violates optimizer constraints.
)doc"
    );
}
