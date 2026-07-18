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

RUN cmake -S /app/engine -B /app/build/engine \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
    && cmake --build /app/build/engine --target optiroute_cpp --parallel

WORKDIR /app/backend

ENV PYTHONPATH=/app/build/engine:${PYTHONPATH}
