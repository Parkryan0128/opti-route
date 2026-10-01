"""Verify the real Gunicorn -> Redis -> Celery -> compiled C++ path over HTTPS."""
import http.client
import json
import re
import socket
import ssl
import subprocess
import sys
import time

TLS = ssl.create_default_context(cafile=sys.argv[1])
HOST = "optiroute.ryanparkdev.com"
COMPOSE = ["docker", "compose", "--env-file", "deploy/.env.production", "-f", "deploy/compose.yml"]


class LocalConnection(http.client.HTTPSConnection):
    def connect(self):
        self.sock = TLS.wrap_socket(
            socket.create_connection(("127.0.0.1", 8443), timeout=10),
            server_hostname=HOST,
        )


def request(path, data=None, expected=200):
    connection = LocalConnection(HOST, 8443, context=TLS, timeout=10)
    method = "GET" if data is None else "POST"
    connection.request(
        method, path, None if data is None else json.dumps(data),
        headers={"Content-Type": "application/json"},
    )
    response = connection.getresponse()
    body = response.read()
    status = response.status
    content_type = response.getheader("Content-Type", "")
    connection.close()
    assert status == expected, f"{method} {path}: expected {expected}, got {status}: {body[:300]!r}"
    return json.loads(body) if "json" in content_type else body.decode()


for attempt in range(30):
    try:
        assert request("/health/")["status"] == "UP"
        break
    except (OSError, AssertionError):
        if attempt == 29:
            raise
        time.sleep(1)

html = request("/")
assert "OptiRoute" in html and 'data-maps-configured="true"' in html
assets = re.findall(r'(?:src|href)="(/static/[^"]+)"', html)
assert len(assets) >= 2, "Missing static CSS/JavaScript assets"
for asset in assets:
    assert request(asset), f"Empty static asset: {asset}"

payload = {
    "depot": {"lat": 49.28, "lng": -123.12},
    "stops": [
        {"lat": 49.29, "lng": -123.11},
        {"lat": 49.27, "lng": -123.10},
        {"lat": 49.26, "lng": -123.13},
    ],
    "num_vehicles": 2,
}
task_id = request("/api/v1/optimize/", payload, 202)["task_id"]
for attempt in range(60):
    result = request(f"/api/v1/optimize/{task_id}/")
    assert result["status"] != "FAILED", result
    if result["status"] == "SUCCESS":
        break
    if attempt == 59:
        raise AssertionError("The real worker did not complete the task")
    time.sleep(1)
routes = result["result"]["routes"]
assert len(routes) == 2
assert sorted(stop for route in routes for stop in route["stop_order"]) == [0, 1, 2]
assert result["result"]["total_distance_km"] > 0
ttl = int(subprocess.check_output(COMPOSE + ["exec", "-T", "redis", "redis-cli", "TTL", f"task:{task_id}"]))
assert 0 < ttl <= 86400, f"Task results must expire: TTL={ttl}"
request("/api/v1/optimize/", {**payload, "num_vehicles": 101}, 400)
subprocess.run(["docker", "stats", "--no-stream"], check=True)

# Check real queue admission without allowing a worker to consume these disposable fixtures.
subprocess.run(COMPOSE + ["stop", "worker"], check=True)
subprocess.run(
    COMPOSE + ["exec", "-T", "redis", "redis-cli", "LPUSH", "celery"] + ["ci-queue-fixture"] * 20,
    check=True, stdout=subprocess.DEVNULL,
)
assert "queue" in request("/api/v1/optimize/", payload, 429)["error_message"]
print("Public HTTPS acceptance passed: static assets, real Celery/C++ computation, task TTL, and queue admission.")
