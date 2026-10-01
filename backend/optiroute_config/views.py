from django.conf import settings
from django.http import HttpRequest, HttpResponse, JsonResponse
from django.shortcuts import render


def index(request: HttpRequest) -> HttpResponse:
    return render(
        request,
        'index.html',
        {'google_maps_api_key': settings.GOOGLE_MAPS_API_KEY},
    )


def health(request: HttpRequest) -> JsonResponse:
    from api.task_store import get_redis_client
    try:
        get_redis_client().ping()
        return JsonResponse({"status": "UP"})
    except Exception:
        return JsonResponse({"status": "DOWN"}, status=503)
