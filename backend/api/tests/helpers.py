class FakeRedis:
    def __init__(self):
        self.data = {}

    def set(self, key, value, nx=False):
        if nx and key in self.data:
            return False
        self.data[key] = value
        return True

    def get(self, key):
        return self.data.get(key)


def sample_input_data(*, two_stops=False):
    stops = [{"lat": 37.78, "lng": -122.43}]
    if two_stops:
        stops.append({"lat": 37.79, "lng": -122.41})
    return {
        "depot": {"lat": 37.77, "lng": -122.42},
        "stops": stops,
        "num_vehicles": len(stops),
    }


def empty_result():
    return {
        "routes": [],
        "total_distance_km": 0.0,
        "max_distance_km": 0.0,
    }
