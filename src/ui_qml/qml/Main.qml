pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as NativeDialogs
import QtQuick.Window
// Dvs.Ui is registered by the C++ host before this document is loaded.
// qmllint disable import
import Dvs.Ui 1.0

// qmllint enable import

ApplicationWindow {
    id: root

    width: 1440
    height: 900
    minimumWidth: 960
    minimumHeight: 640
    visible: false
    title: qsTr("VCStation — VideoCompareStation")
    color: "#0d121b"

    readonly property color panelColor: "#171e2a"
    readonly property color raisedPanelColor: "#1d2635"
    readonly property color borderColor: "#303d51"
    readonly property color accentColor: "#4b8df8"
    readonly property color mutedTextColor: "#93a2ba"
    readonly property color primaryTextColor: "#f3f6fb"
    readonly property color errorColor: "#f87171"
    // reviewController is provided by the C++ application context.
    // qmllint disable unqualified
    readonly property var controller: reviewController
    readonly property var preferences: reviewPreferences
    readonly property var workspace: workspaceController
    // qmllint enable unqualified

    property url selectedSourceA: ""
    property url selectedSourceB: ""
    property url selectedSourceC: ""
    // -1 means prediction-only; otherwise the selected source defines the canonical timeline.
    property int referenceSourceIndex: 0
    property bool inspectorOpen: false
    property bool chromeVisible: true
    property int visibilityBeforeFullScreen: Window.Windowed
    property string immersiveHudText: ""
    property bool immersiveHudVisible: false
    property bool manualHudPending: false
    property bool showFramePending: false
    property real wipePosition: 0.5
    property var pendingDroppedVideos: []
    property bool pendingComparisonPreservesPosition: false
    property string dropError: ""
    property string exportMessage: ""
    property var appliedRestoredViewSerial: 0
    property var startupRequestQueue: []
    property var activeStartupRequest: null
    property var pendingDestructiveAction: null
    property bool awaitingGuardSave: false
    property bool guardSaveAs: false
    property bool allowWindowClose: false
    property bool dropRequestExternal: false

    readonly property bool busy: Boolean((controller && controller.busy) || (workspace && workspace.busy))
    readonly property bool framePending: Boolean(controller && controller.framePending)
    readonly property bool projectDirty: Boolean(workspace && workspace.dirty)
    readonly property bool hasProject: Boolean(workspace && workspace.hasProject)
    readonly property bool canSaveProject: Boolean(workspace && workspace.canSave)
    readonly property bool relinkRequired: Boolean(workspace && workspace.relinkRequired)
    readonly property string workspaceError: String(workspace && workspace.errorTechnicalDetail ? workspace.errorTechnicalDetail : "")
    readonly property bool playing: Boolean(controller && controller.playing)
    readonly property bool fullScreen: visibility === Window.FullScreen
    readonly property bool graphicsReady: Boolean(controller && controller.graphicsReady)
    readonly property var currentFrame: controller ? controller.currentFrame : -1
    readonly property var totalFrames: controller ? controller.totalFrames : 0
    readonly property int oneSecondStepFrames: controller ? controller.oneSecondStepFrames : 30
    readonly property string sourceAErrorKey: controller ? controller.sourceAErrorKey : ""
    readonly property string sourceBErrorKey: controller ? controller.sourceBErrorKey : ""
    readonly property string sourceCErrorKey: controller ? controller.sourceCErrorKey : ""
    readonly property bool sourceAMissing: Boolean(!controller || controller.sourceAMissing)
    readonly property bool sourceBMissing: Boolean(!controller || controller.sourceBMissing)
    readonly property bool sourceCMissing: Boolean(!controller || controller.sourceCMissing)
    readonly property int sourceCount: controller ? Number(controller.sourceCount) : 0
    readonly property bool singleMode: sourceCount === 1
    readonly property string pairErrorKey: controller ? controller.pairErrorKey : ""
    readonly property string frameMappingStatus: controller ? controller.frameMappingStatus : ""
    readonly property string alignmentEstimateStatus: controller ? controller.alignmentEstimateStatus : ""
    readonly property string sequenceAlignmentStatus: controller ? controller.sequenceAlignmentStatus : ""
    readonly property bool alignmentAnalysisRunning: Boolean(controller && controller.alignmentAnalysisRunning)
    readonly property string alignmentAnalysisStatus: controller ? controller.alignmentAnalysisStatus : ""
    readonly property string manualAnchorStatus: controller ? controller.manualAnchorStatus : ""
    readonly property var alignmentTimelineMarkers: controller ? controller.alignmentTimelineMarkers : []
    readonly property bool manualAnchorActive: Boolean(controller && controller.manualAnchorActive)
    readonly property bool alignmentRequired: Boolean(controller && controller.alignmentRequired)
    readonly property bool automaticAlignmentPending: Boolean(controller && controller.automaticAlignmentPending)
    readonly property bool canConfirmAutomaticAlignment: Boolean(controller && controller.canConfirmAutomaticAlignment)
    readonly property bool canUndoAutomaticAlignment: Boolean(controller && controller.canUndoAutomaticAlignment)
    readonly property var compatibilityFindings: controller ? controller.compatibilityFindings : []
    readonly property var differenceEdges: controller ? controller.differenceEdges : []
    readonly property string combinedAlignmentStatus: {
        const parts = [];
        if (frameMappingStatus.length > 0)
            parts.push(frameMappingStatus);
        if (alignmentEstimateStatus.length > 0)
            parts.push(alignmentEstimateStatus);
        if (sequenceAlignmentStatus.length > 0)
            parts.push(sequenceAlignmentStatus);
        if (alignmentAnalysisStatus.length > 0)
            parts.push(alignmentAnalysisStatus);
        if (manualAnchorStatus.length > 0)
            parts.push(manualAnchorStatus);
        return parts.join("  |  ");
    }
    readonly property bool autoAlignmentActive: Boolean(controller && controller.autoAlignmentActive)
    readonly property bool hasErrors: sourceAErrorKey.length > 0 || sourceBErrorKey.length > 0 || sourceCErrorKey.length > 0 || pairErrorKey.length > 0 || workspaceError.length > 0
    readonly property bool hasSelectedSourceA: selectedSourceA.toString().length > 0
    readonly property bool hasSelectedSourceB: selectedSourceB.toString().length > 0
    readonly property bool hasSelectedSourceC: selectedSourceC.toString().length > 0
    readonly property string sourceAName: hasSelectedSourceA ? fileName(selectedSourceA) : (controller && controller.sourceAFilename.length > 0 ? controller.sourceAFilename : qsTr("No file selected"))
    readonly property string sourceBName: hasSelectedSourceB ? fileName(selectedSourceB) : (controller && controller.sourceBFilename.length > 0 ? controller.sourceBFilename : qsTr("No file selected"))
    readonly property string sourceCName: hasSelectedSourceC ? fileName(selectedSourceC) : (controller && controller.sourceCFilename.length > 0 ? controller.sourceCFilename : qsTr("Optional third source"))
    readonly property bool canFirstAction: graphicsReady && !busy && Boolean(controller && controller.canFirst)
    readonly property bool canPreviousAction: graphicsReady && !busy && Boolean(controller && controller.canPrevious)
    readonly property bool canNextAction: graphicsReady && !busy && Boolean(controller && controller.canNext)
    readonly property bool canLastAction: graphicsReady && !busy && Boolean(controller && controller.canLast)
    readonly property bool canPlayAction: graphicsReady && !busy && Boolean(controller && controller.canPlay)
    readonly property bool canPauseAction: !busy && Boolean(controller && controller.canPause)
    // ComparisonSurface is a C++ type registered by the host.
    // qmllint disable unqualified
    readonly property int effectiveViewMode: {
        if (singleMode)
            return ComparisonSurface.Single;
        const requested = preferences ? Number(preferences.viewMode) : ComparisonSurface.SideBySide;
        if (sourceCount === 2 && (requested === ComparisonSurface.ThreeUp || requested === ComparisonSurface.ReferenceFocus || requested === ComparisonSurface.AnalysisGrid))
            return ComparisonSurface.SideBySide;
        return requested === ComparisonSurface.Single ? ComparisonSurface.SideBySide : requested;
    }
    readonly property var availableViewModes: sourceCount <= 1 ? [
        {
            "label": qsTr("Single"),
            "value": ComparisonSurface.Single
        }
    ] : sourceCount === 2 ? [
        {
            "label": qsTr("Side by side"),
            "value": ComparisonSurface.SideBySide
        },
        {
            "label": qsTr("Wipe compare"),
            "value": ComparisonSurface.Wipe
        },
        {
            "label": qsTr("Diff"),
            "value": ComparisonSurface.Difference
        }
    ] : [
        {
            "label": qsTr("Side by side"),
            "value": ComparisonSurface.SideBySide
        },
        {
            "label": qsTr("Three up"),
            "value": ComparisonSurface.ThreeUp
        },
        {
            "label": qsTr("Reference focus"),
            "value": ComparisonSurface.ReferenceFocus
        },
        {
            "label": qsTr("Diff"),
            "value": ComparisonSurface.Difference
        },
        {
            "label": qsTr("Analysis grid"),
            "value": ComparisonSurface.AnalysisGrid
        },
        {
            "label": qsTr("Wipe compare"),
            "value": ComparisonSurface.Wipe
        }
    ]
    readonly property bool analysisGridMode: effectiveViewMode === ComparisonSurface.AnalysisGrid
    readonly property bool differenceMode: effectiveViewMode === ComparisonSurface.Difference || analysisGridMode
    readonly property bool wipeMode: effectiveViewMode === ComparisonSurface.Wipe
    readonly property bool sideBySideMode: effectiveViewMode === ComparisonSurface.SideBySide
    readonly property bool threeUpMode: effectiveViewMode === ComparisonSurface.ThreeUp
    // qmllint enable unqualified
    readonly property int differenceEdge: preferences ? Number(preferences.differenceEdge) : 0
    property bool differenceThresholdEnabled: false
    property int differenceThresholdCode: 0
    property int differenceThresholdPolicy: 1
    property bool roiSelecting: false
    property int roiPanel: -1
    property real roiStartX: 0
    property real roiStartY: 0
    property real roiCurrentX: 0
    property real roiCurrentY: 0
    property real panLastX: 0
    property real panLastY: 0
    readonly property var selectedDifferenceEdge: {
        for (const edge of differenceEdges) {
            if (Number(edge.preferenceValue) === differenceEdge)
                return edge;
        }
        return null;
    }
    readonly property int differenceFirstSlot: selectedDifferenceEdge ? Number(selectedDifferenceEdge.firstSourceId) : 0
    readonly property int differenceSecondSlot: selectedDifferenceEdge ? Number(selectedDifferenceEdge.secondSourceId) : 1
    readonly property int selectedDifferenceExactness: selectedDifferenceEdge ? Number(selectedDifferenceEdge.exactness) : 4
    readonly property bool exactPlaneMode: preferences ? Number(preferences.differenceMetric) === 4 : false
    readonly property string differenceUnavailableDetail: {
        if ((!differenceMode && !wipeMode) || currentFrame < 0)
            return "";
        const missing = [];
        if (sourceMissing(differenceFirstSlot))
            missing.push(sourceLabel(differenceFirstSlot));
        if (sourceMissing(differenceSecondSlot))
            missing.push(sourceLabel(differenceSecondSlot));
        if (missing.length > 0)
            return qsTr("Frame %1 cannot be compared because %2 is missing.").arg(Number(currentFrame) + 1).arg(missing.join(qsTr(" and ")));
        if (differenceMode && exactPlaneMode && selectedDifferenceExactness !== 0)
            return qsTr("Exact Plane Diff requires equal dimensions, pixel format, bit depth, color metadata, and ExactIndex mapping.");
        return "";
    }
    property var sourceOffsetValues: ({})
    readonly property bool manualOffsetActive: {
        for (const sourceId of Object.keys(sourceOffsetValues)) {
            if (Number(sourceOffsetValues[sourceId]) !== 0)
                return true;
        }
        return false;
    }
    readonly property bool anyManualAlignmentActive: manualAnchorActive || manualOffsetActive
    property bool timelineDragging: false
    property int timelinePreviewFrame: -1
    readonly property string frameText: timelineDragging && timelinePreviewFrame >= 0 ? qsTr("Frame %1 of %2 (release to seek)").arg(timelinePreviewFrame + 1).arg(totalFrames) : (currentFrame >= 0 && totalFrames > 0 ? qsTr("Frame %1 of %2").arg(currentFrame + 1).arg(totalFrames) : qsTr("No frame displayed"))
    readonly property real frameProgress: currentFrame >= 0 && totalFrames > 1 ? Math.max(0, Math.min(1, Number(currentFrame) / (Number(totalFrames) - 1))) : 0
    readonly property real timelineProgress: timelineDragging && timelinePreviewFrame >= 0 && totalFrames > 1 ? Number(timelinePreviewFrame) / (Number(totalFrames) - 1) : frameProgress
    readonly property bool timelineEnabled: graphicsReady && !busy && Boolean(controller && controller.canFirst) && totalFrames > 0
    readonly property bool globalMediaShortcutsEnabled: !chromeVisible || !focusBlocksGlobalMediaShortcuts(root.activeFocusItem)
    readonly property bool frameErrorBannerVisible: hasErrors && currentFrame >= 0 && !busy && graphicsReady && Boolean(controller && controller.canFirst)
    readonly property string overlayTitle: busy ? qsTr("Loading review...") : (!graphicsReady ? qsTr("Graphics unavailable") : (hasErrors ? qsTr("Unable to open review") : qsTr("Drop one to three videos here")))
    readonly property string overlayDetail: busy ? qsTr("Please wait while the requested media is prepared.") : (!graphicsReady ? qsTr("Navigation and opening are disabled until the graphics device is ready.") : (hasErrors ? errorDetails() : qsTr("Open one video for playback and frame review, or two to three videos for comparison.")))
    readonly property bool overlayVisible: (busy && currentFrame < 0) || !graphicsReady || currentFrame < 0 || (hasErrors && !frameErrorBannerVisible)

    onFramePendingChanged: {
        if (framePending)
            framePendingDelay.restart();
        else {
            framePendingDelay.stop();
            showFramePending = false;
        }
    }

    onCurrentFrameChanged: {
        if (!chromeVisible && manualHudPending && currentFrame >= 0) {
            manualHudPending = false;
            showImmersiveHud(frameText);
        }
    }

    onSourceCountChanged: {
        normalizeViewMode();
    }

    onPlayingChanged: {
        if (!chromeVisible && currentFrame >= 0)
            showImmersiveHud(playing ? qsTr("Playing · %1").arg(frameText) : qsTr("Paused · %1").arg(frameText));
    }

    onClosing: closeEvent => {
        if (allowWindowClose || !hasUnsavedReview())
            return;
        closeEvent.accepted = false;
        requestDestructiveAction({
            "kind": "exit",
            "external": false
        });
    }

    function fileName(fileUrl) {
        const decodedUrl = decodeURIComponent(fileUrl.toString());
        const separator = Math.max(decodedUrl.lastIndexOf("/"), decodedUrl.lastIndexOf("\\"));
        return decodedUrl.substring(separator + 1);
    }

    // ComparisonSurface is runtime-registered by the host.
    // qmllint disable unqualified
    function normalizeViewMode() {
        if (!preferences || sourceCount <= 1)
            return;
        const requested = Number(preferences.viewMode);
        if (sourceCount === 2 && requested !== ComparisonSurface.SideBySide && requested !== ComparisonSurface.Wipe && requested !== ComparisonSurface.Difference)
            preferences.viewMode = ComparisonSurface.SideBySide;
    }
    // qmllint enable unqualified

    function setDroppedVideoOrder(urls) {
        pendingDroppedVideos = urls.slice(0);
    }

    function swapDroppedVideos(first, second) {
        if (first < 0 || second < 0 || first >= pendingDroppedVideos.length || second >= pendingDroppedVideos.length)
            return;
        const next = pendingDroppedVideos.slice(0);
        const temporary = next[first];
        next[first] = next[second];
        next[second] = temporary;
        pendingDroppedVideos = next;
    }

    function droppedUrlError(errorKey, detail) {
        switch (errorKey) {
        case "drop-empty":
            return qsTr("No files were dropped.");
        case "drop-too-many":
            return qsTr("Drop at most three video files.");
        case "drop-invalid-local":
            return qsTr("Only local files can be dropped.");
        case "drop-missing":
            return qsTr("Dropped file is missing: %1").arg(detail);
        case "drop-duplicate":
            return qsTr("The same file was dropped more than once: %1").arg(detail);
        case "drop-mixed":
            return qsTr("Drop one project, or one to three videos—not a mixed set.");
        default:
            return qsTr("The dropped files could not be reviewed.");
        }
    }

    function hasUnsavedReview() {
        return sourceCount > 0 && (!hasProject || projectDirty);
    }

    function clearReviewUi() {
        selectedSourceA = "";
        selectedSourceB = "";
        selectedSourceC = "";
        referenceSourceIndex = 0;
        sourceOffsetValues = {};
        wipePosition = 0.5;
        differenceThresholdEnabled = false;
        differenceThresholdCode = 0;
        if (viewportFrame && viewportFrame.surface)
            viewportFrame.surface.resetViewport();
    }

    function requestDestructiveAction(action) {
        if (pendingDestructiveAction !== null)
            return false;
        pendingDestructiveAction = action;
        if (hasUnsavedReview())
            workspaceDialogs.openUnsaved();
        else
            executePendingDestructiveAction();
        return true;
    }

    function executePendingDestructiveAction() {
        const action = pendingDestructiveAction;
        pendingDestructiveAction = null;
        if (!action)
            return;
        if (action.kind === "openVideos") {
            performVideoReview(action.urls, Boolean(action.external));
            return;
        }
        if (action.kind === "openProject") {
            clearReviewUi();
            if (!workspace.openProject(action.url))
                dropError = qsTr("The review project could not be opened.");
            finishActiveStartupRequest();
            return;
        }
        if (action.kind === "closeReview") {
            if (workspace.closeReview())
                clearReviewUi();
            finishActiveStartupRequest();
            return;
        }
        if (action.kind === "exit") {
            allowWindowClose = true;
            close();
        }
    }

    function cancelPendingDestructiveAction() {
        pendingDestructiveAction = null;
        awaitingGuardSave = false;
        guardSaveAs = false;
        finishActiveStartupRequest();
    }

    function continueAfterGuardSave() {
        awaitingGuardSave = false;
        if (!projectDirty && hasProject && workspaceError.length === 0)
            executePendingDestructiveAction();
        else
            workspaceDialogs.openUnsaved();
    }

    function enqueueStartupRequest(kind, urls) {
        if (kind === 0)
            return true;
        const pendingCount = startupRequestQueue.length + (activeStartupRequest ? 1 : 0);
        if (pendingCount >= 9)
            return false;
        const next = startupRequestQueue.slice(0);
        next.push({
            "kind": Number(kind),
            "urls": Array.from(urls)
        });
        startupRequestQueue = next;
        Qt.callLater(drainStartupRequestQueue);
        return true;
    }

    function drainStartupRequestQueue() {
        if (activeStartupRequest || startupRequestQueue.length === 0 || busy || pendingDestructiveAction || dropReviewDialog.visible || workspaceDialogs.unsavedVisible)
            return;
        activeStartupRequest = startupRequestQueue[0];
        startupRequestQueue = startupRequestQueue.slice(1);
        if (activeStartupRequest.kind === 1)
            requestDestructiveAction({
                "kind": "openProject",
                "url": activeStartupRequest.urls[0],
                "external": true
            });
        else
            requestDestructiveAction({
                "kind": "openVideos",
                "urls": activeStartupRequest.urls,
                "external": true
            });
    }

    function finishActiveStartupRequest() {
        activeStartupRequest = null;
        Qt.callLater(drainStartupRequestQueue);
    }

    function performVideoReview(normalizedUrls, externalRequest) {
        if (normalizedUrls.length === 1) {
            selectedSourceA = normalizedUrls[0];
            selectedSourceB = "";
            selectedSourceC = "";
            sourceOffsetValues = {};
            referenceSourceIndex = 0;
            dropError = "";
            controller.openSources([selectedSourceA], 0);
            finishActiveStartupRequest();
            return;
        }
        dropError = "";
        setDroppedVideoOrder(normalizedUrls);
        pendingComparisonPreservesPosition = false;
        dropRequestExternal = externalRequest;
        dropReviewDialog.open();
    }

    function reviewUrls(urls, allowSingleSourceAppend) {
        const reviewed = controller.handleDroppedUrls(urls);
        if (!reviewed.accepted) {
            dropError = droppedUrlError(reviewed.errorKey, reviewed.detail);
            return;
        }
        const normalizedUrls = reviewed.urls;
        if (reviewed.kind === "project") {
            requestDestructiveAction({
                "kind": "openProject",
                "url": normalizedUrls[0],
                "external": false
            });
            return;
        }
        if (normalizedUrls.length === 1) {
            const existing = selectedSourceUrls();
            if (allowSingleSourceAppend && sourceCount > 0 && existing.length === sourceCount && existing.length < 3) {
                const candidate = normalizedUrls[0].toString();
                for (const current of existing) {
                    if (current.toString() === candidate) {
                        dropError = qsTr("That video is already part of the current review.");
                        return;
                    }
                }
                existing.push(normalizedUrls[0]);
                dropError = "";
                setDroppedVideoOrder(existing);
                pendingComparisonPreservesPosition = true;
                dropRequestExternal = false;
                dropReviewDialog.open();
                return;
            }
            requestDestructiveAction({
                "kind": "openVideos",
                "urls": normalizedUrls,
                "external": false
            });
            return;
        }
        requestDestructiveAction({
            "kind": "openVideos",
            "urls": normalizedUrls,
            "external": false
        });
    }

    function reviewDroppedUrls(urls) {
        reviewUrls(urls, true);
    }

    function openNewReviewUrls(urls) {
        reviewUrls(urls, false);
    }

    function openDroppedComparison(referenceIndex) {
        selectedSourceA = pendingDroppedVideos[0];
        selectedSourceB = pendingDroppedVideos[1];
        selectedSourceC = pendingDroppedVideos.length > 2 ? pendingDroppedVideos[2] : "";
        sourceOffsetValues = {};
        referenceSourceIndex = referenceIndex;
        if (preferences && pendingDroppedVideos.length === 3)
            preferences.viewMode = 1;
        const urls = selectedSourceUrls();
        if (pendingComparisonPreservesPosition && sourceCount > 0)
            controller.reopenSources(urls, referenceSourceIndex);
        else
            controller.openSources(urls, referenceSourceIndex);
    }

    function selectedSourceUrls() {
        const urls = [];
        const loadedUrls = controller && controller.sourceUrls ? controller.sourceUrls : [];
        if (hasSelectedSourceA)
            urls.push(selectedSourceA);
        else if (loadedUrls.length > 0)
            urls.push(loadedUrls[0]);
        if (hasSelectedSourceB)
            urls.push(selectedSourceB);
        else if (loadedUrls.length > 1)
            urls.push(loadedUrls[1]);
        if (hasSelectedSourceC)
            urls.push(selectedSourceC);
        else if (loadedUrls.length > 2)
            urls.push(loadedUrls[2]);
        return urls;
    }

    function openSelectedSources(preservePosition) {
        const urls = selectedSourceUrls();
        if (urls.length === 0)
            return false;
        const canonical = referenceSourceIndex >= 0 && referenceSourceIndex < urls.length ? referenceSourceIndex : 0;
        sourceOffsetValues = {};
        return preservePosition && sourceCount > 0 ? controller.reopenSources(urls, canonical) : controller.openSources(urls, canonical);
    }

    function removeSelectedSource(index) {
        const urls = selectedSourceUrls();
        if (index <= 0 || index >= urls.length)
            return false;
        urls.splice(index, 1);
        selectedSourceA = urls.length > 0 ? urls[0] : "";
        selectedSourceB = urls.length > 1 ? urls[1] : "";
        selectedSourceC = urls.length > 2 ? urls[2] : "";
        if (referenceSourceIndex === index)
            referenceSourceIndex = 0;
        else if (referenceSourceIndex > index)
            referenceSourceIndex -= 1;
        return controller.reopenSources(urls, referenceSourceIndex);
    }

    function showImmersiveHud(message) {
        immersiveHudText = message;
        immersiveHudVisible = true;
        immersiveHudTimer.restart();
    }

    function toggleChrome() {
        chromeVisible = !chromeVisible;
        if (!chromeVisible) {
            inspectorOpen = false;
            viewportFrame.forceActiveFocus();
            Qt.callLater(() => viewportFrame.forceActiveFocus());
            showImmersiveHud(frameText);
        }
    }

    function toggleFullScreen() {
        if (fullScreen) {
            if (visibilityBeforeFullScreen === Window.Maximized)
                showMaximized();
            else
                showNormal();
            return;
        }
        visibilityBeforeFullScreen = visibility;
        showFullScreen();
    }

    function escapePresentationMode() {
        if (fullScreen)
            toggleFullScreen();
        else if (!chromeVisible)
            chromeVisible = true;
    }

    // The surface alias points at the runtime-registered ComparisonSurface.
    // qmllint disable unresolved-type
    function capturePresentationState() {
        if (!workspace)
            return false;
        return workspace.updatePresentationState({
            "wipePosition": wipePosition,
            "thresholdEnabled": differenceThresholdEnabled,
            "threshold": Number(differenceThresholdCode) / 255,
            "centerX": viewportFrame.surface.viewCenterX,
            "centerY": viewportFrame.surface.viewCenterY,
            "scale": viewportFrame.surface.viewScale,
            "roiEnabled": viewportFrame.surface.roiEnabled,
            "roiLeft": viewportFrame.surface.roiLeft,
            "roiTop": viewportFrame.surface.roiTop,
            "roiRight": viewportFrame.surface.roiRight,
            "roiBottom": viewportFrame.surface.roiBottom
        });
    }
    // qmllint enable unresolved-type

    function saveCurrentProject() {
        return capturePresentationState() && workspace.save();
    }

    function saveCurrentProjectAs(projectUrl) {
        return capturePresentationState() && workspace.saveAs(projectUrl);
    }

    function openManualAnchorsDialog() {
        anchorDialog.open();
    }

    function applyRestoredPresentation() {
        if (!workspace || Number(workspace.restoredViewSerial) === Number(appliedRestoredViewSerial))
            return;
        const state = workspace.restoredPresentationState;
        appliedRestoredViewSerial = Number(workspace.restoredViewSerial);
        wipePosition = Number(state.wipePosition);
        differenceThresholdEnabled = Boolean(state.thresholdEnabled);
        differenceThresholdCode = Math.round(Number(state.threshold) * 255);
        viewportFrame.surface.restoreViewport(Number(state.centerX), Number(state.centerY), Number(state.scale), Boolean(state.roiEnabled), Number(state.roiLeft || 0), Number(state.roiTop || 0), Number(state.roiRight || 1), Number(state.roiBottom || 1));
    }

    function sourceLabel(slot) {
        return qsTr("Source %1").arg(String.fromCharCode(65 + slot));
    }

    function sourceMissing(slot) {
        if (slot === 0)
            return sourceAMissing;
        if (slot === 1)
            return sourceBMissing;
        return sourceCMissing;
    }

    function sourceFilename(slot) {
        if (slot === 0)
            return sourceAName;
        if (slot === 1)
            return sourceBName;
        return sourceCName;
    }

    function differenceEdgeIndex(preferenceValue) {
        for (let index = 0; index < differenceEdges.length; ++index) {
            if (Number(differenceEdges[index].preferenceValue) === preferenceValue)
                return index;
        }
        return differenceEdges.length > 0 ? 0 : -1;
    }

    function comparisonExactnessLabel(exactness) {
        if (exactness === 0)
            return qsTr("Pixel-exact");
        if (exactness === 1)
            return qsTr("Display-space converted");
        if (exactness === 2)
            return qsTr("Spatially resampled");
        if (exactness === 3)
            return qsTr("Temporally aligned");
        return qsTr("Unavailable");
    }

    function panelPoint(x, y) {
        let columns = 1;
        let rows = 1;
        const mode = effectiveViewMode;
        // ComparisonSurface enum values are mirrored by ReviewPreferencesController.
        if (mode === 0)
            columns = 2;
        else if (mode === 1)
            columns = 3;
        else if (mode === 2) {
            const left = x < viewportFrame.surface.width / 2;
            const panel = left ? 0 : (y < viewportFrame.surface.height / 2 ? 1 : 2);
            return {
                "panel": panel,
                "x": left ? x / (viewportFrame.surface.width / 2) : (x - viewportFrame.surface.width / 2) / (viewportFrame.surface.width / 2),
                "y": left ? y / viewportFrame.surface.height : (y % (viewportFrame.surface.height / 2)) / (viewportFrame.surface.height / 2)
            };
        } else if (mode === 4) {
            columns = 2;
            rows = 2;
        }
        const panelWidth = viewportFrame.surface.width / columns;
        const panelHeight = viewportFrame.surface.height / rows;
        const column = Math.max(0, Math.min(columns - 1, Math.floor(x / panelWidth)));
        const row = Math.max(0, Math.min(rows - 1, Math.floor(y / panelHeight)));
        return {
            "panel": row * columns + column,
            "x": (x - column * panelWidth) / panelWidth,
            "y": (y - row * panelHeight) / panelHeight
        };
    }

    function sourceOffsets() {
        const offsets = [];
        const sourceIds = Object.keys(sourceOffsetValues).sort((first, second) => Number(first) - Number(second));
        for (const sourceId of sourceIds)
            offsets.push({
                "sourceId": Number(sourceId),
                "frames": Number(sourceOffsetValues[sourceId])
            });
        return offsets;
    }

    function updateSourceOffset(sourceId, frames) {
        const next = {};
        for (const currentSourceId of Object.keys(sourceOffsetValues))
            next[currentSourceId] = sourceOffsetValues[currentSourceId];
        next[String(sourceId)] = Number(frames);
        sourceOffsetValues = next;
    }

    function sourceOffset(sourceId, fallback) {
        const key = String(sourceId);
        return Object.prototype.hasOwnProperty.call(sourceOffsetValues, key) ? Number(sourceOffsetValues[key]) : Number(fallback);
    }

    function resetSourceOffsets() {
        const next = {};
        for (const sourceId of Object.keys(sourceOffsetValues))
            next[sourceId] = 0;
        sourceOffsetValues = next;
    }

    function resetCanonicalSourceOffset() {
        const canonicalSourceId = referenceSourceIndex >= 0 ? referenceSourceIndex : 0;
        updateSourceOffset(canonicalSourceId, 0);
    }

    function frameAtTimelinePosition(position) {
        if (totalFrames <= 1)
            return 0;
        const normalized = Math.max(0, Math.min(1, position));
        return Math.round(normalized * (Number(totalFrames) - 1));
    }

    function alignmentMarkerColor(kind) {
        if (kind === "missing")
            return "#f87171";
        if (kind === "duplicate")
            return "#fb923c";
        if (kind === "extra")
            return "#c084fc";
        if (kind === "anchor")
            return "#22d3ee";
        if (kind === "rejected-segment")
            return "#dc2626";
        if (kind === "review-segment")
            return "#facc15";
        return "#facc15";
    }

    function focusBlocksGlobalMediaShortcuts(item) {
        let candidate = item;
        while (candidate) {
            if (candidate.blocksGlobalMediaShortcuts === true)
                return true;
            candidate = candidate.parent;
        }
        return false;
    }

    function errorDetails() {
        const errors = [];
        if (workspaceError.length > 0)
            errors.push(workspaceError);
        if (sourceAErrorKey.length > 0)
            errors.push(qsTr("Source A: %1").arg(errorMessage(sourceAErrorKey)));
        if (sourceBErrorKey.length > 0)
            errors.push(qsTr("Source B: %1").arg(errorMessage(sourceBErrorKey)));
        if (sourceCErrorKey.length > 0)
            errors.push(qsTr("Source C: %1").arg(errorMessage(sourceCErrorKey)));
        if (pairErrorKey.length > 0)
            errors.push(qsTr("Comparison: %1").arg(errorMessage(pairErrorKey)));
        return errors.join(" | ");
    }

    function errorMessage(errorKey) {
        switch (errorKey) {
        case "invalid-argument":
            return qsTr("The request is invalid.");
        case "invalid-rate":
            return qsTr("The media frame rate is invalid.");
        case "invalid-frame-id":
        case "invalid-frame-range":
            return qsTr("The requested frame is outside the available range.");
        case "invalid-frame-count":
            return qsTr("The media frame count is invalid.");
        case "invalid-dimensions":
            return qsTr("The media dimensions are invalid.");
        case "invalid-duration":
            return qsTr("The media duration is invalid.");
        case "invalid-media-descriptor":
            return qsTr("The media description is incomplete or invalid.");
        case "arithmetic-overflow":
            return qsTr("The media timing values are too large to process safely.");
        case "source-frame-rate-mismatch":
            return qsTr("Source frame rates differ; inspect alignment before comparing.");
        case "source-frame-count-mismatch":
            return qsTr("Source frame counts differ; unavailable mapped frames stay Missing.");
        case "source-duration-mismatch":
            return qsTr("Source durations differ; inspect alignment before comparing.");
        case "source-resolution-mismatch":
            return qsTr("Source resolutions differ; visual comparison is resampled.");
        case "source-color-metadata-mismatch":
            return qsTr("Source color metadata differs; comparison applies color conversion.");
        case "duplicate-identifier":
            return qsTr("A project item uses a duplicate identifier.");
        case "marks-incomplete":
            return qsTr("Both range marks are required.");
        case "marks-reversed":
            return qsTr("The range end must follow its start.");
        case "clip-out-of-range":
            return qsTr("The clip is outside the source range.");
        case "clip-not-found":
            return qsTr("The requested clip could not be found.");
        case "export-record-not-found":
            return qsTr("The requested export could not be found.");
        case "duplicate-clip-selection":
            return qsTr("The same clip cannot be selected more than once.");
        case "invalid-export-mode":
            return qsTr("The selected export mode is invalid.");
        case "invalid-export-geometry":
            return qsTr("The selected export dimensions are invalid.");
        case "unsupported-project-schema":
            return qsTr("This project was created by an unsupported version.");
        case "invalid-project-schema":
            return qsTr("The project file is invalid.");
        case "source-missing":
            return qsTr("The selected source file is missing or cannot be read.");
        case "source-fingerprint-mismatch":
            return qsTr("The source file no longer matches the project.");
        case "project-file-io":
            return qsTr("The project file could not be read or written.");
        case "media-open-failed":
            return qsTr("The media file could not be opened.");
        case "media-probe-failed":
            return qsTr("The media information could not be read.");
        case "invalid-cfr-timing":
            return qsTr("The media does not have supported constant-frame-rate timing.");
        case "unsupported-codec":
            return qsTr("The media codec is not supported.");
        case "unsupported-pixel-format":
            return qsTr("The media pixel format is not supported.");
        case "media-decode-failed":
            return qsTr("A video frame could not be decoded.");
        case "frame-timeline-invalid":
            return qsTr("The video timeline cannot address every frame exactly.");
        case "frame-budget-exceeded":
            return qsTr("The frame memory limit was exceeded.");
        case "graphics-unavailable":
            return qsTr("The graphics device is unavailable.");
        case "graphics-device-lost":
            return qsTr("The graphics device was reset or disconnected.");
        case "frame-presentation-timed-out":
            return qsTr("The requested frame was not displayed in time.");
        default:
            return qsTr("An unexpected media error occurred.");
        }
    }

    function compatibilityDetails() {
        const messages = [];
        for (const finding of compatibilityFindings) {
            const labels = [];
            for (const sourceId of finding.sources)
                labels.push(String.fromCharCode(65 + Number(sourceId)));
            let severity = qsTr("warning");
            if (Number(finding.severity) === 0)
                severity = qsTr("incompatible");
            else if (Number(finding.severity) === 2)
                severity = qsTr("alignment required");
            messages.push(qsTr("%1: %2 — %3").arg(labels.join(" ↔ ")).arg(errorMessage(finding.code)).arg(severity));
        }
        return messages.join(" | ");
    }

    component ActionButton: Button {
        id: control

        property bool blocksGlobalMediaShortcuts: true
        property string helpText: ""

        implicitWidth: Math.max(112, contentItem.implicitWidth + 34)
        implicitHeight: 40
        leftPadding: 17
        rightPadding: 17
        activeFocusOnTab: true

        contentItem: Text {
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: control.text
            color: control.enabled ? root.primaryTextColor : "#637086"
            font.pixelSize: 13
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 5
            color: !control.enabled ? "#202938" : (control.down ? "#285da9" : (control.hovered ? "#2d69bf" : "#253247"))
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? root.accentColor : (control.enabled ? "#3b4d67" : "#2a3444")
        }

        ToolTip.visible: hovered && helpText.length > 0
        ToolTip.delay: 500
        ToolTip.text: helpText
    }

    component OffsetSpinBox: SpinBox {
        id: offsetControl

        from: -16
        to: 16
        editable: true
        implicitWidth: 76
        implicitHeight: 34

        contentItem: TextInput {
            text: offsetControl.textFromValue(offsetControl.value, offsetControl.locale)
            color: offsetControl.enabled ? root.primaryTextColor : root.mutedTextColor
            selectionColor: root.accentColor
            selectedTextColor: "white"
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            readOnly: !offsetControl.editable
            validator: offsetControl.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }

        background: Rectangle {
            radius: 4
            color: root.raisedPanelColor
            border.color: offsetControl.activeFocus ? root.accentColor : root.borderColor
        }
    }

    menuBar: MenuBar {
        visible: root.chromeVisible

        Menu {
            title: qsTr("&File")

            MenuItem {
                text: qsTr("Open new review…")
                onTriggered: videoFilesDialog.open()
            }
            MenuItem {
                text: qsTr("Add source…")
                enabled: root.sourceCount > 0 && root.sourceCount < 3 && !root.busy
                onTriggered: addSourceFileDialog.open()
            }
            MenuItem {
                text: qsTr("Open review project…")
                onTriggered: openProjectDialog.open()
            }
            MenuItem {
                text: qsTr("Close current review")
                enabled: root.sourceCount > 0 && !root.busy
                onTriggered: root.requestDestructiveAction({
                    "kind": "closeReview",
                    "external": false
                })
            }
            MenuSeparator {}
            MenuItem {
                text: root.projectDirty ? qsTr("Save review project *") : qsTr("Save review project")
                enabled: root.canSaveProject && root.hasProject && !root.busy
                onTriggered: root.saveCurrentProject()
            }
            MenuItem {
                text: qsTr("Save review project as…")
                enabled: root.canSaveProject && !root.busy
                onTriggered: saveProjectDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Export Bad Case…")
                enabled: root.currentFrame >= 0 && !root.framePending
                onTriggered: badCaseFolderDialog.open()
            }
        }

        Menu {
            title: qsTr("&Compare")
            enabled: !root.singleMode

            MenuItem {
                text: qsTr("Side by side")
                checked: Number(root.preferences.viewMode) === 0
                checkable: true
                onTriggered: root.preferences.viewMode = 0
            }
            MenuItem {
                text: qsTr("Wipe compare")
                checked: Number(root.preferences.viewMode) === 5
                checkable: true
                onTriggered: root.preferences.viewMode = 5
            }
            MenuItem {
                text: qsTr("Difference")
                checked: Number(root.preferences.viewMode) === 3
                checkable: true
                onTriggered: root.preferences.viewMode = 3
            }
            MenuItem {
                objectName: "analysisGridMenuItem"
                text: qsTr("Analysis grid")
                enabled: root.sourceCount === 3
                checked: Number(root.preferences.viewMode) === 4
                checkable: true
                onTriggered: root.preferences.viewMode = 4
            }
            MenuSeparator {}
            MenuItem {
                text: root.inspectorOpen ? qsTr("Hide Advanced Alignment Inspector") : qsTr("Show Advanced Alignment Inspector")
                onTriggered: root.inspectorOpen = !root.inspectorOpen
            }
        }

        Menu {
            title: qsTr("&Analyze")
            enabled: !root.singleMode

            MenuItem {
                text: qsTr("Estimate global frame offset")
                enabled: root.graphicsReady && !root.busy && !root.alignmentAnalysisRunning && Boolean(root.controller && root.controller.canFirst)
                onTriggered: root.controller.estimateAlignment()
            }
            MenuItem {
                text: root.alignmentAnalysisRunning ? qsTr("Cancel analysis") : qsTr("Analyze missing / duplicate frames")
                enabled: root.graphicsReady && !root.busy && Boolean(root.controller && (root.alignmentAnalysisRunning || root.controller.canFirst))
                onTriggered: root.alignmentAnalysisRunning ? root.controller.cancelAlignmentAnalysis() : root.controller.analyzeSequenceAlignment()
            }
        }

        Menu {
            title: qsTr("&View")

            MenuItem {
                text: root.chromeVisible ? qsTr("Hide interface chrome · Tab") : qsTr("Show interface chrome · Tab")
                onTriggered: root.toggleChrome()
            }
            MenuItem {
                text: root.fullScreen ? qsTr("Exit full screen · F11") : qsTr("Enter full screen · F11")
                onTriggered: root.toggleFullScreen()
            }
        }
    }

    ReviewActions {
        id: reviewActions

        controller: root.controller
        shortcutsEnabled: root.globalMediaShortcutsEnabled
        oneSecondStepFrames: root.oneSecondStepFrames
        wipeEnabled: root.wipeMode
        wipePosition: root.wipePosition
        onWipePositionRequested: position => {
            root.wipePosition = position;
            if (!root.chromeVisible)
                root.showImmersiveHud(qsTr("Wipe %1%").arg(Math.round(position * 100)));
        }
        onManualNavigationRequested: {
            if (!root.chromeVisible)
                root.manualHudPending = true;
        }
    }

    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.canTogglePlayback
        onActivated: reviewActions.togglePlayback()
    }
    Shortcut {
        sequence: "Home"
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.canFirst
        onActivated: reviewActions.firstFrame()
    }
    Shortcut {
        sequence: "End"
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.canLast
        onActivated: reviewActions.lastFrame()
    }
    Shortcut {
        sequences: ["Left", "A"]
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.canPrevious
        onActivated: reviewActions.previousFrame()
    }
    Shortcut {
        sequences: ["Right", "D"]
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.canNext
        onActivated: reviewActions.nextFrame()
    }
    Shortcut {
        sequences: ["Down", "Shift+Left", "Shift+A"]
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.canPrevious
        onActivated: reviewActions.stepBackwardFive()
    }
    Shortcut {
        sequences: ["Up", "Shift+Right", "Shift+D"]
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.canNext
        onActivated: reviewActions.stepForwardFive()
    }
    Shortcut {
        sequences: ["Ctrl+Left", "Ctrl+A"]
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.canPrevious
        onActivated: reviewActions.stepBackwardSecond()
    }
    Shortcut {
        sequences: ["Ctrl+Right", "Ctrl+D"]
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.canNext
        onActivated: reviewActions.stepForwardSecond()
    }
    Shortcut {
        sequence: "Alt+Left"
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.wipeEnabled
        onActivated: reviewActions.moveWipe(-0.01)
    }
    Shortcut {
        sequence: "Alt+Right"
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.wipeEnabled
        onActivated: reviewActions.moveWipe(0.01)
    }
    Shortcut {
        sequence: "Shift+Alt+Left"
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.wipeEnabled
        onActivated: reviewActions.moveWipe(-0.05)
    }
    Shortcut {
        sequence: "Shift+Alt+Right"
        context: Qt.ApplicationShortcut
        enabled: reviewActions.shortcutsEnabled && reviewActions.wipeEnabled
        onActivated: reviewActions.moveWipe(0.05)
    }
    Shortcut {
        sequence: "Tab"
        context: Qt.ApplicationShortcut
        onActivated: root.toggleChrome()
    }
    Shortcut {
        sequence: "F11"
        context: Qt.ApplicationShortcut
        onActivated: root.toggleFullScreen()
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: root.fullScreen || !root.chromeVisible
        onActivated: root.escapePresentationMode()
    }

    Timer {
        id: framePendingDelay

        interval: 140
        repeat: false
        onTriggered: root.showFramePending = root.framePending
    }

    Timer {
        id: exportMessageTimer

        interval: 5000
        repeat: false
        onTriggered: root.exportMessage = ""
    }

    Timer {
        id: immersiveHudTimer

        interval: 800
        repeat: false
        onTriggered: root.immersiveHudVisible = false
    }

    Connections {
        target: root.controller

        function onBadCaseExported(folder) {
            root.exportMessage = qsTr("Bad Case exported to %1").arg(folder);
            exportMessageTimer.restart();
        }

        function onBadCaseExportFailed(detail) {
            root.exportMessage = qsTr("Bad Case export failed: %1").arg(detail);
            exportMessageTimer.restart();
        }
    }

    Connections {
        target: root.preferences

        function onPreferencesChanged() {
            Qt.callLater(root.normalizeViewMode);
        }
    }

    Connections {
        target: root.workspace
        ignoreUnknownSignals: true

        function onStateChanged() {
            root.applyRestoredPresentation();
            if (root.awaitingGuardSave && !root.workspace.busy)
                root.continueAfterGuardSave();
            root.drainStartupRequestQueue();
        }
    }

    WorkspaceDialogs {
        id: workspaceDialogs

        host: root
        onSaveRequested: {
            if (root.hasProject) {
                if (root.saveCurrentProject())
                    root.awaitingGuardSave = true;
                else
                    workspaceDialogs.openUnsaved();
            } else {
                root.guardSaveAs = true;
                saveProjectDialog.open();
            }
        }
        onDiscardRequested: root.executePendingDestructiveAction()
        onCancelRequested: root.cancelPendingDestructiveAction()
    }

    NativeDialogs.FolderDialog {
        id: badCaseFolderDialog

        objectName: "badCaseFolderDialog"
        title: qsTr("Choose a folder for Bad Case evidence")
        onAccepted: {
            if (!root.controller.exportBadCase(viewportFrame.surface, selectedFolder)) {
                root.exportMessage = qsTr("Bad Case export requires a displayed frame and a local folder.");
                exportMessageTimer.restart();
            }
        }
    }

    NativeDialogs.FileDialog {
        id: videoFilesDialog

        objectName: "videoFilesDialog"
        title: qsTr("Open one to three videos")
        fileMode: NativeDialogs.FileDialog.OpenFiles
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: root.openNewReviewUrls(selectedFiles)
    }

    NativeDialogs.FileDialog {
        id: addSourceFileDialog

        objectName: "addSourceFileDialog"
        title: qsTr("Add a source to the current review")
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: root.reviewDroppedUrls([selectedFile])
    }

    DropConfirmationDialog {
        id: dropReviewDialog

        pendingVideos: root.pendingDroppedVideos
        fileNameFunction: root.fileName
        initialReferenceIndex: root.pendingComparisonPreservesPosition ? root.referenceSourceIndex : 0
        onMoveRequested: (fromIndex, toIndex) => root.swapDroppedVideos(fromIndex, toIndex)
        onAccepted: {
            root.openDroppedComparison(referenceIndex);
            if (root.dropRequestExternal)
                root.finishActiveStartupRequest();
            root.dropRequestExternal = false;
        }
        onRejected: {
            if (root.dropRequestExternal)
                root.finishActiveStartupRequest();
            root.dropRequestExternal = false;
        }
    }

    NativeDialogs.FileDialog {
        id: sourceAFileDialog

        objectName: "sourceAFileDialog"
        title: qsTr("Select source A")
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: root.selectedSourceA = selectedFile
    }

    NativeDialogs.FileDialog {
        id: sourceBFileDialog

        objectName: "sourceBFileDialog"
        title: qsTr("Select source B")
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: root.selectedSourceB = selectedFile
    }

    NativeDialogs.FileDialog {
        id: sourceCFileDialog

        objectName: "sourceCFileDialog"
        title: qsTr("Select optional source C")
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: root.selectedSourceC = selectedFile
    }

    NativeDialogs.FileDialog {
        id: openProjectDialog

        objectName: "openProjectDialog"
        title: qsTr("Open review project")
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: [qsTr("VCStation projects (*.dvsproj)"), qsTr("All files (*)")]
        onAccepted: {
            root.requestDestructiveAction({
                "kind": "openProject",
                "url": selectedFile,
                "external": false
            });
        }
    }

    NativeDialogs.FileDialog {
        id: saveProjectDialog

        objectName: "saveProjectDialog"
        title: qsTr("Save review project as")
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: "dvsproj"
        nameFilters: [qsTr("VCStation projects (*.dvsproj)")]
        onAccepted: {
            const guardSave = root.guardSaveAs;
            root.guardSaveAs = false;
            if (root.saveCurrentProjectAs(selectedFile)) {
                if (guardSave)
                    root.awaitingGuardSave = true;
            } else if (guardSave) {
                workspaceDialogs.openUnsaved();
            }
        }
        onRejected: {
            if (root.guardSaveAs) {
                root.guardSaveAs = false;
                root.cancelPendingDestructiveAction();
            }
        }
    }

    NativeDialogs.FileDialog {
        id: relinkSourceDialog

        objectName: "relinkSourceDialog"
        title: qsTr("Relink missing or changed source")
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: root.workspace.relinkSource(root.workspace.nextRelinkSourceId, selectedFile)
    }

    Dialog {
        id: anchorDialog

        objectName: "manualAnchorDialog"
        width: 460
        modal: true
        title: qsTr("Manual alignment anchor")
        closePolicy: Popup.CloseOnEscape
        anchors.centerIn: parent

        property var sourceChoices: {
            const choices = [];
            const count = root.hasSelectedSourceC ? 3 : 2;
            for (let index = 0; index < count; ++index) {
                if (index !== root.referenceSourceIndex)
                    choices.push({
                        "label": String.fromCharCode(65 + index),
                        "sourceIndex": index
                    });
            }
            return choices;
        }

        onOpened: {
            canonicalAnchorFrame.value = Math.max(1, root.currentFrame + 1);
            sourceAnchorFrame.value = canonicalAnchorFrame.value;
        }

        background: Rectangle {
            radius: 8
            color: root.panelColor
            border.color: root.borderColor
        }

        contentItem: Column {
            spacing: 14

            Text {
                text: qsTr("Map one canonical frame to a frame in a non-reference source. Multiple anchors remain monotone.")
                color: root.mutedTextColor
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Row {
                spacing: 12

                Text {
                    text: qsTr("Source")
                    color: root.primaryTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                ComboBox {
                    id: anchorSource

                    objectName: "anchorSourceCombo"
                    width: 90
                    model: anchorDialog.sourceChoices
                    textRole: "label"
                    valueRole: "sourceIndex"
                }

                Text {
                    text: qsTr("Canonical frame")
                    color: root.primaryTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                SpinBox {
                    id: canonicalAnchorFrame

                    objectName: "canonicalAnchorFrame"
                    from: 1
                    to: Math.max(1, Math.min(2147483647, root.totalFrames))
                    editable: true
                }
            }

            Row {
                spacing: 12

                Text {
                    text: qsTr("Source frame")
                    color: root.primaryTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                SpinBox {
                    id: sourceAnchorFrame

                    objectName: "sourceAnchorFrame"
                    from: 1
                    to: Math.max(1, Math.min(2147483647, root.totalFrames + 16))
                    editable: true
                }

                ActionButton {
                    objectName: "addManualAnchorButton"
                    text: qsTr("Add / replace")
                    enabled: !root.busy && anchorSource.currentIndex >= 0
                    onClicked: {
                        if (root.controller.setManualAlignmentAnchor(anchorSource.currentValue, canonicalAnchorFrame.value - 1, sourceAnchorFrame.value - 1))
                            anchorDialog.close();
                    }
                }

                ActionButton {
                    objectName: "clearManualAnchorsButton"
                    text: qsTr("Clear all")
                    enabled: !root.busy && root.manualAnchorActive
                    onClicked: {
                        if (root.controller.clearManualAlignmentAnchors())
                            anchorDialog.close();
                    }
                }
            }

            Text {
                text: root.manualAnchorStatus.length > 0 ? root.manualAnchorStatus : qsTr("No manual anchors")
                color: root.manualAnchorActive ? "#efbf83" : root.mutedTextColor
                wrapMode: Text.WordWrap
                width: parent.width
            }
        }
    }

    Rectangle {
        id: commandBar

        height: root.chromeVisible ? 48 : 0
        visible: root.chromeVisible
        color: root.panelColor
        border.color: root.borderColor
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }

        Row {
            spacing: 12
            anchors {
                left: parent.left
                leftMargin: 20
                verticalCenter: parent.verticalCenter
            }

            Text {
                color: root.primaryTextColor
                font.bold: true
                font.pixelSize: 18
                text: qsTr("VCSTATION")
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                visible: root.width >= 1160
                width: 1
                height: 24
                color: root.borderColor
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                visible: root.width >= 1160
                color: root.mutedTextColor
                font.pixelSize: 13
                text: qsTr("VideoCompareStation · frame-exact review")
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row {
            id: projectControls

            spacing: 8
            anchors {
                right: graphicsStatusBadge.left
                rightMargin: 14
                verticalCenter: parent.verticalCenter
            }

            ActionButton {
                objectName: "openProjectButton"
                text: qsTr("Open project")
                visible: false
                enabled: !root.busy
                onClicked: openProjectDialog.open()
            }

            ActionButton {
                objectName: "saveProjectButton"
                text: root.projectDirty ? qsTr("Save *") : qsTr("Save")
                visible: false
                enabled: root.canSaveProject && root.hasProject && !root.busy
                onClicked: root.saveCurrentProject()
            }

            ActionButton {
                objectName: "saveProjectAsButton"
                text: qsTr("Save review project as")
                visible: false
                enabled: root.canSaveProject && !root.busy
                onClicked: saveProjectDialog.open()
            }

            ActionButton {
                objectName: "relinkSourceButton"
                text: qsTr("Relink source")
                visible: root.relinkRequired
                enabled: root.relinkRequired && !root.busy
                onClicked: relinkSourceDialog.open()
            }

            ActionButton {
                objectName: "openVideosButton"
                implicitWidth: 126
                implicitHeight: 36
                text: qsTr("Open videos…")
                helpText: qsTr("Choose one to three videos. A single video opens directly; comparisons confirm A/B/C order and Reference.")
                enabled: root.graphicsReady && !root.busy
                onClicked: videoFilesDialog.open()
            }

            ActionButton {
                objectName: "advancedInspectorButton"
                implicitWidth: 142
                implicitHeight: 36
                visible: !root.singleMode
                text: root.inspectorOpen ? qsTr("Hide Inspector") : qsTr("Advanced Inspector")
                helpText: qsTr("Show frame offsets, automatic alignment, sequence analysis, and manual anchors.")
                enabled: root.graphicsReady
                onClicked: root.inspectorOpen = !root.inspectorOpen
            }
        }

        Rectangle {
            id: graphicsStatusBadge

            implicitWidth: graphicsStatus.implicitWidth + 20
            implicitHeight: 28
            radius: 14
            color: root.graphicsReady ? "#173c31" : "#3d2c1e"
            border.color: root.graphicsReady ? "#2c7a61" : "#79522d"
            anchors {
                right: parent.right
                rightMargin: 12
                verticalCenter: parent.verticalCenter
            }

            Text {
                id: graphicsStatus

                anchors.centerIn: parent
                color: root.graphicsReady ? "#8ce2c2" : "#efbf83"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                text: root.graphicsReady ? qsTr("Graphics ready") : qsTr("Graphics unavailable")
            }
        }
    }

    SourceBar {
        id: sourceBar

        height: root.chromeVisible ? 68 : 0
        visible: root.chromeVisible
        sourceNames: [root.sourceAName, root.sourceBName, root.sourceCName]
        sourceSelected: [root.hasSelectedSourceA, root.hasSelectedSourceB, root.hasSelectedSourceC]
        sourceErrors: [root.sourceAErrorKey.length > 0, root.sourceBErrorKey.length > 0, root.sourceCErrorKey.length > 0]
        referenceSourceIndex: root.referenceSourceIndex
        sourceCount: root.sourceCount
        graphicsReady: root.graphicsReady
        busy: root.busy
        canOpen: Boolean(root.controller && root.controller.canOpen)
        borderColor: root.borderColor
        accentColor: root.accentColor
        textColor: root.primaryTextColor
        mutedTextColor: root.mutedTextColor
        errorColor: root.errorColor
        anchors {
            top: commandBar.bottom
            left: parent.left
            right: parent.right
        }
        onBrowseRequested: sourceIndex => {
            if (sourceIndex === 0)
                sourceAFileDialog.open();
            else if (sourceIndex === 1)
                sourceBFileDialog.open();
            else
                sourceCFileDialog.open();
        }
        onRemoveRequested: sourceIndex => root.removeSelectedSource(sourceIndex)
        onOpenRequested: root.openSelectedSources(root.sourceCount > 0)
    }
    ComparisonToolbar {
        id: comparisonBar

        host: root
        anchors {
            top: sourceBar.bottom
            left: parent.left
            right: parent.right
        }
    }
    AlignmentInspector {
        id: alignmentBar

        host: root
        visible: root.chromeVisible && root.inspectorOpen && !root.singleMode
        z: 20
        anchors {
            top: parent.top
            topMargin: commandBar.height + sourceBar.height + comparisonBar.height
            right: parent.right
            bottom: parent.bottom
            bottomMargin: transport.height
        }
    }
    ComparisonViewport {
        id: viewportFrame

        host: root
        anchors {
            top: parent.top
            topMargin: root.chromeVisible ? commandBar.height + sourceBar.height + comparisonBar.height + 10 : 0
            bottom: parent.bottom
            bottomMargin: root.chromeVisible ? transport.height + 12 : 0
            left: parent.left
            leftMargin: root.chromeVisible ? 14 : 0
            right: alignmentBar.visible ? alignmentBar.left : parent.right
            rightMargin: root.chromeVisible ? 14 : 0
        }
    }
    TimelineBar {
        id: transport

        host: root
        actions: reviewActions
        focusTarget: viewportFrame
    }
    DropArea {
        id: workspaceDropArea

        objectName: "workspaceDropArea"
        anchors.fill: parent
        z: 1000
        onEntered: drag => {
            if (drag.hasUrls)
                drag.acceptProposedAction();
        }
        onDropped: drop => {
            drop.acceptProposedAction();
            root.reviewDroppedUrls(drop.urls);
        }

        Rectangle {
            anchors.fill: parent
            visible: workspaceDropArea.containsDrag
            color: "#df0b1421"
            border.width: 3
            border.color: root.accentColor

            Column {
                spacing: 10
                anchors.centerIn: parent

                Text {
                    text: qsTr("Drop to review in VCStation")
                    color: root.primaryTextColor
                    font.pixelSize: 24
                    font.bold: true
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: qsTr("1–3 videos, or one .dvsproj project")
                    color: root.mutedTextColor
                    font.pixelSize: 14
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    Rectangle {
        visible: root.dropError.length > 0
        z: 1100
        width: Math.min(parent.width - 48, 620)
        height: dropErrorText.implicitHeight + 26
        radius: 7
        color: "#f0351f2a"
        border.color: "#a9503f4a"
        anchors {
            top: parent.top
            topMargin: 18
            horizontalCenter: parent.horizontalCenter
        }

        Text {
            id: dropErrorText

            width: parent.width - 34
            text: root.dropError
            color: "#ffb4b4"
            wrapMode: Text.WordWrap
            anchors.centerIn: parent
        }

        TapHandler {
            onTapped: root.dropError = ""
        }
    }
}
