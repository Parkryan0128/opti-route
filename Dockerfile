FROM python:3.11-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    cmake \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY backend/requirements.txt ./backend/requirements.txt
RUN pip install --no-cache-dir -r backend/requirements.txt

COPY backend/ ./backend/
COPY engine/ ./engine/
COPY frontend/ ./frontend/

WORKDIR /app/backend

# C++ engine build (Milestone 2):
#   docker compose exec web bash -c "cmake -B /app/engine/build /app/engine && cmake --build /app/engine/build"
# After rebuilding C++ code, restart the worker: docker compose restart worker

ENV PYTHONPATH=/app/engine/build:${PYTHONPATH}
