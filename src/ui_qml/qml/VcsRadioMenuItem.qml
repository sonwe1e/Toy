pragma ComponentBehavior: Bound

import QtQuick

VcsMenuItem {
    checkable: true
    autoExclusive: true
    radioIndicator: true
    Accessible.role: Accessible.RadioButton
}
