pragma ComponentBehavior: Bound

import QtQuick
import "VcsTheme.js" as Theme

Item {
    id: control

    required property int controllerState // 0 pinned, 1 auto, 2 hidden
    required property bool playing
    required property bool timelineEnabled
    required property int currentFrame
    required property int totalFrames
    required property real progress
    required property string timecodeText
    required property var markers
    required property var actions
    required property Item focusTarget
    required property bool canFirst
    required property bool canPrevious
    required property bool canPlay
    required property bool canPause
    required property bool canNext
    required property bool canLast
    property int inFrame: -1
    property int outFrame: -1
    property bool loopRangeActive: false
    property int previewFrame: -1
    property string previewTimecode: "00:00:00:00"
    property url previewThumbnailSource: ""
    property bool revealActive: controllerState === 0
    readonly property bool controlsEnabled: controllerState === 0 || revealActive

    signal previewRequested(int frame)
    signal seekRequested(int frame)

    objectName: "transport"
    height: 120
    visible: controllerState !== 2

    onControllerStateChanged: {
        hideTimer.stop();
        revealActive = controllerState === 0;
    }

    function reveal() {
        if (controllerState !== 1)
            return;
        revealActive = true;
        hideTimer.restart();
    }

    Rectangle {
        id: panel

        objectName: "oscPanel"
        anchors.fill: parent
        opacity: control.controlsEnabled ? 1.0 : 0.0
        enabled: control.controlsEnabled
        color: Theme.oscPanel
        border.color: "#384860"

        Behavior on opacity {
            NumberAnimation {
                duration: 140
            }
        }

        HoverHandler {
            onHoveredChanged: {
                if (hovered) {
                    control.revealActive = true;
                    hideTimer.stop();
                } else if (control.controllerState === 1) {
                    hideTimer.restart();
                }
            }
        }

        Row {
            id: readout

            spacing: 14
            anchors {
                top: parent.top
                topMargin: 8
                left: parent.left
                leftMargin: 16
            }

            Text {
                text: control.timecodeText
                color: Theme.primaryText
                font.family: "Consolas"
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                text: control.currentFrame >= 0 ? qsTr("Frame %1 / %2").arg(control.currentFrame + 1).arg(control.totalFrames) : qsTr("No frame")
                color: Theme.mutedText
                font.pixelSize: 12
            }
            Text {
                visible: control.inFrame >= 0 || control.outFrame >= 0
                text: qsTr("In %1  Out %2%3").arg(control.inFrame >= 0 ? control.inFrame + 1 : "—").arg(control.outFrame >= 0 ? control.outFrame + 1 : "—").arg(control.loopRangeActive ? qsTr("  ·  LOOP") : "")
                color: control.loopRangeActive ? "#7dd3fc" : "#9fc3ff"
                font.pixelSize: 11
            }
        }

        TimelineTracks {
            id: tracks

            markers: control.markers
            totalFrames: control.totalFrames
            progress: control.progress
            enabled: control.timelineEnabled && control.controlsEnabled
            inFrame: control.inFrame
            outFrame: control.outFrame
            anchors {
                left: parent.left
                leftMargin: 16
                right: parent.right
                rightMargin: 16
                top: readout.bottom
                topMargin: 2
            }
            onPreviewRequested: frame => control.previewRequested(frame)
            onSeekRequested: frame => control.seekRequested(frame)
        }

        TimelineThumbnailPopup {
            visible: tracks.hoverFrame >= 0
            previewFrame: Math.max(0, control.previewFrame)
            previewTimecode: control.previewTimecode
            thumbnailSource: control.previewThumbnailSource
            comparisonState: control.markers.length > 0 ? qsTr("Analysis markers available") : ""
            x: Math.max(8, Math.min(control.width - width - 8, tracks.x + tracks.positionForFrame(tracks.hoverFrame) * tracks.width - width / 2))
            y: -height - 6
            z: 20
        }

        TransportBar {
            objectName: "transportBar"
            compact: true
            anchors {
                bottom: parent.bottom
                bottomMargin: 5
                horizontalCenter: parent.horizontalCenter
            }
            canFirst: control.canFirst
            canPrevious: control.canPrevious
            canPlay: control.canPlay
            canPause: control.canPause
            canNext: control.canNext
            canLast: control.canLast
            playing: control.playing
            focusTarget: control.focusTarget
            onFirstRequested: control.actions.firstFrame()
            onPreviousSecondRequested: control.actions.stepBackwardSecond()
            onPreviousFiveRequested: control.actions.stepBackwardFive()
            onPreviousRequested: control.actions.previousFrame()
            onPlaybackRequested: control.actions.togglePlayback()
            onNextRequested: control.actions.nextFrame()
            onNextFiveRequested: control.actions.stepForwardFive()
            onNextSecondRequested: control.actions.stepForwardSecond()
            onLastRequested: control.actions.lastFrame()
        }
    }

    Item {
        id: wakeArea

        objectName: "oscWakeArea"
        visible: control.controllerState === 1 && !control.controlsEnabled
        height: 10
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        HoverHandler {
            onHoveredChanged: {
                if (hovered) {
                    control.revealActive = true;
                    hideTimer.stop();
                }
            }
        }
    }

    Timer {
        id: hideTimer

        interval: 1200
        repeat: false
        onTriggered: {
            if (control.controllerState === 1)
                control.revealActive = false;
        }
    }
}
