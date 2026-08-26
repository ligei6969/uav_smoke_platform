(() => {
    "use strict";

    const panel = document.getElementById("analysis-panel");
    if (!panel) {
        return;
    }

    const videoId = panel.dataset.videoId;
    const apiUrl = `/api/videos/${videoId}/analysis`;
    const statusElement = document.getElementById("analysis-status");
    const actionButton = document.getElementById("analysis-action");
    const progress = document.getElementById("analysis-progress");
    const progressText = document.getElementById("analysis-progress-text");
    const failure = document.getElementById("analysis-failure");
    const errorMessage = document.getElementById("analysis-error-message");
    const requestError = document.getElementById("analysis-request-error");
    const resultBlock = document.getElementById("analysis-result");
    const annotatedVideo = document.getElementById("annotated-video");
    const keyframePreview = document.getElementById("keyframe-preview");
    const keyframeButtons = [
        document.getElementById("keyframe-first"),
        document.getElementById("keyframe-max"),
        document.getElementById("keyframe-last")
    ];
    let currentStatus = "NOT_STARTED";
    let pollTimer = null;
    let renderedAttemptId = null;

    const statusLabels = {
        NOT_STARTED: "未开始",
        PENDING: "等待中",
        RUNNING: "分析中",
        COMPLETED: "已完成",
        FAILED: "失败"
    };

    const badgeClasses = {
        NOT_STARTED: "text-bg-secondary",
        PENDING: "text-bg-warning",
        RUNNING: "text-bg-primary",
        COMPLETED: "text-bg-success",
        FAILED: "text-bg-danger"
    };

    async function request(url, options = {}) {
        const response = await fetch(url, {
            ...options,
            headers: {"Accept": "application/json", ...(options.headers || {})}
        });
        const payload = await response.json().catch(() => null);
        if (!response.ok || !payload?.success) {
            throw new Error(payload?.message || `请求失败（${response.status}）`);
        }
        return payload.data;
    }

    async function loadAnalysis() {
        try {
            const analysis = await request(apiUrl);
            requestError.hidden = true;
            render(analysis);
        } catch (error) {
            requestError.textContent = error.message;
            requestError.hidden = false;
            if (currentStatus === "PENDING" || currentStatus === "RUNNING") {
                schedulePoll();
            }
        }
    }

    function render(analysis) {
        currentStatus = analysis.status;
        statusElement.textContent = statusLabels[analysis.status] || analysis.status;
        statusElement.className = `badge ${badgeClasses[analysis.status] || "text-bg-secondary"}`;
        progress.hidden = !["PENDING", "RUNNING"].includes(analysis.status);
        failure.hidden = analysis.status !== "FAILED";
        resultBlock.hidden = analysis.status !== "COMPLETED";
        actionButton.hidden = !["NOT_STARTED", "FAILED"].includes(analysis.status);
        actionButton.disabled = false;

        if (analysis.status === "NOT_STARTED") {
            actionButton.textContent = "开始分析";
        } else if (analysis.status === "FAILED") {
            actionButton.textContent = "重新分析";
            errorMessage.textContent = analysis.errorMessage || "烟雾分析未能完成";
        } else if (analysis.status === "PENDING") {
            progressText.textContent = "任务已提交，正在等待推理资源";
            schedulePoll();
        } else if (analysis.status === "RUNNING") {
            progressText.textContent = "正在逐帧检测烟雾";
            schedulePoll();
        } else if (analysis.status === "COMPLETED") {
            renderResult(analysis);
        }
    }

    async function submitAnalysis() {
        actionButton.disabled = true;
        requestError.hidden = true;
        const retry = currentStatus === "FAILED";
        try {
            const analysis = await request(retry ? `${apiUrl}/retry` : apiUrl, {
                method: "POST"
            });
            render(analysis);
        } catch (error) {
            requestError.textContent = error.message;
            requestError.hidden = false;
            actionButton.disabled = false;
            await loadAnalysis();
        }
    }

    function renderResult(analysis) {
        const banner = document.getElementById("smoke-result-banner");
        banner.classList.toggle("smoke-positive", analysis.smokeDetected === true);
        banner.textContent = analysis.smokeDetected
            ? "检测到烟雾目标"
            : "未检测到烟雾目标";

        setText("stat-duration", seconds(analysis.durationSec));
        setText("stat-frames", integer(analysis.frameCount));
        setText("stat-positive-frames", integer(analysis.positiveFrameCount));
        setText("stat-positive-ratio", percent(analysis.positiveFrameRatio));
        setText("stat-max-confidence", percent(analysis.maxConfidence));
        setText("stat-mean-confidence", percent(analysis.meanConfidence));
        setText("stat-first-detection", seconds(analysis.firstDetectionSec));
        setText("stat-last-detection", seconds(analysis.lastDetectionSec));

        const videoBlock = document.getElementById("annotated-video-block");
        videoBlock.hidden = !analysis.annotatedVideoUrl;
        if (
            analysis.annotatedVideoUrl
            && annotatedVideo.dataset.source !== analysis.annotatedVideoUrl
        ) {
            annotatedVideo.src = analysis.annotatedVideoUrl;
            annotatedVideo.dataset.source = analysis.annotatedVideoUrl;
            annotatedVideo.load();
        }

        const detectionLink = document.getElementById("detection-results-link");
        detectionLink.hidden = !analysis.detectionResultsUrl;
        if (analysis.detectionResultsUrl) {
            detectionLink.href = analysis.detectionResultsUrl;
        }

        renderKeyframes(analysis);
    }

    function renderKeyframes(analysis) {
        const definitions = [
            {
                button: keyframeButtons[0],
                url: analysis.firstKeyframeUrl,
                time: analysis.firstDetectionSec
            },
            {
                button: keyframeButtons[1],
                url: analysis.maxKeyframeUrl,
                time: null
            },
            {
                button: keyframeButtons[2],
                url: analysis.lastKeyframeUrl,
                time: analysis.lastDetectionSec
            }
        ];

        for (const definition of definitions) {
            configureKeyframeButton(definition);
        }

        const available = definitions.filter(({url}) => Boolean(url));
        const keyframesBlock = document.getElementById("keyframes-block");
        keyframesBlock.hidden = available.length === 0;
        if (available.length === 0) {
            keyframePreview.removeAttribute("src");
            return;
        }

        selectKeyframe(available[0].button, false);
        renderedAttemptId = analysis.attemptId;
        if (analysis.detectionResultsUrl && analysis.maxKeyframeUrl) {
            loadMaxConfidenceTime(
                analysis.detectionResultsUrl,
                analysis.attemptId
            );
        }
    }

    function configureKeyframeButton({button, url, time}) {
        button.hidden = !url;
        button.classList.remove("active");
        button.setAttribute("aria-selected", "false");
        if (!url) {
            button.disabled = true;
            delete button.dataset.url;
            delete button.dataset.time;
            return;
        }

        button.dataset.url = url;
        setButtonTime(button, time);
    }

    function setButtonTime(button, value) {
        const time = Number(value);
        const hasTime = Number.isFinite(time) && time >= 0;
        if (hasTime) {
            button.dataset.time = String(time);
        } else {
            delete button.dataset.time;
        }
        button.disabled = !hasTime;
        button.querySelector(".keyframe-time").textContent = hasTime
            ? seconds(time)
            : "正在读取定位时间";
    }

    function selectKeyframe(button, seekVideo) {
        if (!button || button.hidden || !button.dataset.url) {
            return;
        }

        for (const candidate of keyframeButtons) {
            const active = candidate === button;
            candidate.classList.toggle("active", active);
            candidate.setAttribute("aria-selected", String(active));
        }
        keyframePreview.src = button.dataset.url;
        keyframePreview.alt = `${button.dataset.label}关键帧`;

        if (seekVideo && button.dataset.time != null) {
            seekAnnotatedVideo(Number(button.dataset.time));
        }
    }

    function seekAnnotatedVideo(time) {
        if (!Number.isFinite(time) || time < 0 || !annotatedVideo.dataset.source) {
            return;
        }

        const applySeek = () => {
            const upperBound = Number.isFinite(annotatedVideo.duration)
                ? Math.max(0, annotatedVideo.duration - 0.01)
                : time;
            annotatedVideo.pause();
            annotatedVideo.currentTime = Math.min(time, upperBound);
            document.getElementById("annotated-video-block").scrollIntoView({
                behavior: "smooth",
                block: "start"
            });
        };

        if (annotatedVideo.readyState >= HTMLMediaElement.HAVE_METADATA) {
            applySeek();
        } else {
            annotatedVideo.addEventListener("loadedmetadata", applySeek, {once: true});
            annotatedVideo.load();
        }
    }

    async function loadMaxConfidenceTime(url, attemptId) {
        try {
            const response = await fetch(url, {headers: {"Accept": "application/json"}});
            if (!response.ok) {
                throw new Error(`检测结果读取失败（${response.status}）`);
            }
            const payload = await response.json();
            if (renderedAttemptId !== attemptId) {
                return;
            }
            const time = maxConfidenceTime(payload?.detections);
            setButtonTime(keyframeButtons[1], time);
        } catch (_error) {
            if (renderedAttemptId === attemptId) {
                keyframeButtons[1].disabled = true;
                keyframeButtons[1].querySelector(".keyframe-time").textContent =
                    "定位时间不可用";
            }
        }
    }

    function maxConfidenceTime(detections) {
        let bestConfidence = Number.NEGATIVE_INFINITY;
        let bestTime = null;
        for (const frame of Array.isArray(detections) ? detections : []) {
            for (const box of Array.isArray(frame?.boxes) ? frame.boxes : []) {
                const confidence = Number(box?.confidence);
                const time = Number(frame?.timestamp_sec);
                if (
                    Number.isFinite(confidence)
                    && Number.isFinite(time)
                    && confidence > bestConfidence
                ) {
                    bestConfidence = confidence;
                    bestTime = time;
                }
            }
        }
        return bestTime;
    }

    function setText(id, value) {
        document.getElementById(id).textContent = value;
    }

    function integer(value) {
        return value == null ? "--" : Number(value).toLocaleString("zh-CN");
    }

    function percent(value) {
        return value == null ? "--" : `${(Number(value) * 100).toFixed(2)}%`;
    }

    function seconds(value) {
        return value == null ? "--" : `${Number(value).toFixed(2)} 秒`;
    }

    function schedulePoll() {
        window.clearTimeout(pollTimer);
        pollTimer = window.setTimeout(loadAnalysis, 2500);
    }

    actionButton.addEventListener("click", submitAnalysis);
    for (const button of keyframeButtons) {
        button.addEventListener("click", () => selectKeyframe(button, true));
    }
    loadAnalysis();
})();
