# OVH deployment

OptiRoute runs as three containers: Gunicorn web (192 MiB), Celery worker (320 MiB, one task and one CPU at a time), and dedicated Redis (192 MiB). Redis uses AOF persistence and a 64 MiB data limit with no eviction; it is not shared with Inventory's evicting metadata cache.

The existing Inventory Caddy container owns HTTPS. Both web applications join `portfolio-edge`; Redis stays on OptiRoute's private network. No OptiRoute host ports are published.

## Initial setup

Wait for the main branch's Acceptance workflow to publish its tested image, then on the VPS:

```bash
cd ~/apps
git clone https://github.com/Parkryan0128/opti-route.git
cd opti-route
bash deploy/init-env.sh
bash deploy/up.sh
```

Enter the existing **browser** Google Maps API key at the hidden prompt. Keep its website restriction set to `https://optiroute.ryanparkdev.com/*`, and retain the APIs used by the current map and Routes JavaScript libraries. The key is supplied at runtime, never during image build. The script generates a new Django secret and saves both in the owner-only, Git-ignored `deploy/.env.production`.

This creates a fresh Redis volume. The old server is not modified. Previous task IDs/results are only available if its Redis data is deliberately migrated while its web and worker are stopped; do not copy a live AOF directory. There is no SQL database or uploaded-file store in this application.

The deployment script waits for Redis, web, and worker health, installs `optiroute.local.caddy` in the existing Inventory proxy's sites directory, validates/reloads Caddy, and checks the public HTTPS health endpoint. It never builds C++ on the VPS. A failed check returns an error and does not claim success or delete data.

## Automatic deployment

Each push to main runs Django tests, C++/binding tests, and the real production stack over HTTPS. Only a successful run publishes `ghcr.io/parkryan0128/opti-route:sha-<commit>`.

The separate Deploy workflow runs after successful main pushes, when repository variable `DEPLOY_ENABLED=true`. It can also be dispatched manually to deploy an already-tested main commit. Pull requests never deploy.

One-time VM key installation is provided by Inventory's `deploy/install-cd-keys.sh`. Each repository has a separate key restricted to its own deployment command. GitHub Secrets hold `DEPLOY_SSH_KEY` and the verified `DEPLOY_KNOWN_HOSTS`; repository variables hold `DEPLOY_HOST`, `DEPLOY_USER`, and the activation flag. Enable the flag only after the initial application setup and key installation succeed.

The VM checks the requested commit is still the tip of main and fast-forwards a clean checkout. Older runs skip deployment. A VM-wide file lock serializes Inventory and OptiRoute deployments, including proxy reloads. The worker gets up to 60 seconds to finish before replacement; individual tasks have a 45-second execution limit.

Manual update/rollback:

```bash
git pull --ff-only origin main
bash deploy/up.sh
# Roll back to the previous successful image, keeping Redis data:
bash deploy/up.sh "$(cat deploy/.previous-image)"
```

Successful image references are saved in Git-ignored release files. Rollback does not undo database/Redis schema changes; use compatible releases. If image pulling is denied, check the GitHub container package visibility (public packages need no VM token).

## Task lifecycle and limits

Task records expire 24 hours after their last update. A poll or worker delivery marks an abandoned task older than 15 minutes failed. The parent worker records hard timeouts and lost child processes; if Redis is unavailable during that failure, the stale-task check resolves the record later.

Public submissions have a global one-second spacing and an admission check at 20 queued messages, plus the active/prefetched worker task. This queue-length check is not a cross-process atomic capacity guarantee; the production web runs one process with two threads. Terminal task redelivery is a no-op. Celery delivery and Redis persistence do not provide exactly-once execution.

The frontend and solver remain unchanged. Production uses Django 5.2 LTS, Gunicorn and collected WhiteNoise assets with DEBUG disabled. The C++ extension is compiled into the runtime image; compiler tools exist only in the development/build stages.

## Inspect

```bash
sudo docker compose --env-file deploy/.env.production -f deploy/compose.yml ps
sudo docker compose --env-file deploy/.env.production -f deploy/compose.yml logs --tail 100 web worker
sudo docker stats --no-stream
free -h
```

Container logs rotate at 10 MB × 3 files. Do not use `down -v` on the VPS if task history should survive. Check real memory and computation latency under representative input before increasing worker concurrency.
