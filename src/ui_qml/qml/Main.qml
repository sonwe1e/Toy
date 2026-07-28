pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Window
// Dvs.Ui is registered by the C++ host before this document is loaded.
// qmllint disable import
import Dvs.Ui 1.0

// qmllint enable import

Window {
    id: root

    width: 1440
    height: 900
    minimumWidth: 960
    minimumHeight: 640
    visible: false
    title: qsTr("Dual Video Studio")
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
    // qmllint enable unqualified

    property url selectedSourceA: ""
    property url selectedSourceB: ""

    readonly property bool busy: Boolean(controller && controller.busy)
    readonly property bool playing: Boolean(controller && controller.playing)
    readonly property bool graphicsReady: Boolean(controller && controller.graphicsReady)
    readonly property var currentFrame: controller ? controller.currentFrame : -1
    readonly property var totalFrames: controller ? controller.totalFrames : 0
    readonly property string sourceAErrorKey: controller ? controller.sourceAErrorKey : ""
    readonly property string sourceBErrorKey: controller ? controller.sourceBErrorKey : ""
    readonly property string pairErrorKey: controller ? controller.pairErrorKey : ""
    readonly property bool hasErrors: sourceAErrorKey.length > 0 || sourceBErrorKey.length > 0 || pairErrorKey.length > 0
    readonly property bool hasSelectedSourceA: selectedSourceA.toString().length > 0
    readonly property bool hasSelectedSourceB: selectedSourceB.toString().length > 0
    readonly property string sourceAName: hasSelectedSourceA ? fileName(selectedSourceA) : (controller && controller.sourceAFilename.length > 0 ? controller.sourceAFilename : qsTr("No file selected"))
    readonly property string sourceBName: hasSelectedSourceB ? fileName(selectedSourceB) : (controller && controller.sourceBFilename.length > 0 ? controller.sourceBFilename : qsTr("No file selected"))
    readonly property bool canFirstAction: graphicsReady && !busy && Boolean(controller && controller.canFirst)
    readonly property bool canPreviousAction: graphicsReady && !busy && Boolean(controller && controller.canPrevious)
    readonly property bool canNextAction: graphicsReady && !busy && Boolean(controller && controller.canNext)
    readonly property bool canLastAction: graphicsReady && !busy && Boolean(controller && controller.canLast)
    readonly property bool canPlayAction: graphicsReady && !busy && Boolean(controller && controller.canPlay)
    readonly property bool canPauseAction: !busy && Boolean(controller && controller.canPause)
    readonly property int largeStepFrames: preferences ? preferences.largeStepFrames : 10
    readonly property bool differenceMode: preferences ? Number(preferences.viewMode) === 1 : false
    property bool timelineDragging: false
    property int timelinePreviewFrame: -1
    readonly property string frameText: timelineDragging && timelinePreviewFrame >= 0 ? qsTr("Frame %1 of %2 (release to seek)").arg(timelinePreviewFrame + 1).arg(totalFrames) : (currentFrame >= 0 && totalFrames > 0 ? qsTr("Frame %1 of %2").arg(currentFrame + 1).arg(totalFrames) : qsTr("No frame displayed"))
    readonly property real frameProgress: currentFrame >= 0 && totalFrames > 1 ? Math.max(0, Math.min(1, Number(currentFrame) / (Number(totalFrames) - 1))) : 0
    readonly property real timelineProgress: timelineDragging && timelinePreviewFrame >= 0 && totalFrames > 1 ? Number(timelinePreviewFrame) / (Number(totalFrames) - 1) : frameProgress
    readonly property bool timelineEnabled: graphicsReady && !busy && Boolean(controller && controller.canFirst) && totalFrames > 0
    readonly property bool globalMediaShortcutsEnabled: !focusBlocksGlobalMediaShortcuts(root.activeFocusItem)
    readonly property bool frameErrorBannerVisible: hasErrors && currentFrame >= 0 && !busy && graphicsReady && Boolean(controller && controller.canFirst)
    readonly property string overlayTitle: busy ? (currentFrame >= 0 ? qsTr("Loading frame...") : qsTr("Loading source pair...")) : (!graphicsReady ? qsTr("Graphics unavailable") : (hasErrors ? qsTr("Unable to open source pair") : qsTr("No source pair open")))
    readonly property string overlayDetail: busy ? qsTr("Please wait while the requested media is prepared.") : (!graphicsReady ? qsTr("Navigation and opening are disabled until the graphics device is ready.") : (hasErrors ? errorDetails() : qsTr("Select source A and source B, then choose Open Pair.")))
    readonly property bool overlayVisible: busy || !graphicsReady || currentFrame < 0 || (hasErrors && !frameErrorBannerVisible)

    function fileName(fileUrl) {
        const decodedUrl = decodeURIComponent(fileUrl.toString());
        const separator = Math.max(decodedUrl.lastIndexOf("/"), decodedUrl.lastIndexOf("\\"));
        return decodedUrl.substring(separator + 1);
    }

    function frameAtTimelinePosition(position) {
        if (totalFrames <= 1)
            return 0;
        const normalized = Math.max(0, Math.min(1, position));
        return Math.round(normalized * (Number(totalFrames) - 1));
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
        if (sourceAErrorKey.length > 0)
            errors.push(qsTr("Source A: %1").arg(errorMessage(sourceAErrorKey)));
        if (sourceBErrorKey.length > 0)
            errors.push(qsTr("Source B: %1").arg(errorMessage(sourceBErrorKey)));
        if (pairErrorKey.length > 0)
            errors.push(qsTr("Pair: %1").arg(errorMessage(pairErrorKey)));
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
            return qsTr("The two sources must have the same frame rate.");
        case "source-frame-count-mismatch":
            return qsTr("The two sources must contain the same number of frames.");
        case "source-duration-mismatch":
            return qsTr("The two sources must have the same duration.");
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

    component ActionButton: Button {
        id: control

        property bool blocksGlobalMediaShortcuts: true

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
    }

    FileDialog {
        id: sourceAFileDialog

        objectName: "sourceAFileDialog"
        title: qsTr("Select source A")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: root.selectedSourceA = selectedFile
    }

    FileDialog {
        id: sourceBFileDialog

        objectName: "sourceBFileDialog"
        title: qsTr("Select source B")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: root.selectedSourceB = selectedFile
    }

    Rectangle {
        id: commandBar

        height: 64
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
                text: qsTr("DUAL VIDEO STUDIO")
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                width: 1
                height: 24
                color: root.borderColor
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                color: root.mutedTextColor
                font.pixelSize: 13
                text: qsTr("Frame-accurate direct comparison")
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Rectangle {
            implicitWidth: graphicsStatus.implicitWidth + 24
            implicitHeight: 28
            radius: 14
            color: root.graphicsReady ? "#173c31" : "#3d2c1e"
            border.color: root.graphicsReady ? "#2c7a61" : "#79522d"
            anchors {
                right: parent.right
                rightMargin: 20
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

    Rectangle {
        id: sourceBar

        height: 92
        color: "#111823"
        border.color: root.borderColor
        anchors {
            top: commandBar.bottom
            left: parent.left
            right: parent.right
        }

        Row {
            id: sourceRow

            spacing: 12
            anchors {
                fill: parent
                margins: 14
            }

            Rectangle {
                width: (sourceRow.width - openPairButton.width - sourceRow.spacing * 2) / 2
                height: sourceRow.height
                radius: 6
                color: root.raisedPanelColor
                border.color: root.sourceAErrorKey.length > 0 ? root.errorColor : root.borderColor

                Text {
                    id: sourceALabel

                    text: qsTr("SOURCE A")
                    color: "#9fc3ff"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    anchors {
                        top: parent.top
                        topMargin: 11
                        left: parent.left
                        leftMargin: 14
                    }
                }

                Text {
                    id: sourceAFilename

                    objectName: "sourceAFilename"
                    text: root.sourceAName
                    color: root.hasSelectedSourceA ? root.primaryTextColor : root.mutedTextColor
                    font.pixelSize: 13
                    elide: Text.ElideMiddle
                    anchors {
                        top: sourceALabel.bottom
                        topMargin: 5
                        left: parent.left
                        leftMargin: 14
                        right: selectSourceAButton.left
                        rightMargin: 12
                    }
                }

                ActionButton {
                    id: selectSourceAButton

                    objectName: "selectSourceAButton"
                    text: qsTr("Select A")
                    enabled: root.graphicsReady && !root.busy
                    Accessible.name: qsTr("Select source A")
                    onClicked: sourceAFileDialog.open()
                    anchors {
                        right: parent.right
                        rightMargin: 10
                        verticalCenter: parent.verticalCenter
                    }
                }
            }

            Rectangle {
                width: (sourceRow.width - openPairButton.width - sourceRow.spacing * 2) / 2
                height: sourceRow.height
                radius: 6
                color: root.raisedPanelColor
                border.color: root.sourceBErrorKey.length > 0 ? root.errorColor : root.borderColor

                Text {
                    id: sourceBLabel

                    text: qsTr("SOURCE B")
                    color: "#9fc3ff"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    anchors {
                        top: parent.top
                        topMargin: 11
                        left: parent.left
                        leftMargin: 14
                    }
                }

                Text {
                    objectName: "sourceBFilename"
                    text: root.sourceBName
                    color: root.hasSelectedSourceB ? root.primaryTextColor : root.mutedTextColor
                    font.pixelSize: 13
                    elide: Text.ElideMiddle
                    anchors {
                        top: sourceBLabel.bottom
                        topMargin: 5
                        left: parent.left
                        leftMargin: 14
                        right: selectSourceBButton.left
                        rightMargin: 12
                    }
                }

                ActionButton {
                    id: selectSourceBButton

                    objectName: "selectSourceBButton"
                    text: qsTr("Select B")
                    enabled: root.graphicsReady && !root.busy
                    Accessible.name: qsTr("Select source B")
                    onClicked: sourceBFileDialog.open()
                    anchors {
                        right: parent.right
                        rightMargin: 10
                        verticalCenter: parent.verticalCenter
                    }
                }
            }

            ActionButton {
                id: openPairButton

                objectName: "openPairButton"
                width: 148
                height: 46
                text: qsTr("Open Pair")
                enabled: root.hasSelectedSourceA && root.hasSelectedSourceB && root.graphicsReady && !root.busy && Boolean(root.controller && root.controller.canOpen)
                Accessible.name: qsTr("Open selected source pair")
                onClicked: root.controller.openComparison(root.selectedSourceA, root.selectedSourceB)
                anchors.verticalCenter: parent.verticalCenter

                background: Rectangle {
                    radius: 5
                    color: !openPairButton.enabled ? "#202938" : (openPairButton.down ? "#2662bd" : (openPairButton.hovered ? "#4f94ff" : root.accentColor))
                    border.width: openPairButton.activeFocus ? 2 : 1
                    border.color: openPairButton.activeFocus ? "#b7d3ff" : (openPairButton.enabled ? "#72a7fa" : "#2a3444")
                }
            }
        }
    }

    Rectangle {
        id: comparisonBar

        height: 58
        color: root.panelColor
        border.color: root.borderColor
        anchors {
            top: sourceBar.bottom
            left: parent.left
            right: parent.right
        }

        Row {
            id: comparisonControls

            spacing: 8
            anchors {
                left: parent.left
                leftMargin: 20
                verticalCenter: parent.verticalCenter
            }

            Text {
                text: qsTr("View")
                color: root.mutedTextColor
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            ToolbarCombo {
                id: viewModeCombo

                objectName: "viewModeCombo"
                model: [qsTr("Side by side"), qsTr("Diff")]
                currentIndex: root.preferences ? Number(root.preferences.viewMode) : 0
                Accessible.name: qsTr("Comparison view")
                onActivated: index => {
                    if (root.preferences)
                        root.preferences.viewMode = index;
                }
            }

            Text {
                visible: root.differenceMode
                text: qsTr("Metric")
                color: root.mutedTextColor
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            ToolbarCombo {
                id: differenceMetricCombo

                objectName: "differenceMetricCombo"
                visible: root.differenceMode
                model: [qsTr("RGB absolute"), qsTr("Luma"), qsTr("Chroma"), qsTr("Heatmap")]
                currentIndex: root.preferences ? Number(root.preferences.differenceMetric) : 0
                Accessible.name: qsTr("Difference metric")
                onActivated: index => {
                    if (root.preferences)
                        root.preferences.differenceMetric = index;
                }
            }

            ToolbarCombo {
                id: differenceGainCombo

                objectName: "differenceGainCombo"
                visible: root.differenceMode
                implicitWidth: 76
                model: ["1x", "2x", "4x", "8x", "16x"]
                currentIndex: root.preferences ? Number(root.preferences.differenceGain) : 0
                Accessible.name: qsTr("Difference gain")
                onActivated: index => {
                    if (root.preferences)
                        root.preferences.differenceGain = index;
                }
            }

            ToolbarCombo {
                id: differenceReferenceCombo

                objectName: "differenceReferenceCombo"
                visible: root.differenceMode
                implicitWidth: 94
                model: [qsTr("Canvas A"), qsTr("Canvas B")]
                currentIndex: root.preferences ? Number(root.preferences.differenceReference) : 0
                Accessible.name: qsTr("Difference reference canvas")
                onActivated: index => {
                    if (root.preferences)
                        root.preferences.differenceReference = index;
                }
            }

            ToolbarCombo {
                id: differenceFilterCombo

                objectName: "differenceFilterCombo"
                visible: root.differenceMode
                implicitWidth: 104
                model: [qsTr("Nearest"), qsTr("Bilinear"), qsTr("Bicubic")]
                currentIndex: root.preferences ? Number(root.preferences.differenceFilter) : 1
                Accessible.name: qsTr("Spatial resampling filter")
                onActivated: index => {
                    if (root.preferences)
                        root.preferences.differenceFilter = index;
                }
            }

            Text {
                text: qsTr("Step")
                color: root.mutedTextColor
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            ToolbarCombo {
                id: largeStepCombo

                objectName: "largeStepCombo"
                implicitWidth: 74
                model: ["5", "10"]
                currentIndex: root.largeStepFrames === 5 ? 0 : 1
                Accessible.name: qsTr("Up and down frame step")
                onActivated: index => {
                    if (root.preferences)
                        root.preferences.largeStepFrames = index === 0 ? 5 : 10;
                }
            }
        }

        Text {
            visible: root.differenceMode
            text: qsTr("Size mismatch is resampled; the result is not pixel-exact.")
            color: root.mutedTextColor
            font.pixelSize: 10
            elide: Text.ElideRight
            anchors {
                left: comparisonControls.right
                leftMargin: 12
                right: parent.right
                rightMargin: 20
                verticalCenter: parent.verticalCenter
            }
        }
    }

    Rectangle {
        id: viewportFrame

        objectName: "mediaViewportFocusTarget"
        focus: true
        color: "#06080d"
        border.color: root.borderColor
        border.width: 1
        radius: 7
        clip: true
        anchors {
            top: comparisonBar.bottom
            topMargin: 12
            bottom: transport.top
            bottomMargin: 18
            left: parent.left
            leftMargin: 20
            right: parent.right
            rightMargin: 20
        }

        TapHandler {
            onTapped: viewportFrame.forceActiveFocus()
        }

        // The type is runtime-registered; startup smoke coverage verifies the registration.
        // qmllint disable import unqualified unresolved-type
        ComparisonSurface {
            id: dualVideoSurface

            objectName: "dualVideoSurface"
            Accessible.name: qsTr("Dual video comparison surface")
            viewMode: root.preferences ? root.preferences.viewMode : ComparisonSurface.SideBySide
            differenceMetric: root.preferences ? root.preferences.differenceMetric : ComparisonSurface.RgbAbsolute
            differenceGain: root.preferences ? root.preferences.differenceGain : ComparisonSurface.Gain1x
            differenceReference: root.preferences ? root.preferences.differenceReference : ComparisonSurface.ReferenceA
            differenceFilter: root.preferences ? root.preferences.differenceFilter : ComparisonSurface.Bilinear
            anchors {
                fill: parent
                margins: 1
            }
        }
        // qmllint enable import unqualified unresolved-type

        Row {
            id: surfaceLabels

            height: 46
            spacing: 8
            anchors {
                top: parent.top
                topMargin: 12
                left: parent.left
                leftMargin: 12
                right: parent.right
                rightMargin: 12
            }

            Repeater {
                model: [
                    {
                        "side": qsTr("A"),
                        "filename": root.sourceAName
                    },
                    {
                        "side": qsTr("B"),
                        "filename": root.sourceBName
                    }
                ]

                delegate: Rectangle {
                    id: surfaceLabel

                    required property var modelData

                    width: (surfaceLabels.width - surfaceLabels.spacing) / 2
                    height: surfaceLabels.height
                    radius: 5
                    color: "#d9111721"
                    border.color: "#663a4a62"

                    Rectangle {
                        width: 28
                        height: 28
                        radius: 4
                        color: root.accentColor
                        anchors {
                            left: parent.left
                            leftMargin: 9
                            verticalCenter: parent.verticalCenter
                        }

                        Text {
                            anchors.centerIn: parent
                            text: surfaceLabel.modelData.side
                            color: "white"
                            font.bold: true
                            font.pixelSize: 13
                        }
                    }

                    Text {
                        text: surfaceLabel.modelData.filename
                        color: root.primaryTextColor
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                        anchors {
                            left: parent.left
                            leftMargin: 46
                            right: parent.right
                            rightMargin: 10
                            verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }

        Rectangle {
            id: frameErrorBanner

            objectName: "frameErrorBanner"
            width: Math.min(parent.width - 48, 720)
            height: frameErrorBannerColumn.implicitHeight + 20
            radius: 6
            visible: root.frameErrorBannerVisible
            color: "#e6351f2a"
            border.color: "#b9503f4a"
            z: 2
            Accessible.name: qsTr("Frame unchanged. %1").arg(root.errorDetails())
            anchors {
                top: surfaceLabels.bottom
                topMargin: 8
                horizontalCenter: parent.horizontalCenter
            }

            Column {
                id: frameErrorBannerColumn

                width: parent.width - 28
                spacing: 3
                anchors.centerIn: parent

                Text {
                    width: parent.width
                    text: qsTr("Frame unchanged")
                    color: "#ffb4b4"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }

                Text {
                    objectName: "frameErrorBannerDetail"
                    width: parent.width
                    text: root.errorDetails()
                    color: root.primaryTextColor
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }
        }

        Rectangle {
            id: statusOverlay

            objectName: "statusOverlay"
            width: Math.min(parent.width - 48, 560)
            height: overlayColumn.implicitHeight + 32
            radius: 7
            visible: root.overlayVisible
            color: root.hasErrors && !root.busy ? "#ee351f2a" : "#ed151d29"
            border.color: root.hasErrors && !root.busy ? "#a9503f4a" : "#a43d4d64"
            anchors.centerIn: parent

            Column {
                id: overlayColumn

                width: parent.width - 36
                spacing: 8
                anchors.centerIn: parent

                BusyIndicator {
                    width: 34
                    height: 34
                    running: root.busy
                    visible: running
                    anchors.horizontalCenter: parent.horizontalCenter
                    Accessible.name: qsTr("Loading")
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: root.overlayTitle
                    color: root.hasErrors && !root.busy ? "#ffb4b4" : root.primaryTextColor
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }

                Text {
                    objectName: "statusDetail"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: root.overlayDetail
                    color: root.mutedTextColor
                    font.pixelSize: 12
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Rectangle {
        id: transport

        height: 144
        color: root.panelColor
        border.color: root.borderColor
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }

        Text {
            id: frameCounter

            objectName: "frameCounter"
            text: root.frameText
            color: root.primaryTextColor
            font.pixelSize: 14
            font.weight: Font.DemiBold
            Accessible.name: text
            anchors {
                top: parent.top
                topMargin: 15
                horizontalCenter: parent.horizontalCenter
            }
        }

        Item {
            id: progressTrack

            property bool blocksGlobalMediaShortcuts: true

            objectName: "timelineSlider"
            height: 28
            enabled: root.timelineEnabled
            activeFocusOnTab: enabled
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("Frame timeline")
            Accessible.description: root.frameText
            anchors {
                top: frameCounter.bottom
                topMargin: 5
                left: parent.left
                leftMargin: 48
                right: parent.right
                rightMargin: 48
            }

            Rectangle {
                id: timelineRail

                width: parent.width
                height: 4
                radius: 2
                color: root.borderColor
                anchors.centerIn: parent
            }

            Rectangle {
                width: timelineRail.width * root.timelineProgress
                height: timelineRail.height
                radius: timelineRail.radius
                color: root.accentColor
                anchors {
                    left: timelineRail.left
                    verticalCenter: timelineRail.verticalCenter
                }
            }

            Rectangle {
                width: 14
                height: 14
                radius: 7
                x: Math.max(0, Math.min(parent.width - width, root.timelineProgress * parent.width - width / 2))
                color: progressTrack.enabled ? root.accentColor : "#536176"
                border.width: progressTrack.activeFocus ? 2 : 1
                border.color: progressTrack.activeFocus ? "white" : "#b7d3ff"
                anchors.verticalCenter: parent.verticalCenter
            }

            MouseArea {
                anchors.fill: parent
                enabled: progressTrack.enabled
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                preventStealing: true

                onPressed: mouse => {
                    progressTrack.forceActiveFocus();
                    root.timelineDragging = true;
                    root.timelinePreviewFrame = root.frameAtTimelinePosition(mouse.x / width);
                }
                onPositionChanged: mouse => {
                    if (pressed)
                        root.timelinePreviewFrame = root.frameAtTimelinePosition(mouse.x / width);
                }
                onReleased: mouse => {
                    root.timelinePreviewFrame = root.frameAtTimelinePosition(mouse.x / width);
                    const target = root.timelinePreviewFrame;
                    root.timelineDragging = false;
                    if (root.controller)
                        root.controller.seekFrame(target);
                }
                onCanceled: {
                    root.timelineDragging = false;
                    root.timelinePreviewFrame = -1;
                }
            }
        }

        Row {
            spacing: 10
            anchors {
                bottom: parent.bottom
                bottomMargin: 20
                horizontalCenter: parent.horizontalCenter
            }

            ActionButton {
                id: firstButton

                objectName: "firstButton"
                text: qsTr("First")
                enabled: root.canFirstAction
                Accessible.name: qsTr("First frame")
                onClicked: root.controller.first()
            }

            ActionButton {
                id: previousButton

                objectName: "previousButton"
                text: qsTr("Previous")
                enabled: root.canPreviousAction
                Accessible.name: qsTr("Previous frame")
                onClicked: root.controller.previous()
            }

            ActionButton {
                id: playbackButton

                objectName: "playbackButton"
                text: root.playing ? qsTr("Pause") : qsTr("Play")
                enabled: root.playing ? root.canPauseAction : root.canPlayAction
                Accessible.name: root.playing ? qsTr("Pause playback") : qsTr("Play continuously")
                onClicked: root.controller.togglePlayback()
            }

            ActionButton {
                id: nextButton

                objectName: "nextButton"
                text: qsTr("Next")
                enabled: root.canNextAction
                Accessible.name: qsTr("Next frame")
                onClicked: root.controller.next()
            }

            ActionButton {
                id: lastButton

                objectName: "lastButton"
                text: qsTr("Last")
                enabled: root.canLastAction
                Accessible.name: qsTr("Last frame")
                onClicked: root.controller.last()
            }
        }

        Text {
            text: qsTr("Space Play/Pause / Home / End / Left / Right / Up +%1 / Down -%1").arg(root.largeStepFrames)
            color: root.mutedTextColor
            font.pixelSize: 11
            anchors {
                right: parent.right
                rightMargin: 22
                bottom: parent.bottom
                bottomMargin: 13
            }
        }
    }

    Shortcut {
        sequence: "Space"
        enabled: root.globalMediaShortcutsEnabled && playbackButton.enabled
        onActivated: {
            playbackButton.click();
            viewportFrame.forceActiveFocus();
        }
    }

    Shortcut {
        sequence: "Home"
        enabled: root.globalMediaShortcutsEnabled && firstButton.enabled
        onActivated: {
            firstButton.click();
            viewportFrame.forceActiveFocus();
        }
    }

    Shortcut {
        sequence: "End"
        enabled: root.globalMediaShortcutsEnabled && lastButton.enabled
        onActivated: {
            lastButton.click();
            viewportFrame.forceActiveFocus();
        }
    }

    Shortcut {
        sequence: "Left"
        enabled: root.globalMediaShortcutsEnabled && previousButton.enabled
        onActivated: {
            previousButton.click();
            viewportFrame.forceActiveFocus();
        }
    }

    Shortcut {
        sequence: "Right"
        enabled: root.globalMediaShortcutsEnabled && nextButton.enabled
        onActivated: {
            nextButton.click();
            viewportFrame.forceActiveFocus();
        }
    }

    Shortcut {
        sequence: "Up"
        enabled: root.globalMediaShortcutsEnabled && root.canNextAction
        onActivated: {
            root.controller.stepFrames(root.largeStepFrames);
            viewportFrame.forceActiveFocus();
        }
    }

    Shortcut {
        sequence: "Down"
        enabled: root.globalMediaShortcutsEnabled && root.canPreviousAction
        onActivated: {
            root.controller.stepFrames(-root.largeStepFrames);
            viewportFrame.forceActiveFocus();
        }
    }
}
