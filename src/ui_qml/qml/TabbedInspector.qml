pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// qmllint disable import
import Dvs.Ui 1.0

// qmllint enable import

Rectangle {
    id: control

    required property var controller
    required property var preferences
    required property var session
    required property var alignmentHost
    required property color borderColor
    required property color primaryTextColor
    required property color mutedTextColor
    required property bool singleMode
    required property int sourceCount
    required property bool wipeMode
    required property bool differenceMode
    required property bool analysisGridMode
    required property var differenceEdges
    required property int differenceEdge
    required property int referenceSourceIndex
    required property bool differenceThresholdEnabled
    required property int differenceThresholdCode
    required property int differenceThresholdPolicy
    required property real wipePosition
    required property bool roiEnabled
    required property bool graphicsReady
    required property bool dropFrameTimecode
    required property int currentFrame
    required property int inFrame
    required property int outFrame
    required property bool rangePlaybackActive

    signal differenceEdgeRequested(int edge)
    signal referenceRequested(int sourceIndex)
    signal differenceThresholdEnabledRequested(bool enabled)
    signal differenceThresholdCodeRequested(int code)
    signal differenceThresholdPolicyRequested(int policy)
    signal wipePositionRequested(real position)
    signal resetViewportRequested
    signal clearRoiRequested
    signal dropFrameTimecodeRequested(bool enabled)
    signal inPointRequested
    signal outPointRequested
    signal clearRangeRequested
    signal rangeLoopToggleRequested

    objectName: "tabbedInspector"
    width: Math.max(300, Math.min(380, parent ? parent.width * 0.3 : 360))
    color: "#111823"
    border.color: control.borderColor

    readonly property int effectiveTab: control.singleMode && tabs.currentIndex < 2 ? 2 : tabs.currentIndex

    function differenceEdgeIndex(preferenceValue) {
        for (let index = 0; index < control.differenceEdges.length; ++index) {
            if (Number(control.differenceEdges[index].preferenceValue) === Number(preferenceValue))
                return index;
        }
        return 0;
    }

    component DarkTabButton: TabButton {
        id: darkTabButton

        leftPadding: 10
        rightPadding: 10
        topPadding: 8
        bottomPadding: 8

        contentItem: Text {
            text: darkTabButton.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: darkTabButton.checked ? "#f3f6fb" : (darkTabButton.hovered ? "#f3f6fb" : "#93a2ba")
            font.pixelSize: 12
            font.weight: darkTabButton.checked ? Font.DemiBold : Font.Normal
        }

        background: Rectangle {
            color: darkTabButton.checked ? "#243f68" : (darkTabButton.hovered ? "#253247" : "transparent")

            Rectangle {
                visible: darkTabButton.checked
                height: 2
                color: "#4b8df8"
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                    leftMargin: 8
                    rightMargin: 8
                }
            }
        }
    }

    component DarkCheckBox: CheckBox {
        id: darkCheckBox

        leftPadding: 0
        spacing: 8

        contentItem: Text {
            text: darkCheckBox.text
            font.pixelSize: 12
            color: darkCheckBox.enabled ? "#f3f6fb" : "#637086"
            verticalAlignment: Text.AlignVCenter
            leftPadding: darkCheckBox.indicator.width + darkCheckBox.spacing
        }

        indicator: Rectangle {
            x: darkCheckBox.leftPadding
            y: darkCheckBox.height / 2 - height / 2
            implicitWidth: 16
            implicitHeight: 16
            radius: 3
            color: darkCheckBox.checked ? "#4b8df8" : "#1d2635"
            border.color: darkCheckBox.checked ? "#4b8df8" : (darkCheckBox.hovered ? "#4b8df8" : "#40516a")
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: darkCheckBox.checked ? "✓" : ""
                color: "#ffffff"
                font.pixelSize: 10
                font.bold: true
            }
        }
    }

    TabBar {
        id: tabs

        objectName: "inspectorTabBar"
        width: parent.width
        background: Rectangle {
            color: "#171e2a"

            Rectangle {
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                height: 1
                color: "#303d51"
            }
        }
        DarkTabButton {
            objectName: "compareTabButton"
            visible: !control.singleMode
            text: qsTr("Compare")
        }
        DarkTabButton {
            objectName: "alignmentTabButton"
            visible: !control.singleMode
            text: qsTr("Alignment")
        }
        DarkTabButton {
            objectName: "reviewTabButton"
            text: qsTr("Playback")
        }
        DarkTabButton {
            objectName: "infoTabButton"
            text: qsTr("Info")
        }
    }

    StackLayout {
        currentIndex: control.effectiveTab
        anchors {
            top: tabs.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        Flickable {
            clip: true
            contentHeight: compareColumn.implicitHeight + 28

            Column {
                id: compareColumn

                width: parent.width - 28
                spacing: 10
                x: 14
                y: 14

                Label {
                    text: qsTr("Comparison")
                    color: control.primaryTextColor
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Label {
                    text: qsTr("Pair")
                    visible: pairCombo.visible
                    color: control.mutedTextColor
                }
                ToolbarCombo {
                    id: pairCombo
                    visible: control.sourceCount === 3 && (control.wipeMode || control.differenceMode || control.analysisGridMode)
                    width: parent.width
                    model: control.differenceEdges
                    textRole: "label"
                    currentIndex: control.differenceEdgeIndex(control.differenceEdge)
                    onActivated: index => {
                        if (index >= 0 && index < control.differenceEdges.length)
                            control.differenceEdgeRequested(Number(control.differenceEdges[index].preferenceValue));
                    }
                }

                Label {
                    text: qsTr("Reference")
                    color: control.mutedTextColor
                }
                ToolbarCombo {
                    width: parent.width
                    model: control.controller ? control.controller.sources : null
                    textRole: "filename"
                    currentIndex: Math.max(0, control.referenceSourceIndex)
                    onActivated: index => control.referenceRequested(index)
                }

                Label {
                    visible: control.differenceMode
                    text: qsTr("Difference metric")
                    color: control.mutedTextColor
                }
                ToolbarCombo {
                    visible: control.differenceMode
                    width: parent.width
                    model: [qsTr("RGB absolute"), qsTr("Luma"), qsTr("Chroma"), qsTr("Heatmap"), qsTr("Exact planes")]
                    currentIndex: Number(control.preferences.differenceMetric)
                    onActivated: index => control.preferences.differenceMetric = index
                }

                Label {
                    visible: control.differenceMode
                    text: qsTr("Gain")
                    color: control.mutedTextColor
                }
                ToolbarCombo {
                    visible: control.differenceMode
                    width: parent.width
                    model: [qsTr("1×"), qsTr("2×"), qsTr("4×"), qsTr("8×"), qsTr("16×")]
                    currentIndex: Number(control.preferences.differenceGain)
                    onActivated: index => control.preferences.differenceGain = index
                }

                Label {
                    visible: control.differenceMode
                    text: qsTr("Filter")
                    color: control.mutedTextColor
                }
                ToolbarCombo {
                    visible: control.differenceMode
                    width: parent.width
                    model: [qsTr("Nearest"), qsTr("Bilinear"), qsTr("Bicubic")]
                    currentIndex: Number(control.preferences.differenceFilter)
                    onActivated: index => control.preferences.differenceFilter = index
                }

                DarkCheckBox {
                    visible: control.differenceMode
                    text: qsTr("Threshold")
                    checked: control.differenceThresholdEnabled
                    onToggled: control.differenceThresholdEnabledRequested(checked)
                }
                SpinBox {
                    id: thresholdSpinBox

                    property bool blocksGlobalMediaShortcuts: true
                    property bool textEditingInputContext: true
                    visible: control.differenceMode && control.differenceThresholdEnabled
                    width: parent.width
                    from: 0
                    to: 255
                    value: control.differenceThresholdCode
                    editable: true
                    implicitHeight: 34
                    Accessible.name: qsTr("Difference threshold in 8-bit code values")
                    onValueModified: control.differenceThresholdCodeRequested(value)

                    contentItem: TextInput {
                        text: thresholdSpinBox.textFromValue(thresholdSpinBox.value, thresholdSpinBox.locale)
                        color: thresholdSpinBox.enabled ? "#f3f6fb" : "#637086"
                        selectionColor: "#4b8df8"
                        selectedTextColor: "#ffffff"
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        readOnly: !thresholdSpinBox.editable
                        validator: thresholdSpinBox.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }

                    up.indicator: Rectangle {
                        x: thresholdSpinBox.mirrored ? 0 : thresholdSpinBox.width - width
                        height: thresholdSpinBox.height
                        implicitWidth: 32
                        implicitHeight: 40
                        color: thresholdSpinBox.up.pressed ? "#285da9" : "#253247"
                        border.color: "#3b4d67"
                        border.width: 1

                        Rectangle {
                            x: (parent.width - width) / 2
                            y: (parent.height - height) / 2
                            width: parent.width / 3
                            height: 2
                            color: "#f3f6fb"
                        }
                        Rectangle {
                            x: (parent.width - width) / 2
                            y: (parent.height - height) / 2
                            width: 2
                            height: parent.width / 3
                            color: "#f3f6fb"
                        }
                    }

                    down.indicator: Rectangle {
                        x: thresholdSpinBox.mirrored ? thresholdSpinBox.width - width : 0
                        height: thresholdSpinBox.height
                        implicitWidth: 32
                        implicitHeight: 40
                        color: thresholdSpinBox.down.pressed ? "#285da9" : "#253247"
                        border.color: "#3b4d67"
                        border.width: 1

                        Rectangle {
                            x: (parent.width - width) / 2
                            y: (parent.height - height) / 2
                            width: parent.width / 3
                            height: 2
                            color: "#f3f6fb"
                        }
                    }

                    background: Rectangle {
                        radius: 4
                        color: thresholdSpinBox.enabled ? "#1d2635" : "#202938"
                        border.color: thresholdSpinBox.activeFocus ? "#4b8df8" : "#303d51"
                    }
                }
                ToolbarCombo {
                    visible: control.differenceMode && control.differenceThresholdEnabled
                    width: parent.width
                    model: [qsTr("Luma"), qsTr("Any channel"), qsTr("All channels")]
                    currentIndex: control.differenceThresholdPolicy
                    onActivated: index => control.differenceThresholdPolicyRequested(index)
                }

                Label {
                    visible: control.wipeMode
                    text: qsTr("Wipe position · %1%").arg(Math.round(control.wipePosition * 100))
                    color: control.mutedTextColor
                }
                Slider {
                    id: wipeSlider

                    visible: control.wipeMode
                    width: parent.width
                    from: 0
                    to: 1
                    value: control.wipePosition
                    padding: 6
                    onMoved: control.wipePositionRequested(value)

                    background: Rectangle {
                        x: wipeSlider.leftPadding + (wipeSlider.horizontal ? 0 : (wipeSlider.availableWidth - width) / 2)
                        y: wipeSlider.topPadding + (wipeSlider.horizontal ? (wipeSlider.availableHeight - height) / 2 : 0)
                        width: wipeSlider.horizontal ? wipeSlider.availableWidth : implicitWidth
                        height: wipeSlider.horizontal ? implicitHeight : wipeSlider.availableHeight
                        implicitWidth: 200
                        implicitHeight: 6
                        radius: 3
                        color: "#40516a"

                        Rectangle {
                            y: wipeSlider.horizontal ? 0 : wipeSlider.visualPosition * parent.height
                            width: wipeSlider.horizontal ? wipeSlider.position * parent.width : 6
                            height: wipeSlider.horizontal ? 6 : wipeSlider.position * parent.height
                            radius: 3
                            color: "#4b8df8"
                        }
                    }

                    handle: Rectangle {
                        x: wipeSlider.leftPadding + (wipeSlider.horizontal ? wipeSlider.visualPosition * (wipeSlider.availableWidth - width) : (wipeSlider.availableWidth - width) / 2)
                        y: wipeSlider.topPadding + (wipeSlider.horizontal ? (wipeSlider.availableHeight - height) / 2 : wipeSlider.visualPosition * (wipeSlider.availableHeight - height))
                        implicitWidth: 16
                        implicitHeight: 16
                        radius: width / 2
                        color: wipeSlider.pressed ? "#285da9" : "#4b8df8"
                        border.width: wipeSlider.activeFocus ? 2 : 1
                        border.color: wipeSlider.activeFocus ? "#b7d3ff" : "#f3f6fb"
                    }
                }

                ReviewActionButton {
                    width: parent.width
                    implicitHeight: 34
                    text: qsTr("Reset zoom and pan")
                    helpText: qsTr("Restore the full image view.")
                    onClicked: control.resetViewportRequested()
                }
                ReviewActionButton {
                    visible: control.roiEnabled
                    width: parent.width
                    implicitHeight: 34
                    text: qsTr("Clear ROI")
                    helpText: qsTr("Remove the active region of interest.")
                    onClicked: control.clearRoiRequested()
                }
            }
        }

        AlignmentInspector {
            host: control.alignmentHost
        }

        Flickable {
            clip: true
            contentHeight: reviewColumn.implicitHeight + 28

            Column {
                id: reviewColumn

                width: parent.width - 28
                spacing: 10
                x: 14
                y: 14

                Label {
                    text: qsTr("Playback")
                    color: control.primaryTextColor
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("On-screen controls")
                    color: control.mutedTextColor
                }
                ToolbarCombo {
                    width: parent.width
                    model: [qsTr("Contextual"), qsTr("Pinned"), qsTr("Auto hide"), qsTr("Hidden")]
                    currentIndex: control.preferences && Number(control.preferences.oscMode) >= 0 ? Number(control.preferences.oscMode) + 1 : 0
                    onActivated: index => control.preferences.oscMode = index - 1
                }
                DarkCheckBox {
                    visible: control.controller && control.controller.dropFrameTimecodeAvailable
                    text: checked ? qsTr("Drop-frame timecode (DF)") : qsTr("Non-drop timecode (NDF)")
                    checked: control.dropFrameTimecode
                    onToggled: control.dropFrameTimecodeRequested(checked)
                }
                Label {
                    text: qsTr("In: %1").arg(control.inFrame >= 0 ? control.inFrame + 1 : "—")
                    color: control.mutedTextColor
                }
                Label {
                    text: qsTr("Out: %1").arg(control.outFrame >= 0 ? control.outFrame + 1 : "—")
                    color: control.mutedTextColor
                }

                GridLayout {
                    width: parent.width
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8

                    ReviewActionButton {
                        objectName: "setInButton"
                        Layout.fillWidth: true
                        implicitHeight: 34
                        text: qsTr("Set In")
                        helpText: qsTr("Set the range start to the current frame (I).")
                        enabled: control.currentFrame >= 0
                        onClicked: control.inPointRequested()
                    }
                    ReviewActionButton {
                        objectName: "setOutButton"
                        Layout.fillWidth: true
                        implicitHeight: 34
                        text: qsTr("Set Out")
                        helpText: qsTr("Set the range end to the current frame (O).")
                        enabled: control.currentFrame >= 0
                        onClicked: control.outPointRequested()
                    }
                    ReviewActionButton {
                        objectName: "clearRangeButton"
                        Layout.fillWidth: true
                        implicitHeight: 34
                        text: qsTr("Clear range")
                        helpText: qsTr("Clear In, Out, and loop playback.")
                        enabled: control.inFrame >= 0 || control.outFrame >= 0
                        onClicked: control.clearRangeRequested()
                    }
                    ReviewActionButton {
                        objectName: "loopRangeButton"
                        Layout.fillWidth: true
                        implicitHeight: 34
                        text: control.rangePlaybackActive ? qsTr("Stop loop") : qsTr("Loop range")
                        helpText: qsTr("Toggle playback of the selected range.")
                        enabled: control.inFrame >= 0 && control.outFrame >= control.inFrame
                        onClicked: control.rangeLoopToggleRequested()
                    }
                }

                Label {
                    text: qsTr("Marker legend")
                    color: control.primaryTextColor
                    font.weight: Font.DemiBold
                }
                Label {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Red · Missing    Orange · Duplicate    Purple · Extra\nCyan · Anchor    Yellow · Low confidence")
                    color: control.mutedTextColor
                }
                Label {
                    objectName: "markerOverflowNotice"
                    width: parent.width
                    visible: Boolean(control.controller) && Number(control.controller.alignmentTimelineMarkerOverflowCount) > 0
                    wrapMode: Text.WordWrap
                    text: qsTr("%1 additional alignment markers are not shown on the timeline.").arg(Number(control.controller.alignmentTimelineMarkerOverflowCount))
                    color: "#efbf83"
                }
            }
        }

        Flickable {
            clip: true
            contentHeight: infoColumn.implicitHeight + 28

            Column {
                id: infoColumn

                width: parent.width - 28
                spacing: 10
                x: 14
                y: 14

                Label {
                    text: qsTr("Media information")
                    color: control.primaryTextColor
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Repeater {
                    objectName: "mediaInfoRepeater"
                    model: control.controller ? control.controller.sourceMediaInfo : []

                    Rectangle {
                        id: mediaCard

                        required property var modelData
                        width: infoColumn.width
                        height: mediaInfo.implicitHeight + 20
                        radius: 6
                        color: "#1d2635"
                        border.color: control.borderColor

                        Label {
                            id: mediaInfo
                            width: parent.width - 20
                            x: 10
                            y: 10
                            wrapMode: Text.Wrap
                            readonly property string decoderFallbackSuffix: mediaCard.modelData.decodeFallbackReason ? qsTr(" (%1)").arg(String(mediaCard.modelData.decodeFallbackReason)) : ""
                            text: qsTr("Source %1 · %2\n%3 × %4 · %5 · %6 frames\n%7 · %8 · %9-bit\n%10 · %11 · %12\nDecode: %13%14 · Role: %15").arg(String(mediaCard.modelData.label)).arg(String(mediaCard.modelData.filename)).arg(Number(mediaCard.modelData.width)).arg(Number(mediaCard.modelData.height)).arg(String(mediaCard.modelData.frameRate)).arg(Number(mediaCard.modelData.frameCount)).arg(String(mediaCard.modelData.timingMode)).arg(String(mediaCard.modelData.codec)).arg(Number(mediaCard.modelData.bitDepth)).arg(String(mediaCard.modelData.pixelFormat)).arg(String(mediaCard.modelData.colorMatrix)).arg(String(mediaCard.modelData.colorRange)).arg(String(mediaCard.modelData.decodeBackend)).arg(decoderFallbackSuffix).arg(String(mediaCard.modelData.role))
                            color: control.mutedTextColor
                            font.pixelSize: 11
                        }
                    }
                }
                Label {
                    text: control.graphicsReady ? qsTr("D3D11 renderer ready") : qsTr("Graphics unavailable")
                    color: control.graphicsReady ? "#8ce2c2" : "#efbf83"
                }
            }
        }
    }
}
