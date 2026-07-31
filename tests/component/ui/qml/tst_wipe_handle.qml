import QtQuick
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    width: 720
    height: 400

    Item {
        id: surface

        x: 40
        y: 40
        width: 640
        height: 320
    }

    Dvs.WipeHandle {
        id: handle

        surfaceItem: surface
        position: 0.5
    }

    TestCase {
        name: "WipeHandle"
        when: windowShown

        function test_geometry_tracks_surface_split() {
            const knob = findChild(handle, "wipeKnob");
            verify(knob !== null);
            compare(handle.width, 52);
            compare(knob.width, 20);
            compare(knob.height, 84);

            for (const position of [0.25, 0.5, 0.75]) {
                handle.position = position;
                wait(0);
                const actualCenter = handle.x + handle.width / 2;
                const expectedCenter = surface.x + surface.width * position;
                verify(Math.abs(actualCenter - expectedCenter) * Screen.devicePixelRatio <= 1);
            }
        }
    }
}
