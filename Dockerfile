FROM python:3.11-slim-bookworm AS development

RUN apt-get update && apt-get install -y --no-install-recommends \
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

ENV PYTHONPATH=/app/build/engine

FROM development AS assets
RUN APP_ENV=production SECRET_KEY=build-only-placeholder-not-a-runtime-secret-000000000000000000 \
    python manage.py collectstatic --noinput

FROM python:3.11-slim-bookworm AS runtime
LABEL org.opencontainers.image.source="https://github.com/Parkryan0128/opti-route"
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system app && useradd --system --gid app app
WORKDIR /app/backend
COPY --from=assets /usr/local/lib/python3.11/site-packages /usr/local/lib/python3.11/site-packages
COPY --from=assets /usr/local/bin/gunicorn /usr/local/bin/celery /usr/local/bin/
COPY --from=assets /app/backend /app/backend
COPY --from=assets /app/frontend /app/frontend
COPY --from=assets /app/build/engine/optiroute_cpp*.so /app/build/engine/
ENV PYTHONPATH=/app/build/engine PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1
USER app
EXPOSE 8000
CMD ["gunicorn", "optiroute_config.wsgi:application", "--bind", "0.0.0.0:8000", "--workers", "1", "--threads", "2", "--timeout", "30", "--access-logfile", "-"]
