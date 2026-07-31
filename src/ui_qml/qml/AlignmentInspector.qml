pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: control

    required property var host

    objectName: "advancedAlignmentInspector"
    width: 360
    color: "#111823"
    border.color: control.host.borderColor

    Column {
        id: alignmentControls

        spacing: 6
        anchors {
            fill: parent
            margins: 14
        }

        Text {
            objectName: "alignmentModeStatus"
            width: parent.width
            text: control.host.anyManualAlignmentActive ? qsTr("Manual alignment") : (control.host.autoAlignmentActive ? qsTr("Automatic alignment") : qsTr("Strict Index"))
            color: control.host.anyManualAlignmentActive || control.host.autoAlignmentActive ? "#efbf83" : "#8ce2c2"
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }

        Text {
            objectName: "manualOffsetStatusLabel"
            width: parent.width
            text: control.host.manualOffsetActive ? qsTr("Frame alignment offset · active") : qsTr("Frame alignment offset")
            color: control.host.manualOffsetActive ? "#efbf83" : control.host.mutedTextColor
            font.pixelSize: 11
        }

        Repeater {
            id: sourceOffsetRepeater

            objectName: "sourceOffsetRepeater"
            model: control.host.controller ? control.host.controller.sources : null

            delegate: Row {
                id: sourceOffsetDelegate

                required property int sourceId
                required property int role
                required property int manualOffset
                property int sourceIdValue: sourceId
                width: alignmentControls.width
                spacing: 8

                Text {
                    width: parent.width - sourceOffsetInput.width - parent.spacing
                    text: qsTr("Source %1 offset (frames)").arg(String.fromCharCode(65 + sourceOffsetDelegate.sourceIdValue))
                    color: control.host.mutedTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                ReviewOffsetSpinBox {
                    id: sourceOffsetInput

                    objectName: "sourceOffset-" + sourceOffsetDelegate.sourceIdValue
                    textColor: control.host.primaryTextColor
                    mutedTextColor: control.host.mutedTextColor
                    accentColor: control.host.accentColor
                    panelColor: control.host.raisedPanelColor
                    borderColor: control.host.borderColor
                    value: control.host.sourceOffset(sourceOffsetDelegate.sourceIdValue, sourceOffsetDelegate.manualOffset)
                    enabled: sourceOffsetDelegate.sourceIdValue !== (control.host.referenceSourceIndex >= 0 ? control.host.referenceSourceIndex : 0) && !control.host.busy
                    Accessible.name: qsTr("Source %1 global frame offset").arg(String.fromCharCode(65 + sourceOffsetDelegate.sourceIdValue))
                    onValueChanged: control.host.updateSourceOffset(sourceOffsetDelegate.sourceIdValue, value)
                }
            }
        }

        ReviewActionButton {
            objectName: "estimateAlignmentButton"
            width: parent.width
            implicitHeight: 34
            text: qsTr("Estimate global frame offset")
            helpText: qsTr("Estimate one constant shift. Example: Source B is consistently two frames later than Reference.")
            enabled: control.host.graphicsReady && !control.host.busy && !control.host.alignmentAnalysisRunning && Boolean(control.host.controller && control.host.controller.canFirst)
            onClicked: control.host.controller.estimateAlignment()
        }

        ReviewActionButton {
            objectName: "analyzeSequenceButton"
            width: parent.width
            implicitHeight: 34
            text: control.host.alignmentAnalysisRunning ? qsTr("Cancel analysis") : qsTr("Analyze missing / duplicate frames")
            helpText: qsTr("Scan for drops, duplicates, and local timing changes. Example: find one missing frame near a scene cut.")
            enabled: control.host.graphicsReady && !control.host.busy && Boolean(control.host.controller && (control.host.alignmentAnalysisRunning || control.host.controller.canFirst))
            onClicked: control.host.alignmentAnalysisRunning ? control.host.controller.cancelAlignmentAnalysis() : control.host.controller.analyzeSequenceAlignment()
        }

        ReviewActionButton {
            objectName: "confirmAutomaticAlignmentButton"
            width: parent.width
            implicitHeight: 34
            visible: control.host.automaticAlignmentPending
            text: control.host.canConfirmAutomaticAlignment ? qsTr("Confirm proposed mapping") : qsTr("Analyze sequence before confirming")
            helpText: qsTr("Accept the proposed automatic mapping after reviewing its confidence and anomalies.")
            enabled: control.host.graphicsReady && !control.host.busy && !control.host.alignmentAnalysisRunning && control.host.canConfirmAutomaticAlignment
            onClicked: control.host.controller.confirmAutomaticAlignment()
        }

        ReviewActionButton {
            objectName: "undoAutomaticAlignmentButton"
            width: parent.width
            implicitHeight: 34
            visible: control.host.canUndoAutomaticAlignment
            text: qsTr("Undo automatic mapping")
            helpText: qsTr("Restore the mapping used before the last confirmed automatic alignment.")
            enabled: control.host.graphicsReady && !control.host.busy && !control.host.alignmentAnalysisRunning
            onClicked: control.host.controller.undoAutomaticAlignment()
        }

        ReviewActionButton {
            objectName: "manualAnchorsButton"
            width: parent.width
            implicitHeight: 34
            text: control.host.manualAnchorActive ? qsTr("Manual anchors active…") : qsTr("Edit manual anchors…")
            helpText: qsTr("Map isolated points when timing drifts. Example: Reference frame 300 maps to Source B frame 302.")
            enabled: control.host.graphicsReady && !control.host.busy && Boolean(control.host.controller && control.host.controller.canFirst)
            onClicked: control.host.openManualAnchorsDialog()
        }

        ReviewActionButton {
            objectName: "applyAlignmentButton"
            width: parent.width
            implicitHeight: 34
            text: qsTr("Apply frame offsets")
            helpText: qsTr("Apply the fixed per-source offsets shown above.")
            enabled: control.host.graphicsReady && !control.host.busy && Boolean(control.host.controller && control.host.controller.canFirst)
            onClicked: control.host.controller.applySourceOffsets(control.host.sourceOffsets())
        }

        ReviewActionButton {
            objectName: "resetAlignmentButton"
            width: parent.width
            implicitHeight: 34
            text: qsTr("Return to Strict Index")
            helpText: qsTr("Clear fixed offsets and compare the same canonical index across all sources.")
            enabled: control.host.graphicsReady && !control.host.busy && (control.host.anyManualAlignmentActive || control.host.autoAlignmentActive) && Boolean(control.host.controller && control.host.controller.canFirst)
            onClicked: {
                control.host.resetSourceOffsets();
                control.host.controller.applySourceOffsets(control.host.sourceOffsets());
            }
        }

        Text {
            visible: control.host.anyManualAlignmentActive || control.host.autoAlignmentActive
            text: qsTr("Missing mapped frames stay black; offsets are never clamped.")
            color: control.host.mutedTextColor
            font.pixelSize: 10
            width: parent.width
            wrapMode: Text.WordWrap
        }
    }

    Text {
        visible: control.host.compatibilityDetails().length > 0
        text: control.host.compatibilityDetails()
        color: "#efbf83"
        font.pixelSize: 10
        wrapMode: Text.WordWrap
        anchors {
            left: parent.left
            leftMargin: 14
            right: parent.right
            rightMargin: 14
            bottom: parent.bottom
            bottomMargin: 10
        }
    }
}
