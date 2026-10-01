from django.urls import include, path

from .views import health, index

urlpatterns = [
    path('', index, name='index'),
    path('health/', health, name='health'),
    path('api/v1/', include('api.urls')),
]
