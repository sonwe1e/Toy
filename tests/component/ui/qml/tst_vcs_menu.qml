pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    width: 960
    height: 640

    Dvs.VcsMenu {
        id: rootMenu

        objectName: "rootMenu"
        title: "Root"

        Dvs.VcsMenuItem {
            objectName: "rootMenuItem"
            text: "Root action"
        }

        Dvs.VcsMenu {
            id: nestedMenu

            objectName: "nestedMenu"
            title: "Nested"

            Dvs.VcsMenuItem {
                objectName: "nestedMenuItem"
                text: "Nested action"
            }
        }
    }

    TestCase {
        name: "VcsMenu"
        when: windowShown

        function cleanup() {
            nestedMenu.close();
            rootMenu.close();
            wait(0);
        }

        function test_uses_non_native_opaque_popup_and_custom_delegate() {
            compare(rootMenu.popupType, Popup.Window);
            compare(nestedMenu.popupType, Popup.Window);
            compare(rootMenu.background.color.a, 1.0);
            compare(nestedMenu.background.color.a, 1.0);
            verify(rootMenu.popupInputContext);
            verify(findChild(rootMenu, "rootMenuItem").popupInputContext);

            rootMenu.popup(root, 24, 24);
            tryCompare(rootMenu, "opened", true);
            verify(rootMenu.x >= 0);
            verify(rootMenu.y >= 0);

            nestedMenu.popup(root, 320, 24);
            tryCompare(nestedMenu, "opened", true);
            verify(nestedMenu.x >= 0);
            verify(nestedMenu.y >= 0);
        }

        function test_escape_closes_popup() {
            rootMenu.popup(root, 24, 24);
            tryCompare(rootMenu, "opened", true);
            keyClick(Qt.Key_Escape);
            tryCompare(rootMenu, "opened", false);
        }
    }
}
