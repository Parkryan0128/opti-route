"use strict";

const DEFAULT_MAP_CENTER = { lat: 37.7749, lng: -122.4194 };
const MAP_LOAD_TIMEOUT_MS = 15000;

const appState = {
    map: null,
    mapLoadTimeout: null,
};

const mapLoading = document.querySelector("#map-loading");
const mapError = document.querySelector("#map-error");
const mapErrorMessage = document.querySelector("#map-error-message");
const vehicleCountInput = document.querySelector("#vehicle-count");

function showMapError(message) {
    window.clearTimeout(appState.mapLoadTimeout);
    mapLoading.hidden = true;
    mapErrorMessage.textContent = message;
    mapError.hidden = false;
}

function validateVehicleCount() {
    const value = Number(vehicleCountInput.value);
    const isValid = Number.isInteger(value) && value >= 1 && value <= 10;

    vehicleCountInput.setCustomValidity(
        isValid ? "" : "Enter a whole number between 1 and 10.",
    );
}

window.initMap = async function initMap() {
    try {
        const { Map } = await google.maps.importLibrary("maps");
        appState.map = new Map(document.querySelector("#map"), {
            center: DEFAULT_MAP_CENTER,
            zoom: 12,
            clickableIcons: false,
            fullscreenControl: true,
            mapTypeControl: false,
            streetViewControl: false,
        });

        window.clearTimeout(appState.mapLoadTimeout);
        mapLoading.hidden = true;
        mapError.hidden = true;
    } catch (error) {
        console.error("Google Maps initialization failed.", error);
        showMapError("Google Maps could not be initialized. Check the API configuration.");
    }
};

window.gm_authFailure = function gmAuthFailure() {
    showMapError("Google rejected the API key. Check its API and website restrictions.");
};

vehicleCountInput.addEventListener("input", validateVehicleCount);
validateVehicleCount();

if (document.body.dataset.mapsConfigured !== "true") {
    showMapError("GOOGLE_MAPS_API_KEY is not configured on the server.");
} else {
    appState.mapLoadTimeout = window.setTimeout(() => {
        showMapError("Google Maps took too long to load. Check your network and API quotas.");
    }, MAP_LOAD_TIMEOUT_MS);
}
