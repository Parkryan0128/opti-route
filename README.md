# OptiRoute — Hybrid VRP Optimizer

[![Language](https://img.shields.io/badge/Language-C%2B%2B17%20%2B%20Python-blue.svg)]()
[![Stack](https://img.shields.io/badge/Stack-Django%20%7C%20Celery%20%7C%20Redis-green.svg)]()
[![Build](https://img.shields.io/badge/Build-CMake%20%2B%20Docker-lightgrey.svg)]()

A high-performance **Vehicle Routing Problem (VRP)** web service that combines a **C++ optimization engine** with an asynchronous **Django / Celery / Redis** backend and a **Google Maps** planner UI. The engine balances multi-vehicle closed routes using Haversine distance, local search, and deterministic Simulated Annealing; the frontend then draws road-following geometry without changing the optimized stop order.

**Live Demo:** [https://optiroute.ryanparkdev.com](https://optiroute.ryanparkdev.com)

***

## 📋 Table of Contents

* [Key Features](#-key-features)
* [Project Structure](#-project-structure)
* [How to Build and Run](#️-how-to-build-and-run)
* [How It Works (Architecture)](#️-how-it-works-architecture)
* [API Contract](#-api-contract)
* [Testing](#-testing)
* [Limitations](#limitations)
* [Contact](#-contact)

***

## ✨ Key Features

* **C++ VRP Engine:** Haversine distance matrix, farthest-point seeding, greedy assignment, 2-opt, cross-route relocate/swap search, and fixed-seed Simulated Annealing.
* **Workload Balancing:** Primary objective minimizes the longest vehicle route (`max_distance_km`); total distance is the tie-breaker. Every requested vehicle gets at least one stop.
* **pybind11 Binding:** The engine is compiled as `optiroute_cpp` and called directly from the Celery worker.
* **Async Job Pipeline:** `POST` returns a `task_id` immediately; clients poll Redis-backed task status (`PENDING` → `PROCESSING` → `SUCCESS` / `FAILED`).
* **No ORM Required:** Task state lives in Redis JSON (`task:{id}`); Django uses a dummy database backend.
* **Interactive Map UI:** Click to place a depot and stops, tune vehicle count, poll for results, and toggle per-vehicle road routes.
* **Optimization vs Visualization Split:** C++ owns stop order via Haversine; Google Routes only renders road polylines.
* **Docker Compose Ready:** One-command local stack with `web`, `worker`, and `redis`.

***

## 📁 Project Structure

```text
.
├── backend/
│   ├── api/                    # DRF views, Celery task, Redis task store, tests
│   ├── optiroute_config/       # Django settings, URLs, Celery app
│   ├── manage.py
│   └── requirements.txt
├── engine/
│   ├── optimizer.h             # Public C++ API
│   ├── optimizer.cpp           # Orchestration
│   ├── optimizer_internal.h    # Shared internal types / helpers
│   ├── distance.cpp            # Haversine + DistanceMatrix
│   ├── construction.cpp        # Initial solution construction
│   ├── local_search.cpp        # 2-opt + cross-route search
│   ├── annealing.cpp           # Simulated Annealing
│   ├── bindings.cpp            # pybind11 module
│   ├── CMakeLists.txt
│   └── tests/                  # Native C++, binding, and full-stack tests
├── frontend/
│   ├── index.html
│   ├── css/style.css
│   └── js/app.js
├── Dockerfile
├── docker-compose.yml
├── .env.example
└── README.md
```

***

## ⚙️ How to Build and Run

### 1. Requirements

* **Python** 3.10+
* **CMake** 3.18+ and a **C++17** compiler
* **Redis**
* **Docker** (optional, recommended)
* **Google Maps API key** with Maps JavaScript API and Routes API enabled

### 2. Configure Environment

```bash
cp .env.example .env
```

Fill in:

| Variable | Purpose |
|----------|---------|
| `SECRET_KEY` | Django secret |
| `DEBUG` | `True` for local static serving |
| `ALLOWED_HOSTS` | Hostnames / IPs allowed by Django |
| `REDIS_URL` | e.g. `redis://localhost:6379/0` or `redis://redis:6379/0` in Compose |
| `GOOGLE_MAPS_API_KEY` | Browser key (restrict by HTTP referrer) |

### 3. Docker (recommended)

```bash
docker compose up --build
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000).

* Django / frontend edits reload via volume mounts.
* After C++ changes: `docker compose restart web worker` (each container rebuilds `optiroute_cpp` on start).

### 4. Local Build (without Docker app stack)

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r backend/requirements.txt

cmake -S engine -B build/engine -DCMAKE_BUILD_TYPE=Release
cmake --build build/engine --parallel
export PYTHONPATH="$(pwd)/build/engine:${PYTHONPATH}"
```

Run Redis, then in separate terminals:

```bash
# API
cd backend && python manage.py runserver

# Worker
cd backend && celery -A optiroute_config worker --loglevel=info
```

### 5. Demo Flow

1. Open the planner UI.
2. Click the map to place a **depot** (`D`), then add delivery **stops**.
3. Choose the number of vehicles and click **Start**.
4. Wait while the UI polls task status.
5. Inspect colored road routes and toggle vehicles in the route list.

***

## 🏗️ How It Works (Architecture)

```text
Browser (Maps UI)
       │
       │ POST /api/v1/optimize/   → task_id
       │ GET  /api/v1/optimize/<id>/  (poll)
       ▼
┌──────────────┐     enqueue      ┌────────────────┐
│  Django API  │ ───────────────► │ Celery Worker  │
└──────┬───────┘                  └───────┬────────┘
       │                                  │
       │ task:{id} JSON                   │ import optiroute_cpp
       ▼                                  ▼
     Redis                         C++ VRP Engine
                                   (Haversine + SA)
       ▲
       │ SUCCESS / FAILED result
       └──────────────────────────────────┘

After SUCCESS, the browser calls Google Routes
(with waypoint reordering disabled) to draw roads.
```

### 1. Optimization Engine (`engine/`)

1. Validate depot, stops, and vehicle count.
2. Build **one** Haversine `DistanceMatrix`.
3. Construct an initial feasible assignment (seed every vehicle, then insert remaining stops).
4. Improve with **2-opt** and **cross-route** relocate/swap search.
5. Run **deterministic Simulated Annealing** (fixed seed) to escape local optima.
6. Re-apply local search and return ordered routes + Haversine distances.

### 2. Backend (`backend/`)

* **API:** validates input, creates a Redis task, enqueues Celery, returns `202`.
* **Worker:** marks `PROCESSING`, calls C++, writes `SUCCESS` / `FAILED`.
* **Task store:** Redis-only persistence; Celery results are ignored (`ignore_result=True`).

### 3. Frontend (`frontend/`)

* Map click handling for depot/stops, session restore, async polling, and per-vehicle road polyline toggles.
* Browser planner is capped at **10 stops / 10 vehicles** to limit Google Routes usage (API allows up to 100).

***

## 📡 API Contract

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

**Response (`202 Accepted`):**

```json
{"task_id": "550e8400-e29b-41d4-a716-446655440000"}
```

**GET `/api/v1/optimize/<task_id>/`**

| Status | Body |
|--------|------|
| `PENDING` / `PROCESSING` | `{"status": "PENDING"}` |
| `SUCCESS` | `{"status": "SUCCESS", "result": { "routes": [...], "total_distance_km": ..., "max_distance_km": ... }}` |
| `FAILED` | `{"status": "FAILED", "error_message": "..."}` |

Synchronous errors use `error_message` with `400` (validation), `404` (unknown task), or `503` (service unavailable).

***

## 🧪 Testing

### Backend

```bash
cd backend && python manage.py test api optiroute_config
```

Uses Django’s dummy database backend (no SQLite file is created).

### Engine (native C++, bindings, full-stack)

```bash
cmake -S engine -B build/engine -DCMAKE_BUILD_TYPE=Release
cmake --build build/engine --parallel
ctest --test-dir build/engine --output-on-failure
```

| Suite | What it covers |
|-------|----------------|
| `optimizer_cpp_tests` | Haversine, construction, local optima, golden routes, annealing determinism, edge cases |
| `optimizer_python_binding_tests` | pybind11 contract |
| `optiroute_full_stack_tests` | API → Celery task → C++ engine → poll response |

### Optional Sanitizers

```bash
cmake -S engine -B build/engine-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build/engine-sanitize --parallel
ctest --test-dir build/engine-sanitize --output-on-failure
```

***
<a id="limitations"></a>
## ⚠️ Limitations

* **Haversine optimization** — road barriers, one-ways, and traffic do not affect stop ordering.
* **Heuristic, not exact** — solutions are approximate; global optimality is not guaranteed.
* **Google Maps billing** — road visualization uses the Routes API after optimization.
* **Demo-oriented deploy** — current Compose stack uses Django’s development server; production hardening (gunicorn, `DEBUG=False` static serving, closed Redis port) is optional follow-up work.

***

## 📧 Contact

- **Name:** Ryan Park
- **Email:** [parkryan0128@gmail.com](mailto:parkryan0128@gmail.com)
- **LinkedIn:** [https://www.linkedin.com/in/parkryan0128](https://www.linkedin.com/in/parkryan0128)
- **GitHub:** [https://github.com/Parkryan0128](https://github.com/Parkryan0128)
