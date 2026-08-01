pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
// qmllint disable import
import Dvs.Ui 1.0

// qmllint enable import

Rectangle {
    id: control

    required property int sourceCount
    required property int currentMode
    required property var differenceEdges
    required property int currentEdgeIndex
    required property bool inspectorOpen
    required property bool busy
    property color panelColor: "#171e2a"
    property color borderColor: "#303d51"
    property color accentColor: "#4b8df8"
    property color textColor: "#f3f6fb"

    signal modeRequested(int mode)
    signal edgeRequested(int preferenceValue)
    signal inspectorRequested

    objectName: "compareModeBar"
    height: sourceCount > 1 ? 40 : 0
    visible: sourceCount > 1
    color: panelColor
    border.color: borderColor

    Row {
        spacing: 6
        anchors {
            left: parent.left
            leftMargin: 12
            verticalCenter: parent.verticalCenter
        }

        // qmllint disable import unqualified unresolved-type
        ModeButton {
            objectName: "sideModeButton"
            text: qsTr("Side")
            modeValue: ComparisonSurface.SideBySide
        }
        ModeButton {
            objectName: "wipeModeButton"
            text: qsTr("Wipe")
            modeValue: ComparisonSurface.Wipe
        }
        ModeButton {
            objectName: "diffModeButton"
            text: qsTr("Diff")
            modeValue: ComparisonSurface.Difference
        }
        // qmllint enable import unqualified unresolved-type

        ComboBox {
            objectName: "pairCombo"
            visible: control.sourceCount === 3
            implicitWidth: 92
            implicitHeight: 30
            model: control.differenceEdges
            textRole: "label"
            currentIndex: control.currentEdgeIndex
            Accessible.name: qsTr("Comparison pair")
            onActivated: index => {
                if (index >= 0 && index < control.differenceEdges.length)
                    control.edgeRequested(Number(control.differenceEdges[index].preferenceValue));
            }
        }

        ToolButton {
            objectName: "moreCompareModesButton"
            text: "…"
            implicitWidth: 36
            implicitHeight: 30
            onClicked: moreMenu.open()

            Menu {
                id: moreMenu

                // qmllint disable import unqualified unresolved-type
                MenuItem {
                    text: qsTr("Three up")
                    enabled: control.sourceCount === 3
                    onTriggered: control.modeRequested(ComparisonSurface.ThreeUp)
                }
                MenuItem {
                    text: qsTr("Reference focus")
                    enabled: control.sourceCount === 3
                    onTriggered: control.modeRequested(ComparisonSurface.ReferenceFocus)
                }
                MenuItem {
                    text: qsTr("Analysis grid")
                    enabled: control.sourceCount === 3
                    onTriggered: control.modeRequested(ComparisonSurface.AnalysisGrid)
                }
                // qmllint enable import unqualified unresolved-type
            }
        }
    }

    component ModeButton: Button {
        id: modeButton

        required property int modeValue
        property bool blocksGlobalMediaShortcuts: true
        checkable: true
        checked: control.currentMode === modeValue
        implicitHeight: 30
        implicitWidth: 64
        enabled: !control.busy
        onClicked: control.modeRequested(modeValue)
    }

    Button {
        objectName: "inspectorToggleButton"
        text: control.inspectorOpen ? qsTr("Hide inspector") : qsTr("Inspector")
        implicitHeight: 30
        onClicked: control.inspectorRequested()
        anchors {
            right: parent.right
            rightMargin: 12
            verticalCenter: parent.verticalCenter
        }
    }
}
