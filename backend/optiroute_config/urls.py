from django.urls import include, path

from .views import index

urlpatterns = [
    path('', index, name='index'),
    path('api/v1/', include('api.urls')),
]
