# OptiRoute

OptiRoute is a multi-vehicle route planner built with C++, Django, Celery, Redis, and Google Maps.

The solver assigns each stop to a vehicle and determines the visit order. It primarily minimizes the longest vehicle route to keep the workload balanced, using total distance as a tie-breaker.

[Live demo](https://optiroute.ryanparkdev.com)

## How it works

```text
Browser
   │
   │ POST optimization request
   ▼
Django API
   │
   ├── Saves task data in Redis
   └── Adds a Celery job to the Redis queue
                               │
                               ▼
                         Celery worker
                               │
                               ▼
                         C++ optimizer
                               │
                               ▼
                        Result in Redis
                               │
   Browser polls Django ◄──────┘
```

The C++ solver:

1. Calculates a Haversine distance matrix.
2. Builds an initial route assignment.
3. Improves each route with 2-opt.
4. Moves and swaps stops between vehicles.
5. Uses simulated annealing to escape local optima.
6. Runs local search again and returns the best result found.

The optimizer uses straight-line Haversine distances. Google Routes is only used to draw the final stop order on real roads.

## Project structure

```text
backend/    Django API, Celery tasks, and Redis task storage
engine/     C++ route optimizer and Python bindings
frontend/   Google Maps interface
```

The C++ engine is exposed to Python with pybind11.

## Run locally

Create the environment file:

```bash
cp .env.example .env
```

Add a Django secret and a Google Maps API key to `.env`, then run:

```bash
docker compose up --build
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000).

After changing C++ code, rebuild the web and worker services:

```bash
docker compose restart web worker
```

## API

Start an optimization:

```http
POST /api/v1/optimize/
```

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

The API returns a task ID:

```json
{"task_id": "550e8400-e29b-41d4-a716-446655440000"}
```

Check the task:

```http
GET /api/v1/optimize/<task_id>/
```

A task can be `PENDING`, `PROCESSING`, `SUCCESS`, or `FAILED`.

## Tests

Backend tests:

```bash
cd backend
python manage.py test api optiroute_config
```

Engine and integration tests:

```bash
cmake -S engine -B build/engine -DCMAKE_BUILD_TYPE=Release
cmake --build build/engine --parallel
ctest --test-dir build/engine --output-on-failure
```

## Contact

- **Name:** Ryan Park
- **Email:** [parkryan0128@gmail.com](mailto:parkryan0128@gmail.com)
- **LinkedIn:** [https://www.linkedin.com/in/parkryan0128](https://www.linkedin.com/in/parkryan0128)
- **GitHub:** [https://github.com/Parkryan0128](https://github.com/Parkryan0128)
