#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -f deploy/.env.production ]]; then
  echo 'Keeping existing deploy/.env.production.'
  exit 0
fi
maps_key=${GOOGLE_MAPS_API_KEY:-}
if [[ -z "$maps_key" ]]; then
  read -r -s -p 'Google Maps browser API key: ' maps_key </dev/tty
  printf '\n'
fi
if [[ ! "$maps_key" =~ ^[A-Za-z0-9_-]{20,}$ ]]; then
  echo 'Enter the browser API key without spaces or quotes.' >&2
  exit 1
fi
secret=$(openssl rand -hex 32)
revision=$(git rev-parse HEAD)
umask 077
env_temp=$(mktemp deploy/.env.production.XXXXXX)
trap 'rm -f "$env_temp"' EXIT
{
  printf 'DEMO_HOST=optiroute.ryanparkdev.com\n'
  printf 'APP_IMAGE=ghcr.io/parkryan0128/opti-route:sha-%s\n' "$revision"
  printf 'SECRET_KEY=%s\nGOOGLE_MAPS_API_KEY=%s\n' "$secret" "$maps_key"
} > "$env_temp"
ln "$env_temp" deploy/.env.production
echo 'Created private deployment settings.'
