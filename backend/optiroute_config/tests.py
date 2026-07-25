from django.test import SimpleTestCase, override_settings
from django.urls import reverse


class IndexViewTests(SimpleTestCase):
    @override_settings(GOOGLE_MAPS_API_KEY="test-browser-key")
    def test_index_loads_map_assets_with_configured_key(self):
        response = self.client.get(reverse("index"))

        self.assertEqual(response.status_code, 200)
        self.assertContains(response, 'id="map"')
        self.assertContains(response, 'id="vehicle-count"')
        self.assertContains(response, 'id="start-optimization"')
        self.assertContains(response, "key=test-browser-key")
        self.assertContains(response, "callback=initMap")
        self.assertContains(response, 'data-maps-configured="true"')

    @override_settings(GOOGLE_MAPS_API_KEY="")
    def test_index_handles_missing_key_without_loading_google_script(self):
        response = self.client.get(reverse("index"))

        self.assertEqual(response.status_code, 200)
        self.assertContains(response, 'data-maps-configured="false"')
        self.assertNotContains(
            response,
            "https://maps.googleapis.com/maps/api/js",
        )
