pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
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

    palette {
        window: root.panelColor
        windowText: root.primaryTextColor
        base: root.raisedPanelColor
        alternateBase: root.panelColor
        text: root.primaryTextColor
        button: root.raisedPanelColor
        buttonText: root.primaryTextColor
        highlight: root.accentColor
        highlightedText: "#ffffff"
        toolTipBase: root.raisedPanelColor
        toolTipText: root.primaryTextColor
    }

    // reviewController is provided by the C++ application context.
    // qmllint disable unqualified
    readonly property var controller: reviewController
    readonly property var preferences: reviewPreferences
    readonly property var shell: reviewSession
    // qmllint enable unqualified

    readonly property int referenceSourceIndex: controller ? Number(controller.referenceSourceIndex) : -1
    readonly property int canonicalSourceIndex: controller ? Number(controller.canonicalSourceIndex) : -1
    readonly property bool inspectorOpen: shell ? Boolean(shell.inspectorVisible) : false
    readonly property bool chromeVisible: shell ? Boolean(shell.chromeVisible) : true
    property int visibilityBeforeFullScreen: Window.Windowed
    property string immersiveHudText: ""
    property bool immersiveHudVisible: false
    property bool manualHudPending: false
    property bool showFramePending: false
    property real wipePosition: 0.5
    property bool pendingComparisonPreservesPosition: false
    property bool pendingNewReviewWantsThreeUp: false
    property string dropError: ""
    property string intentMessage: ""
    readonly property var pendingDestructiveAction: shell && shell.hasPendingAction ? shell.pendingAction : null
    readonly property int inFrame: shell ? Number(shell.inFrame) : -1
    readonly property int outFrame: shell ? Number(shell.outFrame) : -1
    readonly property real inMediaTime: shell ? Number(shell.inMediaTime) : -1
    readonly property real outMediaTime: shell ? Number(shell.outMediaTime) : -1
    readonly property bool rangePlaybackActive: Boolean(shell && shell.rangePlaybackActive)
    readonly property bool rangeStartPending: Boolean(shell && shell.rangeStartPending)
    property bool shortcutHelpVisible: false
    readonly property int shortcutPreset: preferences ? Number(preferences.shortcutPreset) : 0
    readonly property bool dropFrameTimecode: Boolean(preferences && preferences.dropFrameTimecode)

    readonly property bool busy: Boolean(controller && controller.busy)
    readonly property bool framePending: Boolean(controller && controller.framePending)
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
    readonly property int preferredOscState: preferences ? Number(preferences.oscMode) : -1
    readonly property int oscState: sourceCount === 0 || !chromeVisible ? 2 : (preferredOscState >= 0 ? preferredOscState : (singleMode ? 1 : 0))
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
    readonly property bool hasErrors: sourceAErrorKey.length > 0 || sourceBErrorKey.length > 0 || sourceCErrorKey.length > 0 || pairErrorKey.length > 0
    readonly property string sourceAName: sourceCount > 0 ? controller.sourceAFilename : qsTr("No file selected")
    readonly property string sourceBName: sourceCount > 1 ? controller.sourceBFilename : qsTr("No file selected")
    readonly property string sourceCName: sourceCount > 2 ? controller.sourceCFilename : qsTr("Optional third source")
    readonly property bool canFirstAction: graphicsReady && !busy && Boolean(controller && controller.canFirst)
    readonly property bool canPreviousAction: graphicsReady && !busy && Boolean(controller && controller.canPrevious)
    readonly property bool canNextAction: graphicsReady && !busy && Boolean(controller && controller.canNext)
    readonly property bool canLastAction: graphicsReady && !busy && Boolean(controller && controller.canLast)
    readonly property bool canPlayAction: graphicsReady && !busy && Boolean(controller && controller.canPlay)
    readonly property bool canPauseAction: !busy && Boolean(controller && controller.canPause)
    // ComparisonSurface is a C++ type registered by the host.
    // qmllint disable unqualified
    readonly property int effectiveViewMode: shell ? Number(shell.effectiveViewMode) : (singleMode ? ComparisonSurface.Single : ComparisonSurface.SideBySide)
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
    readonly property int threeUpViewMode: ComparisonSurface.ThreeUp
    // qmllint enable unqualified
    readonly property int differenceEdge: shell ? Number(shell.effectiveDifferenceEdge) : 0
    property bool differenceThresholdEnabled: false
    property int differenceThresholdCode: 0
    property int differenceThresholdPolicy: 1
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
    readonly property int inputContext: reviewInputDialogs.modalVisible || anchorDialog.visible || shortcutHelp.visible ? 3 : (focusIsPopup(root.activeFocusItem) ? 2 : (focusIsTextEditing(root.activeFocusItem) ? 1 : 0))
    readonly property bool globalMediaShortcutsEnabled: inputContext === 0 && (!chromeVisible || !focusBlocksGlobalMediaShortcuts(root.activeFocusItem))
    readonly property bool presentationShortcutsEnabled: inputContext === 0
    readonly property bool frameErrorBannerVisible: hasErrors && currentFrame >= 0 && !busy && graphicsReady && Boolean(controller && controller.canFirst)
    readonly property string overlayTitle: busy ? qsTr("Loading videos...") : (!graphicsReady ? qsTr("Graphics unavailable") : (hasErrors ? qsTr("Unable to open videos") : qsTr("Drop one to three videos here")))
    readonly property string overlayDetail: busy ? qsTr("Please wait while the requested media is prepared.") : (!graphicsReady ? qsTr("Navigation and opening are disabled until the graphics device is ready.") : (hasErrors ? errorDetails() : qsTr("Open one video for playback and frame review, or two to three videos for comparison.")))
    readonly property bool overlayVisible: (busy && currentFrame < 0) || !graphicsReady || (hasErrors && !frameErrorBannerVisible)
    readonly property string currentTimecode: controller ? controller.timecodeForFrame(currentFrame, dropFrameTimecode) : "00:00:00:00"
    readonly property string previewTimecode: controller ? controller.timecodeForFrame(Math.max(0, timelinePreviewFrame), dropFrameTimecode) : "00:00:00:00"
    readonly property bool roiEnabled: Boolean(viewportFrame && viewportFrame.roiEnabled)

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
        if (rangeStartPending && Number(currentFrame) === inFrame && !busy) {
            shell.setRangeStartPending(false);
            Qt.callLater(() => {
                if (rangePlaybackActive && controller && !controller.playing && !controller.play())
                    stopRangeLoop(qsTr("Loop playback stopped because playback could not start."));
            });
            return;
        }
        if (rangePlaybackActive && playing && outFrame >= inFrame && Number(currentFrame) >= outFrame) {
            shell.setRangeStartPending(true);
            if (!controller.seekFrame(inFrame))
                stopRangeLoop(qsTr("Loop playback stopped because the In point could not be reached."));
        }
    }

    onSourceCountChanged: {
        Qt.callLater(remapReviewRange);
    }

    onCanonicalSourceIndexChanged: {
        Qt.callLater(remapReviewRange);
        revealOsc();
    }

    onHasErrorsChanged: {
        if (hasErrors)
            revealOsc();
    }

    onPlayingChanged: {
        revealOsc();
        if (!chromeVisible && currentFrame >= 0)
            showImmersiveHud(playing ? qsTr("Playing · %1").arg(frameText) : qsTr("Paused · %1").arg(frameText));
    }

    // Closing the window never prompts to save. Alignment, probe, and decode work are cancelled
    // by the host shutdown path; the review session needs no persistence guard.
    onClosing: {}

    function fileName(fileUrl) {
        const decodedUrl = decodeURIComponent(fileUrl.toString());
        const separator = Math.max(decodedUrl.lastIndexOf("/"), decodedUrl.lastIndexOf("\\"));
        return decodedUrl.substring(separator + 1);
    }

    function setInPoint() {
        if (currentFrame >= 0 && shell) {
            revealOsc();
            const frame = Number(currentFrame);
            const mediaTime = controller ? Number(controller.mediaTimeForFrame(frame)) : -1;
            if (shell.setRangeIn(frame, mediaTime))
                showImmersiveHud(qsTr("In · Frame %1").arg(frame + 1));
        }
    }

    function setOutPoint() {
        if (currentFrame >= 0 && shell) {
            revealOsc();
            const frame = Number(currentFrame);
            const mediaTime = controller ? Number(controller.mediaTimeForFrame(frame)) : -1;
            if (shell.setRangeOut(frame, mediaTime))
                showImmersiveHud(qsTr("Out · Frame %1").arg(frame + 1));
        }
    }

    function playSelectedRange() {
        if (inFrame < 0 || outFrame < inFrame || !controller || !shell)
            return false;
        const seekRequired = Number(currentFrame) !== inFrame;
        if (!shell.setRangePlaybackState(true, seekRequired))
            return false;
        if (seekRequired) {
            if (!controller.seekFrame(inFrame)) {
                stopRangeLoop(qsTr("Loop playback stopped because the In point could not be reached."));
                return false;
            }
        } else if (!controller.play()) {
            stopRangeLoop(qsTr("Loop playback stopped because playback could not start."));
            return false;
        }
        return true;
    }

    function clearSelectedRange() {
        if (shell)
            shell.clearRange();
    }

    function remapReviewRange() {
        if (!controller || !shell || sourceCount === 0)
            return;
        const mappedIn = inMediaTime >= 0 ? Number(controller.frameForMediaTime(inMediaTime)) : -1;
        const mappedOut = outMediaTime >= 0 ? Number(controller.frameForMediaTime(outMediaTime)) : -1;
        shell.remapRange(mappedIn, mappedOut);
    }

    function revealOsc() {
        transport.reveal();
    }

    function toggleRangeLoop() {
        if (inFrame < 0 || outFrame < inFrame || !shell)
            return false;
        const nextActive = !rangePlaybackActive;
        if (!shell.setRangePlaybackState(nextActive, false))
            return false;
        if (!nextActive && controller && controller.playing && !controller.pause()) {
            showIntentMessage(qsTr("Loop playback was disabled, but playback could not be paused."));
            return false;
        }
        return true;
    }

    function stopRangeLoop(message) {
        if (shell) {
            shell.setRangePlaybackState(false, false);
            shell.setRangeStartPending(false);
        }
        const paused = !controller || !controller.playing || controller.pause();
        if (message.length > 0)
            showIntentMessage(message);
        else if (!paused)
            showIntentMessage(qsTr("Loop playback was disabled, but playback could not be paused."));
        return paused;
    }

    function resetViewport() {
        if (viewportFrame && viewportFrame.surface)
            viewportFrame.surface.resetViewport();
    }

    function clearRoi() {
        if (viewportFrame)
            viewportFrame.clearRoi();
    }

    function setDropFrameTimecode(enabled) {
        if (preferences)
            preferences.dropFrameTimecode = Boolean(enabled);
    }

    function openViewerContextMenu() {
        viewerContextMenu.popup();
    }

    function setDroppedVideoOrder(urls) {
        return shell && shell.stageSources(urls.slice(0), 0);
    }

    function swapDroppedVideos(first, second) {
        if (shell)
            shell.moveStagedSource(first, second);
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
        default:
            return qsTr("The dropped videos could not be opened.");
        }
    }

    function resetReviewVisualState() {
        sourceOffsetValues = {};
        if (shell)
            shell.clearRange();
        wipePosition = 0.5;
        differenceThresholdEnabled = false;
        differenceThresholdCode = 0;
        if (viewportFrame && viewportFrame.surface)
            viewportFrame.surface.resetViewport();
    }

    function clearReviewUi() {
        if (shell)
            shell.clearStagedSources();
        resetReviewVisualState();
    }

    function requestDestructiveAction(action) {
        if (!shell || shell.hasPendingAction)
            return false;
        if (!shell.beginPendingAction(action))
            return false;
        executePendingDestructiveAction();
        return true;
    }

    function executePendingDestructiveAction() {
        if (!shell)
            return;
        const action = shell.takePendingAction();
        if (!action || !action.kind)
            return;
        if (action.kind === "openVideos") {
            performVideoReview(action.urls);
            return;
        }
        if (action.kind === "closeReview") {
            shell.closeSources();
            return;
        }
        if (action.kind === "exit")
            close();
    }

    function cancelPendingDestructiveAction() {
        if (shell)
            shell.cancelPendingAction();
    }

    // After a menu action runs, hand keyboard control back to the viewer so transport shortcuts
    // (Space, arrows, I/O) work immediately.
    function returnFocusToViewer() {
        Qt.callLater(() => {
            if (viewportFrame)
                viewportFrame.forceActiveFocus();
        });
    }

    function showIntentMessage(message) {
        intentMessage = String(message);
        intentMessageTimer.restart();
    }

    function enqueueStartupRequest(kind, urls) {
        return shell && shell.enqueueStartupRequest(Number(kind), Array.from(urls));
    }

    function performVideoReview(normalizedUrls) {
        if (normalizedUrls.length === 1) {
            dropError = "";
            if (shell && shell.stageSources(normalizedUrls, 0))
                shell.openStagedSources(false);
            return;
        }
        dropError = "";
        if (!setDroppedVideoOrder(normalizedUrls)) {
            showIntentMessage(qsTr("The videos could not be staged."));
            return;
        }
        pendingComparisonPreservesPosition = false;
        reviewInputDialogs.openComparison();
    }

    function reviewUrls(urls, allowSingleSourceAppend) {
        const reviewed = controller.handleDroppedUrls(urls);
        if (!reviewed.accepted) {
            dropError = droppedUrlError(reviewed.errorKey, reviewed.detail);
            return;
        }
        const normalizedUrls = reviewed.urls;
        if (normalizedUrls.length === 1) {
            const existing = activeSourceUrls();
            if (allowSingleSourceAppend && sourceCount > 0 && existing.length === sourceCount && existing.length < 3) {
                const candidate = normalizedUrls[0].toString();
                for (const current of existing) {
                    if (current.toString() === candidate) {
                        dropError = qsTr("That video is already open.");
                        return;
                    }
                }
                existing.push(normalizedUrls[0]);
                dropError = "";
                setDroppedVideoOrder(existing);
                pendingComparisonPreservesPosition = true;
                reviewInputDialogs.openComparison();
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
        if (shell)
            shell.stagedReferenceIndex = referenceIndex;
        pendingNewReviewWantsThreeUp = Boolean(shell && shell.stagedSources.length === 3);
        if (shell && !shell.openStagedSources(pendingComparisonPreservesPosition && sourceCount > 0))
            pendingNewReviewWantsThreeUp = false;
    }

    function intentKindText(kind, sourceCountValue) {
        switch (Number(kind)) {
        case 0:
            return qsTr("Open %1 video(s)").arg(sourceCountValue);
        case 1:
            return qsTr("Replace videos");
        case 2:
            return qsTr("Add video");
        case 3:
            return qsTr("Remove video");
        case 4:
            return qsTr("Change reference");
        case 5:
            return qsTr("Close videos");
        default:
            return qsTr("Review request");
        }
    }

    function intentErrorText(error) {
        switch (Number(error)) {
        case 2:
            return qsTr("The review request queue is full.");
        case 3:
            return qsTr("The video list changed; please repeat the operation.");
        case 4:
            return qsTr("The review request could not be started.");
        case 5:
            return qsTr("The review request failed.");
        default:
            return qsTr("The review request was rejected.");
        }
    }

    function activeSourceUrls() {
        return shell ? Array.from(shell.activeSources) : [];
    }

    function removeSelectedSource(index) {
        return shell ? shell.removeActiveSource(index) : false;
    }

    function showImmersiveHud(message) {
        immersiveHudText = message;
        immersiveHudVisible = true;
        immersiveHudTimer.restart();
    }

    function toggleChrome() {
        if (!shell)
            return;
        shell.chromeVisible = !chromeVisible;
        if (!shell.chromeVisible) {
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
            shell.chromeVisible = true;
    }

    function openManualAnchorsDialog() {
        anchorDialog.open();
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

    function differenceEdgeIndex(preferenceValue) {
        for (let index = 0; index < differenceEdges.length; ++index) {
            if (Number(differenceEdges[index].preferenceValue) === preferenceValue)
                return index;
        }
        return differenceEdges.length > 0 ? 0 : -1;
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
        const canonicalSourceId = canonicalSourceIndex >= 0 ? canonicalSourceIndex : 0;
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

    function focusIsTextEditing(item) {
        let candidate = item;
        while (candidate) {
            if (candidate.textEditingInputContext === true)
                return true;
            candidate = candidate.parent;
        }
        return false;
    }

    function focusIsPopup(item) {
        let candidate = item;
        while (candidate) {
            if (candidate.popupInputContext === true)
                return true;
            candidate = candidate.parent;
        }
        return false;
    }

    function errorDetails() {
        const errors = [];
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
        case "source-missing":
            return qsTr("The selected source file is missing or cannot be read.");
        case "source-fingerprint-mismatch":
            return qsTr("The source file no longer matches its recorded identity.");
        case "file-io":
            return qsTr("The file could not be read or written.");
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

        property bool blocksGlobalMediaShortcuts: true
        property bool textEditingInputContext: true
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

    menuBar: ApplicationMenuBar {
        visible: root.chromeVisible
        controller: root.controller
        preferences: root.preferences
        session: root.shell
        sourceCount: root.sourceCount
        busy: root.busy
        canonicalSourceIndex: root.canonicalSourceIndex
        currentViewMode: root.effectiveViewMode
        inspectorOpen: root.inspectorOpen
        graphicsReady: root.graphicsReady
        currentFrame: root.currentFrame
        alignmentAnalysisRunning: root.alignmentAnalysisRunning
        chromeVisible: root.chromeVisible
        fullScreen: root.fullScreen
        shortcutPreset: root.shortcutPreset
        onOpenVideosRequested: reviewInputDialogs.openVideos()
        onAddVideoRequested: reviewInputDialogs.openAddVideo()
        onDestructiveActionRequested: kind => root.requestDestructiveAction({
                "kind": kind,
                "external": false
            })
        onChromeToggleRequested: root.toggleChrome()
        onFullScreenToggleRequested: root.toggleFullScreen()
        onViewerFocusRequested: root.returnFocusToViewer()
    }

    ReviewShortcuts {
        id: reviewShortcuts

        controller: root.controller
        shortcutsEnabled: root.globalMediaShortcutsEnabled
        presentationShortcutsEnabled: root.presentationShortcutsEnabled
        oneSecondStepFrames: root.oneSecondStepFrames
        wipeEnabled: root.wipeMode
        wipePosition: root.wipePosition
        shortcutPreset: root.shortcutPreset
        fullScreen: root.fullScreen
        chromeVisible: root.chromeVisible
        currentFrame: root.currentFrame
        inFrame: root.inFrame
        outFrame: root.outFrame
        sourceCount: root.sourceCount
        onWipePositionRequested: position => {
            root.wipePosition = position;
            if (!root.chromeVisible)
                root.showImmersiveHud(qsTr("Wipe %1%").arg(Math.round(position * 100)));
        }
        onManualNavigationRequested: {
            root.revealOsc();
            if (!root.chromeVisible)
                root.manualHudPending = true;
        }
        onChromeToggleRequested: root.toggleChrome()
        onFullScreenToggleRequested: root.toggleFullScreen()
        onPresentationEscapeRequested: root.escapePresentationMode()
        onShortcutHelpRequested: shortcutHelp.open()
        onInPointRequested: root.setInPoint()
        onOutPointRequested: root.setOutPoint()
        onSelectedRangePlaybackRequested: root.playSelectedRange()
        onOpenVideosRequested: reviewInputDialogs.openVideos()
        onAddVideoRequested: reviewInputDialogs.openAddVideo()
        onCloseVideosRequested: root.requestDestructiveAction({
            "kind": "closeReview",
            "external": false
        })
    }

    Timer {
        id: framePendingDelay

        interval: 140
        repeat: false
        onTriggered: root.showFramePending = root.framePending
    }

    Timer {
        id: intentMessageTimer

        interval: 5000
        repeat: false
        onTriggered: root.intentMessage = ""
    }

    Timer {
        id: immersiveHudTimer

        interval: 800
        repeat: false
        onTriggered: root.immersiveHudVisible = false
    }

    Connections {
        target: root.shell

        function onIntentEvent(intentId, status, kind, error, sourceCountValue) {
            if (Number(status) === 5)
                root.showIntentMessage(root.intentErrorText(error));
            else if (Number(status) === 6)
                root.showIntentMessage(qsTr("A newer request replaced %1.").arg(root.intentKindText(kind, sourceCountValue)));
        }

        function onIntentFinished(intentId, kind, outcome, errorKey) {
            if (Number(outcome) !== 0) {
                if (Number(kind) === 0)
                    root.pendingNewReviewWantsThreeUp = false;
                root.showIntentMessage(errorKey.length > 0 ? root.errorMessage(errorKey) : qsTr("The review request failed."));
                return;
            }
            if (Number(kind) === 0) {
                if (root.pendingNewReviewWantsThreeUp && root.sourceCount === 3 && root.preferences)
                    root.preferences.viewMode = root.threeUpViewMode;
                root.pendingNewReviewWantsThreeUp = false;
                root.resetReviewVisualState();
            } else if (Number(kind) === 5)
                root.clearReviewUi();
        }
    }

    ReviewInputDialogs {
        id: reviewInputDialogs

        stagedVideos: root.shell ? root.shell.stagedSources : []
        fileNameFunction: root.fileName
        initialReferenceIndex: root.pendingComparisonPreservesPosition ? root.canonicalSourceIndex : 0
        onOpenVideosAccepted: urls => root.openNewReviewUrls(urls)
        onAddVideoAccepted: url => root.reviewDroppedUrls([url])
        onMoveRequested: (fromIndex, toIndex) => root.swapDroppedVideos(fromIndex, toIndex)
        onComparisonAccepted: referenceIndex => {
            root.openDroppedComparison(referenceIndex);
        }
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
            const count = root.sourceCount;
            for (let index = 0; index < count; ++index) {
                if (index !== root.canonicalSourceIndex)
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

                    property bool blocksGlobalMediaShortcuts: true
                    property bool popupInputContext: popup.visible
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

                    property bool blocksGlobalMediaShortcuts: true
                    property bool textEditingInputContext: true
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

                    property bool blocksGlobalMediaShortcuts: true
                    property bool textEditingInputContext: true
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

    ActiveSourceStrip {
        id: sourceBar

        sourcesModel: root.controller ? root.controller.sources : null
        sourceCount: root.sourceCount
        singleMode: root.singleMode
        canonicalSourceIndex: root.canonicalSourceIndex
        busy: root.busy
        pendingSourceIndexes: root.shell ? root.shell.pendingSourceIndexes : []
        z: 30
        borderColor: root.borderColor
        accentColor: root.accentColor
        textColor: root.primaryTextColor
        mutedTextColor: root.mutedTextColor
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            leftMargin: root.singleMode ? 8 : 0
        }
        onAddRequested: reviewInputDialogs.openAddVideo()
        onRemoveRequested: sourceIndex => root.removeSelectedSource(sourceIndex)
        onReferenceRequested: sourceIndex => root.shell ? root.shell.changeReference(sourceIndex) : root.controller.changeReference(sourceIndex)
    }
    CompareModeBar {
        id: comparisonBar

        sourceCount: root.sourceCount
        currentMode: root.effectiveViewMode
        differenceEdges: root.differenceEdges
        currentEdgeIndex: root.differenceEdgeIndex(root.differenceEdge)
        inspectorOpen: root.inspectorOpen
        busy: root.busy
        borderColor: root.borderColor
        accentColor: root.accentColor
        textColor: root.primaryTextColor
        anchors {
            top: sourceBar.bottom
            left: parent.left
            right: parent.right
        }
        onModeRequested: mode => root.preferences.viewMode = mode
        onEdgeRequested: edge => root.preferences.differenceEdge = edge
        onInspectorRequested: root.shell.inspectorVisible = !root.inspectorOpen
    }
    TabbedInspector {
        id: alignmentBar

        controller: root.controller
        preferences: root.preferences
        session: root.shell
        alignmentHost: root
        borderColor: root.borderColor
        primaryTextColor: root.primaryTextColor
        mutedTextColor: root.mutedTextColor
        singleMode: root.singleMode
        sourceCount: root.sourceCount
        wipeMode: root.wipeMode
        differenceMode: root.differenceMode
        analysisGridMode: root.analysisGridMode
        differenceEdges: root.differenceEdges
        differenceEdge: root.differenceEdge
        referenceSourceIndex: root.referenceSourceIndex
        differenceThresholdEnabled: root.differenceThresholdEnabled
        differenceThresholdCode: root.differenceThresholdCode
        differenceThresholdPolicy: root.differenceThresholdPolicy
        wipePosition: root.wipePosition
        roiEnabled: root.roiEnabled
        graphicsReady: root.graphicsReady
        dropFrameTimecode: root.dropFrameTimecode
        currentFrame: root.currentFrame
        inFrame: root.inFrame
        outFrame: root.outFrame
        rangePlaybackActive: root.rangePlaybackActive
        visible: root.chromeVisible && root.inspectorOpen && root.sourceCount > 0
        z: 20
        anchors {
            top: parent.top
            topMargin: root.singleMode ? 0 : sourceBar.height + comparisonBar.height
            right: parent.right
            bottom: parent.bottom
            bottomMargin: 0
        }
        onDifferenceEdgeRequested: edge => root.preferences.differenceEdge = edge
        onReferenceRequested: sourceIndex => root.shell.changeReference(sourceIndex)
        onDifferenceThresholdEnabledRequested: enabled => root.differenceThresholdEnabled = enabled
        onDifferenceThresholdCodeRequested: code => root.differenceThresholdCode = code
        onDifferenceThresholdPolicyRequested: policy => root.differenceThresholdPolicy = policy
        onWipePositionRequested: position => root.wipePosition = position
        onResetViewportRequested: root.resetViewport()
        onClearRoiRequested: root.clearRoi()
        onDropFrameTimecodeRequested: enabled => root.setDropFrameTimecode(enabled)
        onInPointRequested: root.setInPoint()
        onOutPointRequested: root.setOutPoint()
        onClearRangeRequested: root.clearSelectedRange()
        onRangeLoopToggleRequested: root.toggleRangeLoop()
    }
    ComparisonViewport {
        id: viewportFrame

        preferences: root.preferences
        borderColor: root.borderColor
        accentColor: root.accentColor
        primaryTextColor: root.primaryTextColor
        mutedTextColor: root.mutedTextColor
        errorColor: root.errorColor
        chromeVisible: root.chromeVisible
        effectiveViewMode: root.effectiveViewMode
        wipePosition: root.wipePosition
        selectedDifferenceExactness: root.selectedDifferenceExactness
        differenceThresholdEnabled: root.differenceThresholdEnabled
        differenceThresholdCode: root.differenceThresholdCode
        differenceThresholdPolicy: root.differenceThresholdPolicy
        referenceSourceIndex: root.referenceSourceIndex
        sourceCount: root.sourceCount
        wipeMode: root.wipeMode
        differenceMode: root.differenceMode
        analysisGridMode: root.analysisGridMode
        immersiveHudVisible: root.immersiveHudVisible
        immersiveHudText: root.immersiveHudText
        showFramePending: root.showFramePending
        currentFrame: root.currentFrame
        differenceUnavailableDetail: root.differenceUnavailableDetail
        combinedAlignmentStatus: root.combinedAlignmentStatus
        singleMode: root.singleMode
        differenceFirstSlot: root.differenceFirstSlot
        effectiveDifferenceEdge: root.differenceEdge
        sourceNames: [root.sourceAName, root.sourceBName, root.sourceCName]
        sourceMediaInfo: root.controller ? root.controller.sourceMediaInfo : []
        frameErrorBannerVisible: root.frameErrorBannerVisible
        errorDetail: root.errorDetails()
        overlayVisible: root.overlayVisible
        hasErrors: root.hasErrors
        busy: root.busy
        overlayTitle: root.overlayTitle
        overlayDetail: root.overlayDetail
        anchors {
            top: parent.top
            topMargin: root.chromeVisible && !root.singleMode ? sourceBar.height + comparisonBar.height + 6 : 0
            bottom: parent.bottom
            bottomMargin: 0
            left: parent.left
            leftMargin: root.chromeVisible ? 14 : 0
            right: alignmentBar.visible && root.width >= 1120 ? alignmentBar.left : parent.right
            rightMargin: root.chromeVisible ? 14 : 0
        }
        onWipePositionRequested: position => root.wipePosition = position
        onOscRevealRequested: root.revealOsc()
        onContextMenuRequested: root.openViewerContextMenu()
        onFullScreenToggleRequested: root.toggleFullScreen()
    }
    EmptyReviewView {
        visible: root.sourceCount === 0 && !root.busy && root.graphicsReady && !root.hasErrors
        z: 35
        accentColor: root.accentColor
        textColor: root.primaryTextColor
        mutedTextColor: root.mutedTextColor
        anchors.fill: viewportFrame
        onOpenVideosRequested: reviewInputDialogs.openVideos()
    }
    TimelineThumbnailCache {
        id: thumbnailCache

        sourceItem: viewportFrame.videoOutput
        currentFrame: Number(root.currentFrame)
        totalFrames: Number(root.totalFrames)
        generation: root.shell ? root.shell.activeGeneration : 0
    }
    PlayerOsc {
        id: transport

        z: 50
        controllerState: root.oscState
        playing: root.playing
        timelineEnabled: root.timelineEnabled
        currentFrame: Number(root.currentFrame)
        totalFrames: Number(root.totalFrames)
        progress: root.timelineProgress
        timecodeText: root.currentTimecode
        markers: root.alignmentTimelineMarkers
        actions: reviewShortcuts.actions
        focusTarget: viewportFrame
        canFirst: root.canFirstAction
        canPrevious: root.canPreviousAction
        canPlay: root.canPlayAction
        canPause: root.canPauseAction
        canNext: root.canNextAction
        canLast: root.canLastAction
        inFrame: root.inFrame
        outFrame: root.outFrame
        loopRangeActive: root.rangePlaybackActive
        previewFrame: root.timelinePreviewFrame
        previewTimecode: root.previewTimecode
        previewThumbnailSource: thumbnailCache.urlForFrame(root.timelinePreviewFrame)
        anchors {
            left: viewportFrame.left
            right: viewportFrame.right
            bottom: viewportFrame.bottom
        }
        onSeekRequested: frame => {
            root.revealOsc();
            root.timelinePreviewFrame = frame;
            root.controller.seekFrame(frame);
        }
        onPreviewRequested: frame => root.timelinePreviewFrame = frame
    }

    ShortcutHelpOverlay {
        id: shortcutHelp

        playerPreset: root.shortcutPreset === 1
    }

    ReviewContextMenu {
        id: viewerContextMenu

        sourceCount: root.sourceCount
        canonicalSourceIndex: root.canonicalSourceIndex
        currentViewMode: root.effectiveViewMode
        fullScreen: root.fullScreen
        differenceEdges: root.differenceEdges
        currentEdgeIndex: root.differenceEdgeIndex(root.differenceEdge)
        // qmllint disable unqualified
        onSideRequested: root.preferences.viewMode = ComparisonSurface.SideBySide
        onWipeRequested: root.preferences.viewMode = ComparisonSurface.Wipe
        onDiffRequested: root.preferences.viewMode = ComparisonSurface.Difference
        // qmllint enable unqualified
        onEdgeRequested: edge => root.preferences.differenceEdge = edge
        onReferenceRequested: sourceIndex => root.shell ? root.shell.changeReference(sourceIndex) : root.controller.changeReference(sourceIndex)
        onOpenRequested: reviewInputDialogs.openVideos()
        onInspectorRequested: root.shell.inspectorVisible = true
        onFullScreenRequested: root.toggleFullScreen()
    }

    Rectangle {
        id: intentQueuePanel

        readonly property var runningIntent: root.shell ? root.shell.activeIntent : ({})
        readonly property var queued: root.shell ? root.shell.queuedIntents : []
        visible: Number(runningIntent.id || 0) > 0 || queued.length > 0
        z: 890
        width: 286
        height: queueColumn.implicitHeight + 20
        radius: 7
        color: "#f21d2635"
        border.color: root.borderColor
        anchors {
            top: parent.top
            right: parent.right
            topMargin: root.chromeVisible ? 58 : 18
            rightMargin: 18
        }

        Column {
            id: queueColumn

            spacing: 7
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: 10
            }

            Text {
                visible: Number(intentQueuePanel.runningIntent.id || 0) > 0
                text: qsTr("Working · %1").arg(root.intentKindText(intentQueuePanel.runningIntent.kind, intentQueuePanel.runningIntent.sourceCount))
                color: root.primaryTextColor
                font.pixelSize: 12
                elide: Text.ElideRight
                width: parent.width
            }

            Row {
                visible: intentQueuePanel.queued.length > 0
                width: parent.width

                Text {
                    text: qsTr("%1 request(s) queued").arg(intentQueuePanel.queued.length)
                    color: root.mutedTextColor
                    font.pixelSize: 12
                    width: parent.width - cancelAllButton.width
                    anchors.verticalCenter: parent.verticalCenter
                }

                ToolButton {
                    id: cancelAllButton

                    text: qsTr("Cancel all")
                    implicitWidth: 74
                    implicitHeight: 24
                    onClicked: root.shell.cancelAllQueuedIntents()
                }
            }

            Repeater {
                model: intentQueuePanel.queued

                delegate: Row {
                    id: queuedIntentRow

                    required property var modelData
                    width: queueColumn.width

                    Text {
                        text: root.intentKindText(queuedIntentRow.modelData.kind, queuedIntentRow.modelData.sourceCount)
                        color: root.primaryTextColor
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        width: parent.width - cancelQueuedButton.width
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    ToolButton {
                        id: cancelQueuedButton

                        text: qsTr("Cancel")
                        implicitWidth: 58
                        implicitHeight: 22
                        onClicked: root.shell.cancelQueuedIntent(queuedIntentRow.modelData.id)
                    }
                }
            }
        }
    }

    Rectangle {
        visible: root.intentMessage.length > 0
        z: 900
        radius: 6
        color: "#ed1d2635"
        border.color: root.borderColor
        width: Math.max(0, Math.min(root.width - 48, intentToastText.implicitWidth + 32))
        height: intentToastText.paintedHeight + 20
        anchors {
            horizontalCenter: parent.horizontalCenter
            top: parent.top
            topMargin: root.chromeVisible ? 58 : 18
        }

        Text {
            id: intentToastText

            anchors.centerIn: parent
            width: Math.max(0, parent.width - 32)
            text: root.intentMessage
            color: root.primaryTextColor
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
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
                    text: qsTr("Drop to open in VCStation")
                    color: root.primaryTextColor
                    font.pixelSize: 24
                    font.bold: true
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: qsTr("1–3 videos")
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
