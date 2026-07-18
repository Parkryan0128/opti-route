from django.urls import path

from .views import OptimizationDetailView, OptimizationListView

app_name = "api"

urlpatterns = [
    path(
        "optimize/",
        OptimizationListView.as_view(),
        name="optimization-list",
    ),
    path(
        "optimize/<str:task_id>/",
        OptimizationDetailView.as_view(),
        name="optimization-detail",
    ),
]
