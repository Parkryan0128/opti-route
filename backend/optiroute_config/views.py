from django.conf import settings
from django.http import HttpRequest, HttpResponse
from django.shortcuts import render


def index(request: HttpRequest) -> HttpResponse:
    return render(
        request,
        'index.html',
        {'google_maps_api_key': settings.GOOGLE_MAPS_API_KEY},
    )
