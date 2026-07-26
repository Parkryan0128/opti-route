"use strict";

const DEFAULT_MAP_CENTER = { lat: 49.2827, lng: -123.1207 };
const MAP_LOAD_TIMEOUT_MS = 15000;
const POLL_INTERVAL_MS = 2000;
const HIDDEN_POLL_INTERVAL_MS = 5000;
const POLL_TIMEOUT_MS = 30000;
const PENDING_TASK_STORAGE_KEY = "optiroute.pendingTask";
const MAX_GOOGLE_INTERMEDIATES = 25;
const ROUTE_COLORS = [
    "#2563eb",
    "#dc2626",
    "#16a34a",
    "#9333ea",
    "#ea580c",
    "#0891b2",
    "#ca8a04",
    "#db2777",
    "#4f46e5",
    "#0f766e",
];

const appState = {
    map: null,
    mapLoadTimeout: null,
    AdvancedMarkerElement: null,
    PinElement: null,
    depot: null,
    depotMarker: null,
    stops: [],
    stopMarkers: [],
    taskId: null,
    taskStartedAt: null,
    pollTimer: null,
    isBusy: false,
    timedOut: false,
    routePolylines: [],
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
const routeSummary = document.querySelector("#route-summary");
const routeList = document.querySelector("#route-list");
const totalDistance = document.querySelector("#total-distance");
const longestRoute = document.querySelector("#longest-route");
const MAX_STOPS = Number(stopCount.dataset.maxStops);

function showMapError(message) {
    window.clearTimeout(appState.mapLoadTimeout);
    mapLoading.hidden = true;
    mapErrorMessage.textContent = message;
    mapError.hidden = false;
    startButton.disabled = true;
}

function validateVehicleCount() {
    const value = Number(vehicleCountInput.value);
    const minimum = Number(vehicleCountInput.min);
    const maximum = Number(vehicleCountInput.max);
    const isValid =
        Number.isInteger(value) && value >= minimum && value <= maximum;

    vehicleCountInput.setCustomValidity(
        isValid
            ? ""
            : `Enter a whole number between ${minimum} and ${maximum}.`,
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

function plannerSnapshot() {
    const vehicleCount = Number(vehicleCountInput.value);
    const validVehicleCount = validateVehicleCount();
    const hasDepot = appState.depot !== null;
    const hasStops = appState.stops.length > 0;
    const enoughStops =
        validVehicleCount && vehicleCount <= appState.stops.length;
    return {
        vehicleCount,
        validVehicleCount,
        hasDepot,
        hasStops,
        enoughStops,
    };
}

function canSubmitOptimization(state = plannerSnapshot()) {
    return (
        !appState.isBusy &&
        state.hasDepot &&
        state.hasStops &&
        state.validVehicleCount &&
        state.enoughStops
    );
}

function updatePlannerControls(state) {
    depotStatus.textContent = state.hasDepot ? "Selected" : "Not selected";
    stopCount.textContent = `${appState.stops.length} / ${MAX_STOPS}`;
    clearButton.disabled = !state.hasDepot || appState.isBusy;
    vehicleCountInput.disabled = appState.isBusy || appState.timedOut;

    if (appState.isBusy) {
        startButton.disabled = true;
        startButton.textContent = "Optimizing…";
        return false;
    }

    if (appState.timedOut) {
        startButton.disabled = false;
        startButton.textContent = "Check status";
        return false;
    }

    startButton.textContent = "Start optimization";
    startButton.disabled = !canSubmitOptimization(state);
    return true;
}

function updatePlannerGuidance(state) {
    const maximumVehicles = Number(vehicleCountInput.max);
    if (!state.hasDepot) {
        selectionInstruction.textContent =
            "Your first map click will set the depot.";
        setAppStatus(
            "Waiting for a depot and at least one stop.",
        );
    } else if (!state.hasStops) {
        selectionInstruction.textContent =
            "Now click the map to add delivery stops.";
        setAppStatus("Add at least one delivery stop.");
    } else if (!state.validVehicleCount) {
        selectionInstruction.textContent =
            "Continue adding stops or correct the vehicle count.";
        setAppStatus(
            `Enter a whole number between 1 and ${maximumVehicles} vehicles.`,
        );
    } else if (!state.enoughStops) {
        selectionInstruction.textContent =
            "Add more stops or reduce the number of vehicles.";
        setAppStatus(
            "The number of vehicles cannot exceed the number of stops.",
        );
    } else if (appState.stops.length === MAX_STOPS) {
        selectionInstruction.textContent =
            `The ${MAX_STOPS}-stop limit has been reached.`;
        setAppStatus("Locations are ready for optimization.");
    } else {
        selectionInstruction.textContent =
            `Click the map to add more stops, up to ${MAX_STOPS} total.`;
        setAppStatus("Locations are ready for optimization.");
    }
}

function updatePlannerState() {
    const state = plannerSnapshot();
    if (updatePlannerControls(state)) {
        updatePlannerGuidance(state);
    }
}

function handleMapClick(event) {
    if (appState.isBusy || appState.timedOut || !event.latLng) {
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
        setAppStatus(`A maximum of ${MAX_STOPS} stops is supported.`);
        return;
    }

    updatePlannerState();
}

function clearRenderedRoutes() {
    for (const routePolylines of appState.routePolylines) {
        for (const polyline of routePolylines || []) {
            polyline.setMap(null);
        }
    }
    appState.routePolylines = [];
    routeList.replaceChildren();
    routeSummary.hidden = true;
}

function clearLocations() {
    clearPollTimer();
    removeStoredTask();
    clearRenderedRoutes();

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
    appState.taskId = null;
    appState.taskStartedAt = null;
    appState.isBusy = false;
    appState.timedOut = false;
    updatePlannerState();
}

function setAppStatus(message, state = "info") {
    appStatus.textContent = message;
    appStatus.dataset.state = state;
}

function clearPollTimer() {
    window.clearTimeout(appState.pollTimer);
    appState.pollTimer = null;
}

function pendingTaskSnapshot() {
    return {
        taskId: appState.taskId,
        startedAt: appState.taskStartedAt,
        depot: appState.depot,
        stops: appState.stops,
        numVehicles: Number(vehicleCountInput.value),
    };
}

function storePendingTask() {
    try {
        sessionStorage.setItem(
            PENDING_TASK_STORAGE_KEY,
            JSON.stringify(pendingTaskSnapshot()),
        );
    } catch (error) {
        console.warn("Could not persist pending task state.", error);
    }
}

function loadPendingTask() {
    try {
        const value = sessionStorage.getItem(PENDING_TASK_STORAGE_KEY);
        return value ? JSON.parse(value) : null;
    } catch (error) {
        console.warn("Could not restore pending task state.", error);
        removeStoredTask();
        return null;
    }
}

function removeStoredTask() {
    try {
        sessionStorage.removeItem(PENDING_TASK_STORAGE_KEY);
    } catch (error) {
        console.warn("Could not clear pending task state.", error);
    }
}

function isCoordinate(value) {
    return (
        value !== null &&
        Number.isFinite(value.lat) &&
        value.lat >= -90 &&
        value.lat <= 90 &&
        Number.isFinite(value.lng) &&
        value.lng >= -180 &&
        value.lng <= 180
    );
}

async function requestJson(url, options = {}) {
    const response = await fetch(url, {
        ...options,
        headers: {
            Accept: "application/json",
            ...(options.headers || {}),
        },
    });

    let body = null;
    try {
        body = await response.json();
    } catch {
        // The status code below still provides a useful fallback error.
    }

    if (!response.ok) {
        const error = new Error(
            body?.error_message ||
                `Request failed with status ${response.status}.`,
        );
        error.status = response.status;
        throw error;
    }
    return body;
}

function splitRouteCoordinates(coordinates) {
    const segments = [];
    let start = 0;

    while (start < coordinates.length - 1) {
        const end = Math.min(
            start + MAX_GOOGLE_INTERMEDIATES + 1,
            coordinates.length - 1,
        );
        segments.push(coordinates.slice(start, end + 1));
        start = end;
    }
    return segments;
}

function routeColor(index) {
    return ROUTE_COLORS[index % ROUTE_COLORS.length];
}

async function renderVehicleRoute(Route, route, routeIndex) {
    if (
        !Array.isArray(route.route_coordinates) ||
        route.route_coordinates.length < 3 ||
        !route.route_coordinates.every(isCoordinate)
    ) {
        throw new Error("The optimizer returned invalid route coordinates.");
    }

    const color = routeColor(routeIndex);
    const vehiclePolylines = [];

    try {
        for (const segment of splitRouteCoordinates(
            route.route_coordinates,
        )) {
            const { routes } = await Route.computeRoutes({
                origin: segment[0],
                destination: segment[segment.length - 1],
                intermediates: segment.slice(1, -1).map((location) => ({
                    location,
                })),
                travelMode: "DRIVING",
                optimizeWaypointOrder: false,
                fields: ["path"],
            });

            if (!routes?.length) {
                throw new Error("Google returned no drivable route.");
            }

            const polylines = routes[0].createPolylines({
                polylineOptions: {
                    strokeColor: color,
                    strokeOpacity: 0.9,
                    strokeWeight: 5,
                    zIndex: 100 - routeIndex,
                },
            });
            for (const polyline of polylines) {
                polyline.setMap(appState.map);
                vehiclePolylines.push(polyline);
            }
        }
    } catch (error) {
        for (const polyline of vehiclePolylines) {
            polyline.setMap(null);
        }
        throw error;
    }

    appState.routePolylines[routeIndex] = vehiclePolylines;
}

function showRouteSummary(result) {
    routeList.replaceChildren();

    for (const [index, route] of result.routes.entries()) {
        const item = document.createElement("label");
        item.className = "route-item";

        const checkbox = document.createElement("input");
        checkbox.className = "route-checkbox";
        checkbox.type = "checkbox";
        checkbox.checked = Boolean(appState.routePolylines[index]?.length);
        checkbox.disabled = !checkbox.checked;
        checkbox.setAttribute(
            "aria-label",
            `Show route for vehicle ${route.vehicle_id}`,
        );
        checkbox.style.accentColor = routeColor(index);
        checkbox.addEventListener("change", () => {
            for (const polyline of appState.routePolylines[index] || []) {
                polyline.setMap(checkbox.checked ? appState.map : null);
            }
        });

        const swatch = document.createElement("span");
        swatch.className = "route-swatch";
        swatch.style.backgroundColor = routeColor(index);

        const label = document.createElement("strong");
        label.textContent = `Vehicle ${route.vehicle_id}`;

        const details = document.createElement("span");
        const stopLabel = route.stop_order.length === 1 ? "stop" : "stops";
        details.textContent =
            `${route.stop_order.length} ${stopLabel} · ` +
            `${route.distance_km.toFixed(1)} km`;

        item.append(checkbox, swatch, label, details);
        routeList.append(item);
    }

    totalDistance.textContent = `${result.total_distance_km.toFixed(1)} km`;
    longestRoute.textContent =
        `Longest route: ${result.max_distance_km.toFixed(1)} km.`;
    routeSummary.hidden = false;
}

async function renderOptimizedRoutes(result) {
    if (
        !result ||
        !Array.isArray(result.routes) ||
        result.routes.length === 0 ||
        !result.routes.every(
            (route) =>
                Number.isInteger(route.vehicle_id) &&
                Array.isArray(route.stop_order) &&
                Number.isFinite(route.distance_km),
        ) ||
        !Number.isFinite(result.total_distance_km) ||
        !Number.isFinite(result.max_distance_km)
    ) {
        throw new Error("The optimizer returned an invalid result.");
    }

    clearRenderedRoutes();
    const { Route } = await google.maps.importLibrary("routes");
    const failures = [];

    for (const [index, route] of result.routes.entries()) {
        setAppStatus(
            `Drawing road route ${index + 1} of ${result.routes.length}…`,
        );
        try {
            await renderVehicleRoute(Route, route, index);
        } catch (error) {
            console.error(
                `Could not draw road route for vehicle ${route.vehicle_id}.`,
                error,
            );
            failures.push(`Vehicle ${route.vehicle_id}: ${error.message}`);
        }
    }

    showRouteSummary(result);
    return failures;
}

function finishPolling(keepBusy = false) {
    clearPollTimer();
    appState.isBusy = keepBusy;
    appState.timedOut = false;
    appState.taskId = null;
    appState.taskStartedAt = null;
    removeStoredTask();
    updatePlannerState();
}

function handlePollingTimeout() {
    clearPollTimer();
    appState.isBusy = false;
    appState.timedOut = true;
    updatePlannerState();
    setAppStatus(
        "Still processing. The backend task continues; choose Check status to resume polling.",
        "warning",
    );
}

function schedulePoll() {
    const delay = document.hidden
        ? HIDDEN_POLL_INTERVAL_MS
        : POLL_INTERVAL_MS;
    appState.pollTimer = window.setTimeout(pollTask, delay);
}

async function handleOptimizationSuccess(result) {
    finishPolling(true);
    try {
        const failures = await renderOptimizedRoutes(result);
        appState.isBusy = false;
        updatePlannerState();
        if (failures.length > 0) {
            setAppStatus(
                `Optimization completed, but ${failures.join(" ")}`,
                "warning",
            );
        } else {
            setAppStatus(
                "Optimization complete. Road routes are shown on the map.",
                "success",
            );
        }
    } catch (error) {
        appState.isBusy = false;
        updatePlannerState();
        setAppStatus(
            error.message || "Could not draw the optimized routes.",
            "error",
        );
    }
}

async function handleTaskResponse(response) {
    if (response.status === "SUCCESS") {
        await handleOptimizationSuccess(response.result);
        return;
    }
    if (response.status === "FAILED") {
        finishPolling();
        setAppStatus(
            response.error_message || "Optimization failed.",
            "error",
        );
        return;
    }
    if (
        response.status === "PENDING" ||
        response.status === "PROCESSING"
    ) {
        setAppStatus(
            response.status === "PENDING"
                ? "Optimization queued…"
                : "Optimizing routes…",
        );
        schedulePoll();
        return;
    }

    finishPolling();
    setAppStatus("The server returned an unknown task status.", "error");
}

async function pollTask() {
    if (!appState.isBusy || !appState.taskId) {
        return;
    }
    if (Date.now() - appState.taskStartedAt >= POLL_TIMEOUT_MS) {
        handlePollingTimeout();
        return;
    }

    const taskId = appState.taskId;
    try {
        const response = await requestJson(
            `/api/v1/optimize/${encodeURIComponent(taskId)}/`,
        );
        if (taskId !== appState.taskId) {
            return;
        }

        await handleTaskResponse(response);
    } catch (error) {
        if (taskId !== appState.taskId) {
            return;
        }
        if (error.status === 404) {
            finishPolling();
            setAppStatus("The optimization task could not be found.", "error");
            return;
        }

        setAppStatus(
            "Could not check task status. Retrying automatically…",
            "warning",
        );
        schedulePoll();
    }
}

async function submitOptimization() {
    const state = plannerSnapshot();
    if (!canSubmitOptimization(state)) {
        return;
    }

    appState.isBusy = true;
    clearRenderedRoutes();
    updatePlannerState();
    setAppStatus("Submitting optimization request…");

    try {
        const response = await requestJson("/api/v1/optimize/", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify({
                depot: appState.depot,
                stops: appState.stops,
                num_vehicles: state.vehicleCount,
            }),
        });

        appState.taskId = response.task_id;
        if (typeof appState.taskId !== "string" || !appState.taskId) {
            throw new Error("The server did not return a valid task ID.");
        }
        appState.taskStartedAt = Date.now();
        appState.timedOut = false;
        storePendingTask();
        setAppStatus("Optimization queued…");
        schedulePoll();
    } catch (error) {
        appState.isBusy = false;
        updatePlannerState();
        setAppStatus(
            error.message || "Could not submit the optimization request.",
            "error",
        );
    }
}

function isValidPendingTask(pending) {
    return Boolean(
        pending &&
        typeof pending.taskId === "string" &&
        Number.isFinite(pending.startedAt) &&
        isCoordinate(pending.depot) &&
        Array.isArray(pending.stops) &&
        pending.stops.length > 0 &&
        pending.stops.length <= MAX_STOPS &&
        pending.stops.every(isCoordinate) &&
        Number.isInteger(pending.numVehicles) &&
        pending.numVehicles >= 1 &&
        pending.numVehicles <= pending.stops.length
    );
}

function applyPendingTask(pending) {
    appState.depot = pending.depot;
    appState.stops = pending.stops;
    appState.taskId = pending.taskId;
    appState.taskStartedAt = pending.startedAt;
    appState.depotMarker = createMarker(appState.depot, "depot");
    appState.stopMarkers = appState.stops.map((position, index) =>
        createMarker(position, "stop", index + 1),
    );
    vehicleCountInput.value = String(pending.numVehicles);

    const bounds = new google.maps.LatLngBounds();
    bounds.extend(appState.depot);
    for (const stop of appState.stops) {
        bounds.extend(stop);
    }
    appState.map.fitBounds(bounds, 60);
}

function restorePendingTask() {
    const pending = loadPendingTask();
    if (!isValidPendingTask(pending)) {
        if (pending) {
            removeStoredTask();
        }
        return;
    }

    applyPendingTask(pending);

    if (Date.now() - appState.taskStartedAt >= POLL_TIMEOUT_MS) {
        appState.timedOut = true;
        updatePlannerState();
        setAppStatus(
            "This task may still be processing. Choose Check status to resume polling.",
            "warning",
        );
    } else {
        appState.isBusy = true;
        updatePlannerState();
        setAppStatus("Resuming optimization status checks…");
        pollTask();
    }
}

function resumePolling() {
    appState.timedOut = false;
    appState.isBusy = true;
    appState.taskStartedAt = Date.now();
    storePendingTask();
    updatePlannerState();
    setAppStatus("Checking optimization status…");
    pollTask();
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
        restorePendingTask();
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
    if (appState.timedOut && appState.taskId) {
        resumePolling();
    } else {
        submitOptimization();
    }
});
updatePlannerState();

if (document.body.dataset.mapsConfigured !== "true") {
    showMapError("GOOGLE_MAPS_API_KEY is not configured on the server.");
} else {
    appState.mapLoadTimeout = window.setTimeout(() => {
        showMapError("Google Maps took too long to load. Check your network and API quotas.");
    }, MAP_LOAD_TIMEOUT_MS);
}
