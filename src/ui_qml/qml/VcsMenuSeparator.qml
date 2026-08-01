pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Divider row inside a VcsMenu popup. The inset line matches the menu's horizontal padding.
MenuSeparator {
    id: control

    property color separatorColor: "#40516a"
    property real separatorInset: 12

    topPadding: 5
    bottomPadding: 5

    contentItem: Item {
        implicitHeight: 1

        Rectangle {
            height: 1
            color: control.separatorColor
            anchors {
                left: parent.left
                leftMargin: control.separatorInset
                right: parent.right
                rightMargin: control.separatorInset
            }
        }
    }
}
