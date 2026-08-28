const elements = {
    map: document.querySelector("#liveMap"),
    fallback: document.querySelector("#mapFallback"),
    fallbackReason: document.querySelector("#mapFallbackReason"),
    warning: document.querySelector("#mapWarning"),
    status: document.querySelector("#connectionStatus"),
    follow: document.querySelector("#followUav"),
    mapControls: document.querySelector("#mapZoomControls"),
    zoomIn: document.querySelector("#mapZoomIn"),
    zoomOut: document.querySelector("#mapZoomOut"),
    recenter: document.querySelector("#mapRecenter"),
    sourceBadge: document.querySelector("#dataSourceBadge"),
    mapModeSelect: document.querySelector("#mapModeSelect"),
    mapModeStatus: document.querySelector("#mapModeStatus"),
    trackCount: document.querySelector("#trackCount"),
    smokeCount: document.querySelector("#smokeCount"),
    waypointCount: document.querySelector("#waypointCount"),
    targetList: document.querySelector("#smokeTargetList"),
    latitude: document.querySelector("#latitude"),
    longitude: document.querySelector("#longitude"),
    altitude: document.querySelector("#altitude"),
    speed: document.querySelector("#speed"),
    heading: document.querySelector("#heading"),
    gimbalPitch: document.querySelector("#gimbalPitch"),
    lastUpdated: document.querySelector("#lastUpdated"),
    fireLatitude: document.querySelector("#fireLatitude"),
    fireLongitude: document.querySelector("#fireLongitude"),
    fireDistance: document.querySelector("#fireDistance"),
    videoSelect: document.querySelector("#liveVideoSelect"),
    videoPlayer: document.querySelector("#liveVideoPlayer"),
    videoEmpty: document.querySelector("#liveVideoEmpty"),
    streamStatus: document.querySelector("#streamStatus"),
    currentVideoTitle: document.querySelector("#currentVideoTitle"),
    currentVideoMeta: document.querySelector("#currentVideoMeta"),
    goLiveButton: document.querySelector("#goLiveButton"),
    geolocationStatus: document.querySelector("#geolocationStatus"),
    geolocationMessage: document.querySelector("#geolocationMessage"),
    geoVideoTime: document.querySelector("#geoVideoTime"),
    geoFrame: document.querySelector("#geoFrame"),
    geoConfidence: document.querySelector("#geoConfidence"),
    geoReferenceError: document.querySelector("#geoReferenceError"),
    geoTelemetryCoordinates: document.querySelector("#geoTelemetryCoordinates"),
    geoTargetCoordinates: document.querySelector("#geoTargetCoordinates")
};

const state = {
    provider: null,
    latest: null,
    track: [],
    firePoint: null,
    smokeTargets: [],
    missionWaypoints: [],
    baselineSmokeTargets: [],
    baselineMissionWaypoints: [],
    videos: [],
    currentVideo: null,
    geolocationRequestToken: 0,
    geolocationPending: false,
    lastGeolocationFetchMs: 0,
    hasLocatedTarget: false
};

function setConnectionStatus(text, kind) {
    elements.status.textContent = text;
    elements.status.className = `badge text-bg-${kind}`;
}

function showWarning(message) {
    elements.warning.textContent = message;
    elements.warning.classList.toggle("d-none", !message);
}

function pointFeature(point) {
    return {
        type: "Feature",
        geometry: {type: "Point", coordinates: [point.longitude, point.latitude]},
        properties: {
            id: point.id,
            label: point.label,
            confidence: point.confidence ?? null,
            altitudeM: point.altitudeM ?? null
        }
    };
}

function pointCollection(points) {
    return {type: "FeatureCollection", features: points.map(pointFeature)};
}

function lineFeature(points) {
    if (points.length < 2) {
        return {type: "FeatureCollection", features: []};
    }
    return {
        type: "Feature",
        properties: {},
        geometry: {
            type: "LineString",
            coordinates: points.map(point => [point.longitude, point.latitude])
        }
    };
}

class NoMapProvider {
    constructor(reason = "当前配置未启用地图") {
        this.reason = reason;
    }

    async init() {
        elements.map.classList.add("d-none");
        elements.fallback.classList.remove("d-none");
        elements.mapControls.classList.add("d-none");
        elements.fallbackReason.textContent = `${this.reason}，遥测数据仍会实时更新`;
    }

    setTrack() {}
    setFirePoint() {}
    setSmokeTargets() {}
    setMissionWaypoints() {}
    updateTelemetry() {}
    zoomIn() {}
    zoomOut() {}
    recenter() {}
    destroy() {}
}

class GoogleMapProvider extends NoMapProvider {
    constructor() {
        super("Google 地图接口已保留，当前版本尚未接入密钥与实现");
    }
}

class MapLibreProvider {
    constructor(config) {
        this.config = config;
        this.map = null;
        this.droneMarker = null;
    }

    async init() {
        elements.mapControls.classList.remove("d-none");
        this.installStylesheet();
        const mapLibre = await import(this.config.mapLibreModuleUrl);
        const style = this.config.effectiveStyle === "satellite"
            ? this.config.styleUrl
            : {
                version: 8,
                sources: {
                    osm: {
                        type: "raster",
                        tiles: [this.config.tileUrl],
                        tileSize: 256,
                        attribution: this.config.attribution
                    }
                },
                layers: [{id: "osm", type: "raster", source: "osm"}]
            };

        this.map = new mapLibre.Map({
            container: elements.map,
            style,
            center: [this.config.centerLongitude, this.config.centerLatitude],
            zoom: this.config.zoom,
            minZoom: 2,
            maxZoom: 20,
            scrollZoom: true,
            dragPan: true,
            doubleClickZoom: true,
            touchZoomRotate: true,
            keyboard: true
        });

        await new Promise((resolve, reject) => {
            const timeout = window.setTimeout(
                () => reject(new Error("地图加载超时")),
                12000
            );
            this.map.once("load", () => {
                window.clearTimeout(timeout);
                resolve();
            });
            this.map.once("error", event => {
                if (!this.map.loaded()) {
                    window.clearTimeout(timeout);
                    reject(event.error ?? new Error("地图资源加载失败"));
                }
            });
        });

        this.addOperationalLayers();
        const markerElement = document.createElement("div");
        markerElement.className = "uav-map-marker";
        markerElement.setAttribute("aria-label", "无人机当前位置");
        markerElement.innerHTML = "<span></span>";
        this.droneMarker = new mapLibre.Marker({element: markerElement, rotationAlignment: "map"})
            .setLngLat([this.config.centerLongitude, this.config.centerLatitude])
            .addTo(this.map);
        this.disableFollowWhenUserInteracts();
        window.requestAnimationFrame(() => this.map.resize());
    }

    installStylesheet() {
        if (document.querySelector("#maplibreStyles")) {
            return;
        }
        const link = document.createElement("link");
        link.id = "maplibreStyles";
        link.rel = "stylesheet";
        link.href = this.config.mapLibreStylesheetUrl;
        document.head.append(link);
    }

    addOperationalLayers() {
        this.map.addSource("uav-track", {type: "geojson", data: lineFeature([])});
        this.map.addLayer({
            id: "uav-track-line",
            type: "line",
            source: "uav-track",
            paint: {"line-color": "#2f80ed", "line-width": 4, "line-opacity": 0.9}
        });
        this.map.addSource("mission-route", {type: "geojson", data: lineFeature([])});
        this.map.addLayer({
            id: "mission-route-line",
            type: "line",
            source: "mission-route",
            paint: {
                "line-color": "#f2c94c",
                "line-width": 3,
                "line-dasharray": [2, 2]
            }
        });
        this.map.addSource("smoke-targets", {type: "geojson", data: pointCollection([])});
        this.map.addLayer({
            id: "smoke-target-points",
            type: "circle",
            source: "smoke-targets",
            paint: {
                "circle-radius": 9,
                "circle-color": "#dc3545",
                "circle-stroke-color": "#ffffff",
                "circle-stroke-width": 3
            }
        });
        this.map.addSource("fire-point", {type: "geojson", data: pointCollection([])});
        this.map.addLayer({
            id: "fire-point-marker",
            type: "circle",
            source: "fire-point",
            paint: {
                "circle-radius": 11,
                "circle-color": "#ff7a00",
                "circle-stroke-color": "#ffffff",
                "circle-stroke-width": 4
            }
        });
        this.map.addSource("mission-waypoints", {type: "geojson", data: pointCollection([])});
        this.map.addLayer({
            id: "mission-waypoint-points",
            type: "circle",
            source: "mission-waypoints",
            paint: {
                "circle-radius": 7,
                "circle-color": "#f2c94c",
                "circle-stroke-color": "#6b5600",
                "circle-stroke-width": 2
            }
        });
    }

    setTrack(track) {
        this.map.getSource("uav-track")?.setData(lineFeature(track));
    }

    setFirePoint(firePoint) {
        const points = firePoint ? [firePoint] : [];
        this.map.getSource("fire-point")?.setData(pointCollection(points));
    }

    setSmokeTargets(targets) {
        this.map.getSource("smoke-targets")?.setData(pointCollection(targets));
    }

    setMissionWaypoints(waypoints) {
        this.map.getSource("mission-waypoints")?.setData(pointCollection(waypoints));
        this.map.getSource("mission-route")?.setData(lineFeature(waypoints));
    }

    updateTelemetry(sample) {
        if (!this.droneMarker || !sample) {
            return;
        }
        const location = [sample.longitude, sample.latitude];
        this.droneMarker.setLngLat(location).setRotation(sample.headingDeg);
        if (elements.follow.checked) {
            this.map.easeTo({center: location, duration: 700});
        }
    }

    disableFollowWhenUserInteracts() {
        for (const eventName of ["zoomstart", "dragstart", "rotatestart", "pitchstart"]) {
            this.map.on(eventName, event => {
                if (event.originalEvent) {
                    elements.follow.checked = false;
                }
            });
        }
    }

    zoomIn() {
        elements.follow.checked = false;
        this.map.zoomIn({duration: 250});
    }

    zoomOut() {
        elements.follow.checked = false;
        this.map.zoomOut({duration: 250});
    }

    recenter() {
        elements.follow.checked = true;
        this.updateTelemetry(state.latest);
    }

    destroy() {
        this.map?.remove();
    }
}

function createProvider(config) {
    if (config.provider === "maplibre") {
        return new MapLibreProvider(config);
    }
    if (config.provider === "google") {
        return new GoogleMapProvider();
    }
    return new NoMapProvider();
}

function resetMapSurface() {
    elements.fallback.classList.add("d-none");
    elements.map.classList.remove("d-none", "maplibregl-map");
    elements.map.replaceChildren();
}

function selectedModeFor(config) {
    if (config.provider === "google") {
        return "google:osm";
    }
    if (config.provider === "none") {
        return "none:osm";
    }
    return config.requestedStyle === "satellite"
        ? "maplibre:satellite"
        : "maplibre:osm";
}

function describeMapMode(config, fellBack = false) {
    if (fellBack) {
        return "地图资源加载失败，当前显示坐标面板";
    }
    if (config.provider === "google") {
        return "Google 提供者接口已预留，但尚未接入 SDK 和 API Key";
    }
    if (config.provider === "none") {
        return "已关闭底图，实时坐标和遥测仍会更新";
    }
    if (config.requestedStyle === "satellite" && config.effectiveStyle === "osm") {
        return "未配置 MapTiler Key，卫星地图已回退为 OSM 普通地图";
    }
    return config.effectiveStyle === "satellite"
        ? "当前显示 MapTiler 卫星地图"
        : "当前显示 OpenStreetMap 普通地图";
}

function applyMonitoringLayers() {
    if (!state.provider) {
        return;
    }
    state.provider.setTrack(state.track);
    state.provider.setFirePoint(state.firePoint);
    state.provider.setSmokeTargets(state.smokeTargets);
    state.provider.setMissionWaypoints(state.missionWaypoints);
    state.provider.updateTelemetry(state.latest);
}

async function loadMapConfiguration(provider, style) {
    const query = new URLSearchParams({provider, style});
    const response = await fetch(`/api/map/config?${query}`);
    if (!response.ok) {
        throw new Error("地图配置接口返回异常");
    }
    const envelope = await response.json();
    if (!envelope.data) {
        throw new Error("地图配置数据为空");
    }
    return envelope.data;
}

async function activateMapProvider(config) {
    state.provider?.destroy();
    state.provider = null;
    resetMapSurface();
    showWarning(config.warning);

    let nextProvider = createProvider(config);
    let fellBack = false;
    try {
        await nextProvider.init();
    } catch (error) {
        nextProvider.destroy();
        resetMapSurface();
        nextProvider = new NoMapProvider("地图资源加载失败");
        await nextProvider.init();
        fellBack = true;
        showWarning(`地图资源加载失败，已进入无地图模式：${error.message}`);
    }
    state.provider = nextProvider;
    elements.mapModeStatus.textContent = describeMapMode(config, fellBack);
    applyMonitoringLayers();
}

async function switchMapMode() {
    const [provider, style] = elements.mapModeSelect.value.split(":");
    elements.mapModeSelect.disabled = true;
    elements.mapModeStatus.textContent = "正在切换地图…";
    try {
        const config = await loadMapConfiguration(provider, style);
        await activateMapProvider(config);
    } catch (error) {
        showWarning(`地图切换失败：${error.message}`);
        elements.mapModeStatus.textContent = "切换失败，保留当前遥测数据";
    } finally {
        elements.mapModeSelect.disabled = false;
    }
}

function renderTelemetry(sample) {
    if (!sample) {
        return;
    }
    state.latest = sample;
    elements.latitude.textContent = sample.latitude.toFixed(6);
    elements.longitude.textContent = sample.longitude.toFixed(6);
    elements.altitude.textContent = `${sample.altitudeM.toFixed(1)} m`;
    elements.speed.textContent = `${sample.speedMps.toFixed(1)} m/s`;
    elements.heading.textContent = `${sample.headingDeg.toFixed(0)}°`;
    elements.gimbalPitch.textContent = `${sample.gimbalPitchDeg.toFixed(0)}°`;
    elements.lastUpdated.dateTime = sample.timestamp;
    elements.lastUpdated.textContent = new Date(sample.timestamp).toLocaleTimeString("zh-CN", {
        hour12: false
    });
    renderFirePointDistance(sample);
    state.provider?.updateTelemetry(sample);
}

function haversineDistanceMeters(first, second) {
    if (!first || !second) {
        return null;
    }
    const earthRadiusM = 6371008.8;
    const toRadians = degrees => degrees * Math.PI / 180;
    const latitudeDelta = toRadians(second.latitude - first.latitude);
    const longitudeDelta = toRadians(second.longitude - first.longitude);
    const firstLatitude = toRadians(first.latitude);
    const secondLatitude = toRadians(second.latitude);
    const haversine = Math.sin(latitudeDelta / 2) ** 2
        + Math.cos(firstLatitude) * Math.cos(secondLatitude)
        * Math.sin(longitudeDelta / 2) ** 2;
    return 2 * earthRadiusM * Math.asin(Math.sqrt(haversine));
}

function renderFirePoint(firePoint) {
    if (!firePoint) {
        elements.fireLatitude.textContent = "--";
        elements.fireLongitude.textContent = "--";
        elements.fireDistance.textContent = "--";
        return;
    }
    elements.fireLatitude.textContent = firePoint.latitude.toFixed(6);
    elements.fireLongitude.textContent = firePoint.longitude.toFixed(6);
    renderFirePointDistance(state.latest);
}

function renderFirePointDistance(sample) {
    const distance = haversineDistanceMeters(sample, state.firePoint);
    elements.fireDistance.textContent = distance == null
        ? "--"
        : distance >= 1000
            ? `${(distance / 1000).toFixed(2)} km`
            : `${distance.toFixed(1)} m`;
}

function renderTargets(targets) {
    if (!targets.length) {
        elements.targetList.textContent = "暂无烟雾目标";
        return;
    }
    elements.targetList.replaceChildren(...targets.map(target => {
        const item = document.createElement("div");
        item.className = "target-list-item";
        const title = document.createElement("strong");
        title.textContent = target.label;
        const coordinate = document.createElement("span");
        coordinate.textContent = `${target.latitude.toFixed(5)}, ${target.longitude.toFixed(5)}`;
        const confidence = document.createElement("span");
        confidence.textContent = target.confidence == null
            ? ""
            : `识别准确率 ${Math.round(target.confidence * 100)}%`;
        item.append(title, coordinate, confidence);
        return item;
    }));
}

function consumeSample(sample) {
    const last = state.track.at(-1);
    if (!last || sample.sequence > last.sequence) {
        state.track.push(sample);
        if (state.track.length > 600) {
            state.track.shift();
        }
    }
    elements.trackCount.textContent = state.track.length.toString();
    state.provider?.setTrack(state.track);
    renderTelemetry(sample);
}

function setStreamStatus(text, kind) {
    elements.streamStatus.textContent = text;
    elements.streamStatus.className = `badge text-bg-${kind}`;
}

function setGeolocationStatus(text, kind) {
    elements.geolocationStatus.textContent = text;
    elements.geolocationStatus.className = `badge text-bg-${kind}`;
}

function restoreBaselineMission() {
    state.smokeTargets = [...state.baselineSmokeTargets];
    state.missionWaypoints = [...state.baselineMissionWaypoints];
    elements.smokeCount.textContent = state.smokeTargets.length.toString();
    elements.waypointCount.textContent = state.missionWaypoints.length.toString();
    renderTargets(state.smokeTargets);
    state.provider?.setSmokeTargets(state.smokeTargets);
    state.provider?.setMissionWaypoints(state.missionWaypoints);
}

function resetGeolocation() {
    state.geolocationRequestToken += 1;
    state.geolocationPending = false;
    state.lastGeolocationFetchMs = 0;
    state.hasLocatedTarget = false;
    setGeolocationStatus("等待检测", "secondary");
    elements.geolocationMessage.textContent = "正在等待视频时间与检测结果同步";
    elements.geoVideoTime.textContent = "--";
    elements.geoFrame.textContent = "--";
    elements.geoConfidence.textContent = "--";
    elements.geoReferenceError.textContent = "--";
    elements.geoTelemetryCoordinates.textContent = "--";
    elements.geoTargetCoordinates.textContent = "--";
    restoreBaselineMission();
}

function formatDistance(distance) {
    if (!Number.isFinite(distance)) {
        return "--";
    }
    return distance >= 1000
        ? `${(distance / 1000).toFixed(2)} km`
        : `${distance.toFixed(1)} m`;
}

function renderGeolocation(result) {
    elements.geoVideoTime.textContent = `${result.videoTimeSec.toFixed(2)} s`;
    if (!result.detected) {
        setGeolocationStatus("当前无烟雾", "secondary");
        elements.geolocationMessage.textContent = state.hasLocatedTarget
            ? "当前画面没有检测框，地图保留最近一次定位结果"
            : "当前画面附近没有烟雾检测帧";
        elements.geoFrame.textContent = "--";
        elements.geoConfidence.textContent = "--";
        return;
    }

    elements.geoFrame.textContent = `#${result.frameId} · ${result.detectionTimeSec.toFixed(2)} s`;
    elements.geoConfidence.textContent = `${Math.round(result.confidence * 100)}%`;
    if (!result.geolocated || !result.target) {
        setGeolocationStatus("定位失败", "warning");
        elements.geolocationMessage.textContent = "检测到烟雾，但当前相机射线无法与地面相交";
        return;
    }

    state.hasLocatedTarget = true;
    setGeolocationStatus("定位成功", "success");
    elements.geolocationMessage.textContent = "真实检测框与模拟同步遥测已生成目标和复查航点";
    elements.geoReferenceError.textContent = formatDistance(result.referenceErrorM);
    elements.geoTelemetryCoordinates.textContent =
        `${result.telemetry.latitude.toFixed(6)}, ${result.telemetry.longitude.toFixed(6)}`;
    elements.geoTargetCoordinates.textContent =
        `${result.target.latitude.toFixed(6)}, ${result.target.longitude.toFixed(6)}`;

    state.smokeTargets = [result.target];
    state.missionWaypoints = result.missionWaypoints ?? [];
    elements.smokeCount.textContent = state.smokeTargets.length.toString();
    elements.waypointCount.textContent = state.missionWaypoints.length.toString();
    renderTargets(state.smokeTargets);
    state.provider?.setSmokeTargets(state.smokeTargets);
    state.provider?.setMissionWaypoints(state.missionWaypoints);
}

async function updateVideoGeolocation(force = false) {
    if (!state.currentVideo || elements.videoPlayer.readyState < 1) {
        return;
    }
    const now = performance.now();
    if (!force && (state.geolocationPending
            || now - state.lastGeolocationFetchMs < 750)) {
        return;
    }
    if (force && state.geolocationPending) {
        // 拖动进度条时让新位置优先；旧响应会被请求令牌丢弃。
        state.geolocationRequestToken += 1;
        state.geolocationPending = false;
    }
    state.geolocationPending = true;
    state.lastGeolocationFetchMs = now;
    const requestToken = ++state.geolocationRequestToken;
    const videoId = String(state.currentVideo.id);
    const timeSec = Math.max(0, elements.videoPlayer.currentTime || 0);
    if (force) {
        setGeolocationStatus("同步中", "primary");
    }
    try {
        const query = new URLSearchParams({timeSec: timeSec.toFixed(3)});
        const response = await fetch(
            `/api/videos/${encodeURIComponent(videoId)}/geolocation?${query}`
        );
        const envelope = await response.json().catch(() => null);
        if (!response.ok || !envelope?.success || !envelope.data) {
            throw new Error(envelope?.message || "视频定位接口返回异常");
        }
        if (requestToken !== state.geolocationRequestToken
                || videoId !== String(state.currentVideo?.id)) {
            return;
        }
        renderGeolocation(envelope.data);
    } catch (error) {
        if (requestToken !== state.geolocationRequestToken) {
            return;
        }
        setGeolocationStatus("定位不可用", "danger");
        elements.geolocationMessage.textContent = error.message;
    } finally {
        if (requestToken === state.geolocationRequestToken) {
            state.geolocationPending = false;
        }
    }
}

function showVideoEmpty(title, message) {
    const heading = document.createElement("strong");
    heading.textContent = title;
    const detail = document.createElement("span");
    detail.textContent = message;
    elements.videoEmpty.replaceChildren(heading, detail);
    elements.videoEmpty.classList.remove("d-none");
}

function formatFileSize(bytes) {
    if (!Number.isFinite(bytes) || bytes < 1) {
        return "未知大小";
    }
    const units = ["B", "KB", "MB", "GB"];
    const unitIndex = Math.min(
        Math.floor(Math.log(bytes) / Math.log(1024)),
        units.length - 1
    );
    return `${(bytes / 1024 ** unitIndex).toFixed(unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`;
}

async function loadVideoSources() {
    try {
        const response = await fetch("/api/videos");
        if (!response.ok) {
            throw new Error("视频列表接口返回异常");
        }
        const envelope = await response.json();
        if (!envelope.success || !Array.isArray(envelope.data)) {
            throw new Error(envelope.message || "视频列表数据格式错误");
        }
        state.videos = (envelope.data ?? []).filter(video => video.status === "NORMAL");
        if (!state.videos.length) {
            setStreamStatus("暂无视频", "secondary");
            showVideoEmpty("没有可用视频", "请先在视频库上传 MP4");
            elements.videoSelect.replaceChildren(new Option("暂无上传视频", ""));
            return;
        }

        const options = state.videos.map((video, index) => new Option(
            `视频源 ${index + 1} · ${video.title}`,
            video.id
        ));
        elements.videoSelect.replaceChildren(...options);
        elements.videoSelect.disabled = false;
        selectVideoSource(state.videos[0].id);
    } catch (error) {
        setStreamStatus("读取失败", "danger");
        showVideoEmpty("视频源加载失败", error.message);
    }
}

function selectVideoSource(videoId) {
    const video = state.videos.find(item => String(item.id) === String(videoId));
    if (!video) {
        return;
    }
    state.currentVideo = video;
    resetGeolocation();
    elements.currentVideoTitle.textContent = video.title;
    elements.currentVideoMeta.textContent = `${video.originalFilename} · ${formatFileSize(video.fileSize)} · 循环模拟实时传输`;
    elements.videoEmpty.classList.add("d-none");
    elements.goLiveButton.disabled = true;
    setStreamStatus("正在连接", "warning");
    elements.videoPlayer.src = `/media/videos/${encodeURIComponent(video.id)}`;
    elements.videoPlayer.load();
}

function simulatedLiveTime() {
    const duration = elements.videoPlayer.duration;
    if (!Number.isFinite(duration) || duration <= 0) {
        return 0;
    }
    return (Date.now() / 1000) % duration;
}

async function goToSimulatedLiveEdge() {
    if (!state.currentVideo) {
        return;
    }
    const liveTime = simulatedLiveTime();
    if (Number.isFinite(liveTime)) {
        elements.videoPlayer.currentTime = liveTime;
    }
    elements.goLiveButton.disabled = false;
    try {
        await elements.videoPlayer.play();
        setStreamStatus("模拟直播中", "danger");
    } catch (_error) {
        setStreamStatus("点击播放", "primary");
    }
}

async function initialize() {
    try {
        const [configResponse, snapshotResponse] = await Promise.all([
            fetch("/api/map/config"),
            fetch("/api/telemetry/snapshot")
        ]);
        if (!configResponse.ok || !snapshotResponse.ok) {
            throw new Error("监控接口返回异常");
        }
        const configEnvelope = await configResponse.json();
        const snapshotEnvelope = await snapshotResponse.json();
        const config = configEnvelope.data;
        const snapshot = snapshotEnvelope.data;
        if (!config || !snapshot) {
            throw new Error("监控接口数据为空");
        }
        state.track = snapshot.track ?? [];
        state.firePoint = snapshot.firePoint ?? null;
        state.smokeTargets = snapshot.smokeTargets ?? [];
        state.missionWaypoints = snapshot.missionWaypoints ?? [];
        state.baselineSmokeTargets = [...state.smokeTargets];
        state.baselineMissionWaypoints = [...state.missionWaypoints];
        state.latest = snapshot.latest;
        elements.sourceBadge.textContent = snapshot.simulation ? "模拟数据" : "真实设备";
        elements.trackCount.textContent = state.track.length.toString();
        elements.smokeCount.textContent = state.smokeTargets.length.toString();
        elements.waypointCount.textContent = state.missionWaypoints.length.toString();
        renderFirePoint(state.firePoint);
        renderTargets(state.smokeTargets);
        elements.mapModeSelect.value = selectedModeFor(config);
        await activateMapProvider(config);
        renderTelemetry(state.latest);
        connectTelemetryStream();
        updateVideoGeolocation(true);
    } catch (error) {
        setConnectionStatus("加载失败", "danger");
        showWarning(`实时监控初始化失败：${error.message}`);
        state.provider = new NoMapProvider("监控接口暂不可用");
        await state.provider.init();
    }
}

function connectTelemetryStream() {
    const stream = new EventSource("/api/telemetry/stream");
    stream.addEventListener("open", () => setConnectionStatus("实时连接", "success"));
    stream.addEventListener("telemetry", event => consumeSample(JSON.parse(event.data)));
    stream.addEventListener("error", () => setConnectionStatus("正在重连", "warning"));
}

elements.mapModeSelect.addEventListener("change", switchMapMode);
elements.zoomIn.addEventListener("click", () => state.provider?.zoomIn());
elements.zoomOut.addEventListener("click", () => state.provider?.zoomOut());
elements.recenter.addEventListener("click", () => state.provider?.recenter());
elements.videoSelect.addEventListener("change", event => selectVideoSource(event.target.value));
elements.goLiveButton.addEventListener("click", goToSimulatedLiveEdge);
elements.videoPlayer.addEventListener("loadedmetadata", async () => {
    await goToSimulatedLiveEdge();
    await updateVideoGeolocation(true);
});
elements.videoPlayer.addEventListener("timeupdate", () => updateVideoGeolocation());
elements.videoPlayer.addEventListener("seeked", () => updateVideoGeolocation(true));
elements.videoPlayer.addEventListener("play", () => setStreamStatus("模拟直播中", "danger"));
elements.videoPlayer.addEventListener("pause", () => {
    if (!elements.videoPlayer.ended) {
        setStreamStatus("已暂停", "secondary");
    }
});
elements.videoPlayer.addEventListener("waiting", () => setStreamStatus("缓冲中", "warning"));
elements.videoPlayer.addEventListener("error", () => {
    setStreamStatus("播放失败", "danger");
    showVideoEmpty("视频无法播放", "请检查文件编码或媒体接口");
});
loadVideoSources();
initialize();
