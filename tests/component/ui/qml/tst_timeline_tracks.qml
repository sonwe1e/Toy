import QtQuick
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    width: 600
    height: 120

    Dvs.TimelineTracks {
        id: tracks
        width: 560
        markers: [{"frame": 30, "kind": "missing", "source": "B", "confidence": 12}]
        totalFrames: 100
        progress: 0.5
        inFrame: 0
        outFrame: 99
        anchors.centerIn: root
    }

    TestCase {
        name: "TimelineTracks"
        when: windowShown

        function test_range_is_clipped_to_zoom_window() {
            tracks.setZoom(4, 0.5);
            const range = findChild(tracks, "rangeHighlight");
            verify(range !== null);
            verify(range.visible);
            verify(range.x >= 0);
            verify(range.x + range.width <= tracks.width + 0.5);
        }

        function test_preview_signal_uses_visible_window_frame() {
            const expected = tracks.frameAt(0.75);
            verify(expected >= tracks.windowStartFrame);
            verify(expected < tracks.windowStartFrame + tracks.visibleFrameCount);
        }
    }
}
