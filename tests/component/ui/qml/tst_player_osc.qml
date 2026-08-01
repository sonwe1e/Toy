import QtQuick
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    width: 900
    height: 400

    QtObject {
        id: actions

        function firstFrame() {}
        function stepBackwardSecond() {}
        function stepBackwardFive() {}
        function previousFrame() {}
        function togglePlayback() {}
        function nextFrame() {}
        function stepForwardFive() {}
        function stepForwardSecond() {}
        function lastFrame() {}
    }

    Item {
        id: focusTarget
    }

    Dvs.PlayerOsc {
        id: osc

        width: root.width
        controllerState: 1
        playing: false
        timelineEnabled: true
        currentFrame: 10
        totalFrames: 100
        progress: 0.1
        timecodeText: "00:00:00:10"
        markers: []
        actions: actions
        focusTarget: focusTarget
        canFirst: true
        canPrevious: true
        canPlay: true
        canPause: false
        canNext: true
        canLast: true
        anchors.bottom: root.bottom
    }

    SignalSpy {
        id: previewSpy
        target: osc
        signalName: "previewRequested"
    }

    TestCase {
        id: testCase

        name: "PlayerOsc"
        when: windowShown

        function test_auto_hide_disables_invisible_controls() {
            const panel = findChild(osc, "oscPanel");
            verify(panel !== null);
            osc.revealActive = true;
            mouseMove(osc, osc.width / 2, osc.height / 2);
            verify(osc.controlsEnabled);
            verify(panel.enabled);
            mouseMove(root, 10, 10);
            wait(1350);
            verify(!osc.controlsEnabled);
            verify(!panel.enabled);
            tryCompare(panel, "opacity", 0, 400);
        }

        function test_compact_layout_does_not_overlap() {
            osc.revealActive = true;
            const timeline = findChild(osc, "timelineSlider");
            const transport = findChild(osc, "transportBar");
            verify(timeline !== null);
            verify(transport !== null);
            verify(timeline.y + timeline.height <= transport.y);
            compare(osc.height, 120);
        }

        function test_preview_is_forwarded_as_a_signal() {
            previewSpy.clear();
            const timeline = findChild(osc, "timelineSlider");
            timeline.previewRequested(42);
            compare(previewSpy.count, 1);
            compare(previewSpy.signalArguments[0][0], 42);
        }
    }
}
