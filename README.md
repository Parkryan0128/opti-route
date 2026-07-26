# OptiRoute: Hybrid Vehicle Routing Problem (VRP) Optimizer

**Status: Project complete.** OptiRoute is a portfolio demonstration of a high-performance asynchronous VRP service: C++ optimization, Django/DRF + Celery + Redis, and a Google Maps frontend.

## VRP Variant

* **Single Depot:** The first location selected is the start and end point for all vehicles.
* **Closed Routes:** All vehicles must return to the depot.
* **Uncapacitated:** Vehicle capacity constraints are ignored.
* **Vehicle Usage:** Every requested vehicle must receive at least one stop.
* **Objective:** Primarily minimize the longest vehicle route to balance workload; use total Haversine distance as a secondary tie-breaker. The optimizer does not guarantee a globally optimal solution.
* **Optimization Distance Metric:** Haversine (straight-line on a sphere). Road conditions, barriers, and traffic do not influence stop ordering.
* **Road Visualization:** After optimization, the Google Maps Routes Library draws road-following geometry while preserving the C++ engine's stop order.

## Architecture

* **Core Engine:** C++17 — Haversine matrix, greedy construction, 2-opt, cross-route local search, deterministic Simulated Annealing
* **Python Binding:** pybind11 (`optiroute_cpp`)
* **Backend:** Django + Django REST Framework, Celery worker (`ignore_result=True`; task state lives in Redis via `task_store`)
* **Broker & Task Store:** Redis (Celery queue + `task:{id}` JSON status)
* **Frontend:** HTML/CSS/Vanilla JS, Google Maps JavaScript API + Routes Library
* **Infrastructure:** Docker Compose (`web`, `worker`, `redis`)

### System Workflow

1. Client submits depot, stops, and `num_vehicles` via `POST /api/v1/optimize/`.
2. Django writes a `PENDING` task to Redis, enqueues Celery, returns `task_id` (`202`).
3. Worker sets `PROCESSING` and calls the C++ engine via pybind11.
4. Engine builds one Haversine distance matrix, constructs an initial solution, improves locally, runs fixed-seed Simulated Annealing, then reapplies local search.
5. Worker writes `SUCCESS` / `FAILED` to Redis.
6. Client polls `GET /api/v1/optimize/<task_id>/` until complete.
7. Frontend requests road polylines from Google Routes without reordering waypoints.

```
Docker Compose host
├── web      (Django API + frontend)
├── worker   (Celery)
└── redis    (broker + task store)
```

## Local Setup

```bash
cp .env.example .env   # set SECRET_KEY, REDIS_URL, GOOGLE_MAPS_API_KEY
python3 -m venv .venv
source .venv/bin/activate
pip install -r backend/requirements.txt
```

### Build the C++ engine (canonical path: `build/engine`)

```bash
cmake -S engine -B build/engine -DCMAKE_BUILD_TYPE=Release
cmake --build build/engine --parallel
export PYTHONPATH="$(pwd)/build/engine:${PYTHONPATH}"
```

### Run locally (Redis required)

```bash
# Terminal 1 — Redis (or: docker compose up redis)
redis-server

# Terminal 2 — API
cd backend && python manage.py runserver

# Terminal 3 — Worker
cd backend && celery -A optiroute_config worker --loglevel=info
```

Open `http://127.0.0.1:8000/`.

### Docker

```bash
docker compose up --build
```

Django/Python changes reload via volume mounts. After C++ edits, restart so containers rebuild the extension:

```bash
docker compose restart web worker
```

## Tests

```bash
# Backend (49 tests; uses Django's dummy DB — no SQLite file created)
cd backend && python manage.py test api optiroute_config

# Engine: native C++, Python bindings, full-stack
ctest --test-dir build/engine --output-on-failure

# Optional AddressSanitizer / UndefinedBehaviorSanitizer build
cmake -S engine -B build/engine-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build/engine-sanitize --parallel
ctest --test-dir build/engine-sanitize --output-on-failure
```

## API Contract

**POST `/api/v1/optimize/`**

```json
{
  "depot": {"lat": 37.77, "lng": -122.42},
  "stops": [
    {"lat": 37.78, "lng": -122.43},
    {"lat": 37.79, "lng": -122.41}
  ],
  "num_vehicles": 2
}
```

**Response (`202 Accepted`):** `{"task_id": "550e8400-e29b-41d4-a716-446655440000"}`

**GET `/api/v1/optimize/<task_id>/`**

| Status | Body |
|--------|------|
| `PENDING` / `PROCESSING` | `{"status": "PENDING"}` |
| `SUCCESS` | `{"status": "SUCCESS", "result": { "routes": [...], "total_distance_km": ..., "max_distance_km": ... }}` |
| `FAILED` | `{"status": "FAILED", "error_message": "..."}` |

Synchronous errors use `error_message` with `400` (validation), `404` (unknown task), or `503` (service unavailable).

### Field Definitions

* **`vehicle_id`:** 1-based vehicle identifier.
* **`stop_order`:** 0-based indexes into `stops` (depot excluded).
* **`route_coordinates`:** Closed Haversine waypoints (depot → stops → depot), not road geometry.
* **`distance_km` / `total_distance_km` / `max_distance_km`:** Haversine optimization scores in kilometers.

### Input Validation

| Case | Rule |
|------|------|
| `num_vehicles < 1` or `num_stops < 1` | `400` |
| `num_vehicles > num_stops` | `400` |
| `num_stops > 100` or `num_vehicles > 100` | `400` |
| Missing/invalid coordinates | `400` |

The API accepts up to 100 stops and vehicles. The browser planner is capped at 10 stops and 10 vehicles to limit Google Routes usage.

### Redis Task Record

```
Key:   task:{task_id}
Value: {
  "status": "SUCCESS",
  "input_data": {...},
  "result_data": {...},
  "error_message": null,
  "created_at": "2026-07-13T22:00:00Z"
}
```

Redis runs with `appendonly yes`. No Django ORM or Celery result backend is used for task polling.
