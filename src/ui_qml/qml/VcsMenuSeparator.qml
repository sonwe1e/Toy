pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "VcsTheme.js" as Theme

// Divider row inside a VcsMenu popup. The inset line matches the menu's horizontal padding.
MenuSeparator {
    id: control

    property color separatorColor: Theme.menuBorder
    property real separatorInset: 12

    implicitHeight: visible ? contentItem.implicitHeight + topPadding + bottomPadding : 0
    height: implicitHeight
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
