pragma ComponentBehavior: Bound

import QtQuick
import "VcsTheme.js" as Theme
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
    property color panelColor: Theme.menu
    property color borderColor: Theme.border
    property color accentColor: Theme.accent
    property color textColor: Theme.primaryText

    signal modeRequested(int mode)
    signal edgeRequested(int preferenceValue)
    signal inspectorRequested

    // qmllint disable import unqualified unresolved-type
    readonly property bool pairRelevant: currentMode === ComparisonSurface.Wipe || currentMode === ComparisonSurface.Difference || currentMode === ComparisonSurface.AnalysisGrid
    readonly property bool advancedMode: currentMode === ComparisonSurface.ThreeUp || currentMode === ComparisonSurface.ReferenceFocus || currentMode === ComparisonSurface.AnalysisGrid
    readonly property string advancedModeLabel: currentMode === ComparisonSurface.ThreeUp ? qsTr("Three up") : (currentMode === ComparisonSurface.ReferenceFocus ? qsTr("Reference focus") : (currentMode === ComparisonSurface.AnalysisGrid ? qsTr("Analysis grid") : ""))
    // qmllint enable import unqualified unresolved-type

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

        ToolbarCombo {
            id: pairComboBox

            objectName: "pairCombo"
            visible: control.sourceCount === 3 && control.pairRelevant
            implicitWidth: 92
            implicitHeight: 30
            model: control.differenceEdges
            textRole: "label"
            currentIndex: control.currentEdgeIndex
            Accessible.name: qsTr("Comparison pair")
            hoverEnabled: true
            leftPadding: 10
            rightPadding: 26
            font.pixelSize: 12
            onActivated: index => {
                if (index >= 0 && index < control.differenceEdges.length)
                    control.edgeRequested(Number(control.differenceEdges[index].preferenceValue));
            }
        }

        VcsToolButton {
            id: moreCompareModesButton

            objectName: "moreCompareModesButton"
            text: control.advancedMode ? qsTr("… · %1").arg(control.advancedModeLabel) : "…"
            implicitWidth: control.advancedMode ? 128 : 36
            implicitHeight: 30
            onClicked: moreMenu.open()
            labelPixelSize: 12

            VcsMenu {
                id: moreMenu
                menuWidth: 220

                // qmllint disable import unqualified unresolved-type
                VcsRadioMenuItem {
                    id: threeUpMenuItem

                    objectName: "threeUpMenuItem"
                    text: qsTr("Three up")
                    enabled: control.sourceCount === 3
                    checked: control.currentMode === ComparisonSurface.ThreeUp
                    onTriggered: control.modeRequested(ComparisonSurface.ThreeUp)
                }
                VcsRadioMenuItem {
                    id: referenceFocusMenuItem

                    objectName: "referenceFocusMenuItem"
                    text: qsTr("Reference focus")
                    enabled: control.sourceCount === 3
                    checked: control.currentMode === ComparisonSurface.ReferenceFocus
                    onTriggered: control.modeRequested(ComparisonSurface.ReferenceFocus)
                }
                VcsRadioMenuItem {
                    id: analysisGridMenuItem

                    objectName: "analysisGridMenuItem"
                    text: qsTr("Analysis grid")
                    enabled: control.sourceCount === 3
                    checked: control.currentMode === ComparisonSurface.AnalysisGrid
                    onTriggered: control.modeRequested(ComparisonSurface.AnalysisGrid)
                }
                // qmllint enable import unqualified unresolved-type
            }
        }
    }

    component ModeButton: ReviewActionButton {
        id: modeButton

        required property int modeValue
        checkable: true
        checked: control.currentMode === modeValue
        implicitHeight: 30
        implicitWidth: 64
        enabled: !control.busy
        leftPadding: 10
        rightPadding: 10
        onClicked: control.modeRequested(modeValue)

        contentItem: Text {
            text: modeButton.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: !modeButton.enabled ? Theme.disabledText : (modeButton.checked || modeButton.hovered ? Theme.primaryText : Theme.mutedText)
            font.pixelSize: 12
            font.weight: modeButton.checked ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 5
            color: !modeButton.enabled ? Theme.disabledPanel : (modeButton.checked ? Theme.controlChecked : (modeButton.down ? Theme.controlPressed : (modeButton.hovered ? Theme.controlHover : Theme.control)))
            border.width: modeButton.activeFocus ? 2 : 1
            border.color: modeButton.activeFocus ? Theme.accent : (modeButton.checked ? Theme.accent : Theme.controlBorder)
        }
    }

    ReviewActionButton {
        id: inspectorToggleButton

        objectName: "inspectorToggleButton"
        text: control.inspectorOpen ? qsTr("Hide inspector") : qsTr("Inspector")
        implicitHeight: 30
        leftPadding: 12
        rightPadding: 12
        onClicked: control.inspectorRequested()
        anchors {
            right: parent.right
            rightMargin: 12
            verticalCenter: parent.verticalCenter
        }
    }
}
