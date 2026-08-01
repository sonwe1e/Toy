import QtQuick
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    width: 960
    height: 640

    Dvs.EmptyReviewView {
        id: emptyView
        anchors.fill: parent
        accentColor: "#4b8df8"
        textColor: "#f3f6fb"
        mutedTextColor: "#93a2ba"
    }

    Dvs.PlayerOsc {
        id: hiddenOsc
        width: root.width
        controllerState: 2
        playing: false
        timelineEnabled: false
        currentFrame: -1
        totalFrames: 0
        progress: 0
        timecodeText: "00:00:00:00"
        markers: []
        actions: QtObject {}
        focusTarget: emptyView
        canFirst: false
        canPrevious: false
        canPlay: false
        canPause: false
        canNext: false
        canLast: false
        anchors.bottom: root.bottom
    }

    TestCase {
        name: "EmptyReviewView"
        when: windowShown

        function test_empty_state_has_no_transport() {
            verify(emptyView.visible);
            verify(!hiddenOsc.visible);
        }
    }
}
