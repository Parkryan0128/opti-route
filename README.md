# OptiRoute: Hybrid Vehicle Routing Problem (VRP) Optimizer

## 1. Project Overview

OptiRoute is a high-performance, asynchronous web service designed to solve the Vehicle Routing Problem (VRP). Designed primarily as a robust portfolio demonstration, it showcases the ability to integrate high-speed C++ algorithmic calculations with a Python (Django) backend and asynchronous task management.

**VRP Variant for MVP:**

* **Single Depot:** The first location selected is the start and end point for all vehicles.
* **Closed Routes:** All vehicles must return to the depot.
* **Uncapacitated:** Vehicle capacity constraints are ignored for the MVP.
* **Objective:** Minimize the total overall distance across all routes.
* **Distance Metric:** Haversine formula (straight-line distance on a sphere) for MVP, to be replaced by real road distances (OSRM/Google Distance Matrix) in future iterations.

## 2. Tech Stack & Architecture

* **Core Engine (Algorithm):** C++17 or higher
* **Python Binding:** pybind11
* **Backend Framework:** Python 3.10+, Django 4.x, Django REST Framework (DRF)
* **Asynchronous Task Queue:** Celery
* **Message Broker & Task Storage:** Redis (Celery job queue + task state persistence)
* **Frontend:** HTML5, CSS3, Vanilla JavaScript, Google Maps API
* **Infrastructure:** Docker & Docker Compose on a single host (e.g., one DigitalOcean Droplet)

### System Workflow

1. **Client** submits depot, stops, and `num_vehicles` via `POST /api/v1/optimize/`.
2. **Django API** writes a `PENDING` task record to Redis, enqueues a Celery job, and returns `task_id` (`202 Accepted`).
3. **Celery Worker** picks up the job from Redis, sets status to `PROCESSING`, and calls the C++ engine via pybind11.
4. **C++ Engine** runs the VRP optimizer and returns ordered routes.
5. **Celery Worker** writes the result (or error) back to Redis as `SUCCESS` / `FAILED`.
6. **Client** polls `GET /api/v1/optimize/<task_id>/` until the task completes, then draws routes on the map.

### Redis Task Storage

Redis serves two roles: Celery message broker and task data store. Each task is stored as a JSON blob:

```
Key:   task:{task_id}
Value: {
  "status": "SUCCESS",
  "input_data": {"depot": {...}, "stops": [...], "num_vehicles": 2},
  "result_data": {"routes": [...], "total_distance_km": 10.0},
  "error_message": null,
  "created_at": "2026-07-13T22:00:00Z"
}
```

Redis runs with `appendonly yes` so task data survives container restarts. No PostgreSQL or Django ORM models are required for task persistence.

```
DigitalOcean Droplet (Docker Compose)
├── web      (Django API + frontend)
├── worker   (Celery)
└── redis    (Celery broker + task store)
```

## 3. API Contract (Data Schema)

To ensure alignment between the frontend, backend, and C++ engine, the following JSON schema is strictly enforced.

**POST `/api/v1/optimize/` (Request)**

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

**GET `/api/v1/optimize/<task_id>/` (Response for PENDING or PROCESSING)**

```json
{"status": "PENDING"}
```

```json
{"status": "PROCESSING"}
```

**GET `/api/v1/optimize/<task_id>/` (Response for SUCCESS)**

```json
{
  "status": "SUCCESS",
  "result": {
    "routes": [
      {
        "vehicle_id": 1,
        "stop_order": [0],
        "route_coordinates": [
          {"lat": 37.77, "lng": -122.42},
          {"lat": 37.78, "lng": -122.43},
          {"lat": 37.77, "lng": -122.42}
        ],
        "distance_km": 5.2
      },
      {
        "vehicle_id": 2,
        "stop_order": [1],
        "route_coordinates": [
          {"lat": 37.77, "lng": -122.42},
          {"lat": 37.79, "lng": -122.41},
          {"lat": 37.77, "lng": -122.42}
        ],
        "distance_km": 4.8
      }
    ],
    "total_distance_km": 10.0
  }
}
```

**GET `/api/v1/optimize/<task_id>/` (Response for FAILED)**

```json
{
  "status": "FAILED",
  "error_message": "C++ engine raised an exception during optimization"
}
```

**Error Responses**

| Endpoint | Status | Body |
|----------|--------|------|
| `POST /api/v1/optimize/` | `400 Bad Request` | `{"error_message": "num_vehicles cannot exceed number of stops"}` |
| `GET /api/v1/optimize/<task_id>/` | `404 Not Found` | `{"error_message": "Task not found"}` |

> **Note:** `status` can be `PENDING`, `PROCESSING`, `SUCCESS`, or `FAILED`. Synchronous validation errors (POST `400`, GET `404`) use `error_message`. Async task failures (GET `FAILED`) also use `error_message`.

### Field Definitions

* **`vehicle_id`:** 1-based integer identifier for each vehicle (e.g., `1`, `2`, …, `num_vehicles`).
* **`stop_order`:** A 0-based index into the `stops` array (depot excluded). For example, if `stops = [A, B, C]` and a vehicle visits A then C, its `stop_order` is `[0, 2]`. The depot is implicit and appears only in `route_coordinates`.
* **`route_coordinates`:** The full ordered path for a vehicle, including the depot at the start and end to form a closed loop (Depot → Stops → Depot).
* **`distance_km`:** Total Haversine distance for that vehicle's closed route, in kilometers.
* **`total_distance_km`:** Sum of `distance_km` across all vehicle routes.

### Input Validation Rules

| Case | Rule |
|------|------|
| `num_vehicles < 1` | Reject with `400 Bad Request` |
| `num_stops < 1` | Reject with `400 Bad Request` |
| `num_vehicles > num_stops` | Reject with `400 Bad Request` |
| `num_stops > 100` | Reject with `400 Bad Request` |
| `num_vehicles > 100` | Reject with `400 Bad Request` |
| Missing or invalid `depot` | Reject with `400 Bad Request` |
| Invalid lat/lng (out of range) | Reject with `400 Bad Request` |
| Duplicate coordinates | Allow silently |

---

## 4. Development Milestones & Task Steps

### 🟢 Milestone 1: Project Initialization & Environment Setup

**Goal:** Set up the unified repository structure, Docker environment, and configuration files.

* [ ] **Step 1.1: Repository Structure & Git Ignore**
    * Create a monorepo: `backend/`, `engine/`, `frontend/`.
    * Add a comprehensive `.gitignore` for Python, C++ build artifacts, and environment files.
    * Create `.env.example` with the following keys:
        * `SECRET_KEY` — Django secret key
        * `REDIS_URL` — Redis connection string (e.g., `redis://redis:6379/0`)
        * `GOOGLE_MAPS_API_KEY` — Google Maps JavaScript API key
* [ ] **Step 1.2: Docker Compose Configuration**
    * Create `docker-compose.yml` with three services: `web` (Django), `worker` (Celery), `redis`.
    * Configure Redis with `appendonly yes` and a named volume for data persistence.
    * Write a `Dockerfile` that installs C++ build tools (`g++`, `cmake`) and Python dependencies.
    * Define the dev workflow: mount the source code as volumes so Django autoreloads, but clearly state how C++ recompilation will be triggered.
* [ ] **Step 1.3: Django Initialization**
    * Initialize Django project (`optiroute_config`) and core app (`api`) under `backend/`.
    * Install DRF, Celery, and `redis` Python client (`requirements.txt`).
    * Configure static files to serve the `frontend/` directory at `/`.

### 🔵 Milestone 2: C++ Optimization Engine & Python Binding

**Goal:** Write the VRP algorithm in C++ and compile it as a Python-callable module using pybind11.

* [x] **Step 2.1: C++ VRP Algorithm (Nearest-Neighbor + 2-opt)**
    * Implement a sequential greedy assignment: repeatedly pick the unassigned stop that is globally nearest to any vehicle's current route endpoint, and assign it to that vehicle (all vehicles start at the depot). Continue until all stops are assigned.
    * After assignment, apply a 2-opt local search heuristic to optimize each vehicle's route independently.
    * Input: Depot coords, Stops coords, Num Vehicles. Output: Ordered routes matching the API contract.
* [ ] **Step 2.2: pybind11 Integration**
    * Write `engine/bindings.cpp` to expose the C++ function to Python, converting STL vectors/structs to Python dicts/lists.
* [ ] **Step 2.3: CMake Build System**
    * Configure `CMakeLists.txt` to compile the engine into a `.so` module callable via `import optiroute_cpp`.

### 🟡 Milestone 3: Django API & Celery Integration

**Goal:** Build REST API endpoints and manage state transitions via Celery.

* [ ] **Step 3.1: Redis Task Store**
    * Create `api/task_store.py` — a thin wrapper around the Redis client with `create_task`, `get_task`, and `update_task` methods.
    * Each task is stored at `task:{task_id}` as JSON with fields: `status`, `input_data`, `result_data`, `error_message`, `created_at`.
* [ ] **Step 3.2: Celery Task Lifecycle**
    * Write `@shared_task` in `api/tasks.py` that updates Redis state: `PENDING` → `PROCESSING` → Calls C++ Engine → `SUCCESS` (or `FAILED` with error payload).
* [ ] **Step 3.3: REST API Endpoints (DRF)**
    * Implement POST and GET endpoints strictly following the defined API Contract.
    * POST returns `202 Accepted` with `task_id` on success; validation failures return `400` with `error_message`.
    * GET returns `PENDING`/`PROCESSING`/`SUCCESS`/`FAILED` payloads per Section 3; unknown `task_id` returns `404` with `error_message`.

### 🟠 Milestone 4: Frontend Visualization (MVP)

**Goal:** Create an interactive UI using Vanilla JS and Google Maps.

* [ ] **Step 4.1: UI & Map Initialization**
    * Load Google Maps. Add inputs for "Number of Vehicles" and a "Start" button.
* [ ] **Step 4.2: Depot and Stops Logic**
    * The first click on the map drops a distinct "Depot" marker (e.g., a star or different color). Subsequent clicks drop regular "Stop" markers.
    * Disable the "Start" button until a depot marker and at least one stop marker have been placed.
* [ ] **Step 4.3: Async Polling Mechanism**
    * On Start, send POST request. Poll the GET endpoint every 2 seconds.
    * Implement a client-side timeout: if `PROCESSING` persists for > 30 seconds, stop polling and alert the user. The backend task continues running; the user can refresh or poll again later using the same `task_id`.
* [ ] **Step 4.4: Result Visualization**
    * On `SUCCESS`, draw closed-loop Polylines (Depot → Stops → Depot) for each vehicle using distinct colors.

### 🔴 Milestone 5: Production Readiness (Optional/Future)

**Goal:** Polish the application for potential B2B deployment or advanced portfolio showcasing.

* [ ] **Road Distance API:** Swap Haversine distance with real routing metrics (e.g., OSRM).
* [ ] **Advanced Algorithm:** Upgrade C++ core to Simulated Annealing or Genetic Algorithm.
* [ ] **Testing Suite:** Add C++ unit tests (GTest), DRF API tests, and Celery integration tests.
* [ ] **Security:** Implement rate limiting, Authentication, and proper CORS settings.
* [ ] **Persistent Database (optional):** Migrate task storage from Redis to PostgreSQL if long-term task history or analytics are needed.
