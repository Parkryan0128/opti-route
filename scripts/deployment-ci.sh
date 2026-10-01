#!/usr/bin/env bash
# Disposable CI stack; never run against a production checkout.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${RUNNER_TEMP:?GitHub Actions only}"
GOOGLE_MAPS_API_KEY=ci-placeholder-browser-key bash deploy/init-env.sh
export APP_IMAGE=optiroute-ci:tested
app=(docker compose --env-file deploy/.env.production -f deploy/compose.yml)
"${app[@]}" config --quiet
docker network create portfolio-edge
"${app[@]}" up -d --wait --wait-timeout 240
cat > "$RUNNER_TEMP/Caddyfile.optiroute-ci" <<'EOF'
{
    local_certs
}
import /etc/caddy/sites/*.caddy
EOF
docker run -d --name optiroute-proxy-ci --network portfolio-edge \
  -p 127.0.0.1:8443:443 \
  -v "$RUNNER_TEMP/Caddyfile.optiroute-ci:/etc/caddy/Caddyfile:ro" \
  -v "$PWD/deploy/optiroute.caddy:/etc/caddy/sites/optiroute.caddy:ro" caddy:2-alpine
for attempt in $(seq 1 30); do
  if docker cp optiroute-proxy-ci:/data/caddy/pki/authorities/local/root.crt "$RUNNER_TEMP/optiroute-ca.crt" 2>/dev/null; then
    break
  fi
  sleep 1
done
python3 scripts/public-smoke.py "$RUNNER_TEMP/optiroute-ca.crt"
