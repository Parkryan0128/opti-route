"""Small public-demo admission limits, independent of browser sessions."""
from django.conf import settings

from .task_store import get_redis_client


class QueueBusyError(Exception):
    pass


def check_capacity():
    if not settings.OPTIMIZATION_QUEUE_LIMIT:
        return
    redis = get_redis_client()
    if redis.llen("celery") >= settings.OPTIMIZATION_QUEUE_LIMIT:
        raise QueueBusyError("The optimization queue is full. Try again shortly.")
    interval = settings.OPTIMIZATION_MIN_INTERVAL_SECONDS
    if interval and not redis.set("optiroute:admission", "1", nx=True, ex=interval):
        raise QueueBusyError("Please wait a moment before submitting another optimization.")
