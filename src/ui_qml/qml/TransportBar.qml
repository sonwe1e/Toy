pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: control

    property bool canFirst: false
    property bool canPrevious: false
    property bool canPlay: false
    property bool canPause: false
    property bool canNext: false
    property bool canLast: false
    property bool playing: false
    property Item focusTarget: null
    property bool compact: false

    signal firstRequested
    signal previousSecondRequested
    signal previousFiveRequested
    signal previousRequested
    signal playbackRequested
    signal nextRequested
    signal nextFiveRequested
    signal nextSecondRequested
    signal lastRequested

    implicitWidth: buttons.implicitWidth
    implicitHeight: buttons.implicitHeight

    function restoreFocus() {
        if (focusTarget)
            focusTarget.forceActiveFocus();
    }

    component TransportButton: Button {
        id: button

        required property url iconSource
        required property string helpText

        implicitWidth: control.compact ? 38 : 48
        implicitHeight: control.compact ? 34 : 42
        activeFocusOnTab: true
        Accessible.name: helpText.split("\n")[0]
        Accessible.description: helpText

        contentItem: Image {
            source: button.iconSource
            sourceSize.width: control.compact ? 20 : 24
            sourceSize.height: control.compact ? 20 : 24
            fillMode: Image.PreserveAspectFit
            opacity: button.enabled ? 1.0 : 0.35
        }

        background: Rectangle {
            radius: 6
            color: !button.enabled ? "#ff202938" : (button.down ? "#ff285da9" : (button.hovered ? "#ff2d69bf" : "#ff253247"))
            border.width: button.activeFocus ? 2 : 1
            border.color: button.activeFocus ? "#ff4b8df8" : (button.enabled ? "#ff3b4d67" : "#ff2a3444")
        }

        ToolTip.visible: hovered
        ToolTip.delay: 650
        ToolTip.text: helpText
    }

    Row {
        id: buttons

        spacing: control.compact ? 4 : 6

        TransportButton {
            id: firstButton

            objectName: "firstButton"
            iconSource: "qrc:/icons/first.svg"
            helpText: qsTr("First frame\nShortcut: Home")
            enabled: control.canFirst
            onClicked: {
                control.firstRequested();
                control.restoreFocus();
            }
        }
        TransportButton {
            objectName: "previousSecondButton"
            iconSource: "qrc:/icons/previous-second.svg"
            helpText: qsTr("Back 1 second\nShortcut: Ctrl+Left / Ctrl+A")
            enabled: control.canPrevious
            onClicked: {
                control.previousSecondRequested();
                control.restoreFocus();
            }
        }
        TransportButton {
            objectName: "previousFiveButton"
            iconSource: "qrc:/icons/previous-five.svg"
            helpText: qsTr("Back 5 frames\nShortcut: Shift+Left / Shift+A")
            enabled: control.canPrevious
            onClicked: {
                control.previousFiveRequested();
                control.restoreFocus();
            }
        }
        TransportButton {
            id: previousButton

            objectName: "previousButton"
            iconSource: "qrc:/icons/previous.svg"
            helpText: qsTr("Previous frame\nShortcut: Left / A")
            enabled: control.canPrevious
            onClicked: {
                control.previousRequested();
                control.restoreFocus();
            }
        }
        TransportButton {
            id: playbackButton

            objectName: "playbackButton"
            implicitWidth: control.compact ? 42 : 54
            iconSource: control.playing ? "qrc:/icons/pause.svg" : "qrc:/icons/play.svg"
            helpText: control.playing ? qsTr("Pause\nShortcut: Space") : qsTr("Play\nShortcut: Space")
            enabled: control.playing ? control.canPause : control.canPlay
            onClicked: {
                control.playbackRequested();
                control.restoreFocus();
            }
        }
        TransportButton {
            id: nextButton

            objectName: "nextButton"
            iconSource: "qrc:/icons/next.svg"
            helpText: qsTr("Next frame\nShortcut: Right / D")
            enabled: control.canNext
            onClicked: {
                control.nextRequested();
                control.restoreFocus();
            }
        }
        TransportButton {
            objectName: "nextFiveButton"
            iconSource: "qrc:/icons/next-five.svg"
            helpText: qsTr("Forward 5 frames\nShortcut: Shift+Right / Shift+D")
            enabled: control.canNext
            onClicked: {
                control.nextFiveRequested();
                control.restoreFocus();
            }
        }
        TransportButton {
            objectName: "nextSecondButton"
            iconSource: "qrc:/icons/next-second.svg"
            helpText: qsTr("Forward 1 second\nShortcut: Ctrl+Right / Ctrl+D")
            enabled: control.canNext
            onClicked: {
                control.nextSecondRequested();
                control.restoreFocus();
            }
        }
        TransportButton {
            id: lastButton

            objectName: "lastButton"
            iconSource: "qrc:/icons/last.svg"
            helpText: qsTr("Last frame\nShortcut: End")
            enabled: control.canLast
            onClicked: {
                control.lastRequested();
                control.restoreFocus();
            }
        }
    }
}
