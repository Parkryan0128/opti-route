"use strict";

const DEFAULT_MAP_CENTER = { lat: 37.7749, lng: -122.4194 };
const MAP_LOAD_TIMEOUT_MS = 15000;
const MAX_STOPS = 10;

const appState = {
    map: null,
    mapLoadTimeout: null,
    AdvancedMarkerElement: null,
    PinElement: null,
    depot: null,
    depotMarker: null,
    stops: [],
    stopMarkers: [],
};

const mapLoading = document.querySelector("#map-loading");
const mapError = document.querySelector("#map-error");
const mapErrorMessage = document.querySelector("#map-error-message");
const vehicleCountInput = document.querySelector("#vehicle-count");
const depotStatus = document.querySelector("#depot-status");
const stopCount = document.querySelector("#stop-count");
const selectionInstruction = document.querySelector("#selection-instruction");
const startButton = document.querySelector("#start-optimization");
const clearButton = document.querySelector("#clear-locations");
const appStatus = document.querySelector("#app-status");

function showMapError(message) {
    window.clearTimeout(appState.mapLoadTimeout);
    mapLoading.hidden = true;
    mapErrorMessage.textContent = message;
    mapError.hidden = false;
    startButton.disabled = true;
}

function validateVehicleCount() {
    const value = Number(vehicleCountInput.value);
    const isValid = Number.isInteger(value) && value >= 1 && value <= 10;

    vehicleCountInput.setCustomValidity(
        isValid ? "" : "Enter a whole number between 1 and 10.",
    );
    return isValid;
}

function createMarker(position, type, stopNumber = null) {
    const isDepot = type === "depot";
    const pin = new appState.PinElement({
        background: isDepot ? "#14213d" : "#2563eb",
        borderColor: isDepot ? "#0f172a" : "#1d4ed8",
        glyphColor: "#ffffff",
        glyphText: isDepot ? "D" : String(stopNumber),
        scale: isDepot ? 1.2 : 1,
    });

    return new appState.AdvancedMarkerElement({
        map: appState.map,
        position,
        title: isDepot ? "Depot" : `Stop ${stopNumber}`,
        content: pin,
    });
}

function updatePlannerState() {
    const vehicleCount = Number(vehicleCountInput.value);
    const validVehicleCount = validateVehicleCount();
    const hasDepot = appState.depot !== null;
    const hasStops = appState.stops.length > 0;
    const enoughStops =
        validVehicleCount && vehicleCount <= appState.stops.length;

    depotStatus.textContent = hasDepot ? "Selected" : "Not selected";
    stopCount.textContent = `${appState.stops.length} / ${MAX_STOPS}`;
    clearButton.disabled = !hasDepot;
    startButton.disabled = !(hasDepot && hasStops && enoughStops);

    if (!hasDepot) {
        selectionInstruction.textContent =
            "Your first map click will set the depot.";
        appStatus.textContent =
            "Waiting for a depot and at least one stop.";
    } else if (!hasStops) {
        selectionInstruction.textContent =
            "Now click the map to add delivery stops.";
        appStatus.textContent = "Add at least one delivery stop.";
    } else if (!validVehicleCount) {
        selectionInstruction.textContent =
            "Continue adding stops or correct the vehicle count.";
        appStatus.textContent =
            "Enter a whole number between 1 and 10 vehicles.";
    } else if (!enoughStops) {
        selectionInstruction.textContent =
            "Add more stops or reduce the number of vehicles.";
        appStatus.textContent =
            "The number of vehicles cannot exceed the number of stops.";
    } else if (appState.stops.length === MAX_STOPS) {
        selectionInstruction.textContent =
            "The 10-stop MVP limit has been reached.";
        appStatus.textContent = "Locations are ready for optimization.";
    } else {
        selectionInstruction.textContent =
            "Click the map to add more stops, up to 10 total.";
        appStatus.textContent = "Locations are ready for optimization.";
    }
}

function handleMapClick(event) {
    if (!event.latLng) {
        return;
    }

    const position = event.latLng.toJSON();
    if (appState.depot === null) {
        appState.depot = position;
        appState.depotMarker = createMarker(position, "depot");
    } else if (appState.stops.length < MAX_STOPS) {
        const stopNumber = appState.stops.length + 1;
        appState.stops.push(position);
        appState.stopMarkers.push(
            createMarker(position, "stop", stopNumber),
        );
    } else {
        appStatus.textContent =
            "The MVP supports a maximum of 10 stops.";
        return;
    }

    updatePlannerState();
}

function clearLocations() {
    if (appState.depotMarker) {
        appState.depotMarker.map = null;
    }
    for (const marker of appState.stopMarkers) {
        marker.map = null;
    }

    appState.depot = null;
    appState.depotMarker = null;
    appState.stops = [];
    appState.stopMarkers = [];
    updatePlannerState();
}

window.initMap = async function initMap() {
    try {
        const [{ Map }, { AdvancedMarkerElement, PinElement }] =
            await Promise.all([
                google.maps.importLibrary("maps"),
                google.maps.importLibrary("marker"),
            ]);

        appState.AdvancedMarkerElement = AdvancedMarkerElement;
        appState.PinElement = PinElement;
        appState.map = new Map(document.querySelector("#map"), {
            center: DEFAULT_MAP_CENTER,
            zoom: 12,
            mapId: "DEMO_MAP_ID",
            clickableIcons: false,
            draggableCursor: "crosshair",
            fullscreenControl: true,
            mapTypeControl: false,
            streetViewControl: false,
        });
        appState.map.addListener("click", handleMapClick);

        window.clearTimeout(appState.mapLoadTimeout);
        mapLoading.hidden = true;
        mapError.hidden = true;
        updatePlannerState();
    } catch (error) {
        console.error("Google Maps initialization failed.", error);
        showMapError("Google Maps could not be initialized. Check the API configuration.");
    }
};

window.gm_authFailure = function gmAuthFailure() {
    showMapError("Google rejected the API key. Check its API and website restrictions.");
};

vehicleCountInput.addEventListener("input", updatePlannerState);
clearButton.addEventListener("click", clearLocations);
startButton.addEventListener("click", () => {
    appStatus.textContent =
        "Task submission will be connected in Step 4.3.";
});
updatePlannerState();

if (document.body.dataset.mapsConfigured !== "true") {
    showMapError("GOOGLE_MAPS_API_KEY is not configured on the server.");
} else {
    appState.mapLoadTimeout = window.setTimeout(() => {
        showMapError("Google Maps took too long to load. Check your network and API quotas.");
    }, MAP_LOAD_TIMEOUT_MS);
}
