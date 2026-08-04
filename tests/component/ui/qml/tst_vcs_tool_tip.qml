pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    width: 480
    height: 240

    Rectangle {
        anchors.fill: parent
        color: "#ffffff"
    }

    Item {
        id: anchorItem

        width: 80
        height: 32
        anchors.centerIn: parent
    }

    Dvs.VcsToolTip {
        id: toolTip

        parent: anchorItem
        text: "Opaque tooltip"
    }

    TestCase {
        name: "VcsToolTip"
        when: windowShown

        function cleanup() {
            toolTip.close();
            wait(0);
        }

        function test_uses_opaque_item_popup() {
            compare(toolTip.popupType, Popup.Item);
            compare(toolTip.background.color.a, 1.0);

            toolTip.open();
            tryCompare(toolTip, "visible", true);
            wait(50);

            const content = root.Window.window.contentItem;
            const image = grabImage(content);
            const center = toolTip.background.mapToItem(content, toolTip.background.width / 2, toolTip.background.height / 2);
            const scaleX = image.width / content.width;
            const scaleY = image.height / content.height;
            const x = Math.max(0, Math.min(image.width - 1, Math.round(center.x * scaleX)));
            const y = Math.max(0, Math.min(image.height - 1, Math.round(center.y * scaleY)));
            compare(image.alpha(x, y), 255);
        }
    }
}
