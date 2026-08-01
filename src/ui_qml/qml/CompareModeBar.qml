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

        ComboBox {
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

            delegate: ItemDelegate {
                id: pairComboDelegate

                required property int index

                width: ListView.view ? ListView.view.width : pairComboBox.width
                implicitHeight: 30
                highlighted: pairComboBox.highlightedIndex === index
                hoverEnabled: pairComboBox.hoverEnabled

                contentItem: Text {
                    text: pairComboBox.textAt(pairComboDelegate.index)
                    color: pairComboDelegate.highlighted || pairComboDelegate.hovered ? "#f3f6fb" : "#93a2ba"
                    font.pixelSize: 12
                    font.weight: pairComboBox.currentIndex === pairComboDelegate.index ? Font.DemiBold : Font.Normal
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: 3
                    color: pairComboDelegate.highlighted || pairComboDelegate.hovered ? "#285da9" : "#1d2635"
                }
            }

            indicator: Text {
                x: pairComboBox.width - width - 6
                y: (pairComboBox.height - height) / 2
                text: "▾"
                color: pairComboBox.enabled ? "#f3f6fb" : "#637086"
                font.pixelSize: 12
            }

            contentItem: Text {
                text: pairComboBox.displayText
                color: pairComboBox.enabled ? "#f3f6fb" : "#637086"
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            background: Rectangle {
                radius: 5
                color: pairComboBox.enabled ? "#1d2635" : "#202938"
                border.width: pairComboBox.activeFocus ? 2 : 1
                border.color: pairComboBox.activeFocus ? "#4b8df8" : "#303d51"
            }

            popup: Popup {
                y: pairComboBox.height + 2
                width: pairComboBox.width
                implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                height: Math.min(implicitHeight, 220)
                topMargin: 6
                bottomMargin: 6
                padding: 4
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: pairComboBox.delegateModel
                    currentIndex: pairComboBox.highlightedIndex
                    highlightMoveDuration: 0
                    boundsBehavior: Flickable.StopAtBounds
                }

                background: Rectangle {
                    radius: 5
                    color: "#1d2635"
                    border.width: 1
                    border.color: "#303d51"
                }
            }
        }

        ToolButton {
            id: moreCompareModesButton

            objectName: "moreCompareModesButton"
            text: control.advancedMode ? qsTr("… · %1").arg(control.advancedModeLabel) : "…"
            implicitWidth: control.advancedMode ? 128 : 36
            implicitHeight: 30
            padding: 0
            onClicked: moreMenu.open()

            contentItem: Text {
                text: moreCompareModesButton.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: moreCompareModesButton.enabled ? "#f3f6fb" : "#637086"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            background: Rectangle {
                radius: 5
                color: !moreCompareModesButton.enabled ? "#202938" : (moreCompareModesButton.down ? "#285da9" : (moreCompareModesButton.hovered ? "#2d69bf" : "#253247"))
                border.width: moreCompareModesButton.activeFocus ? 2 : 1
                border.color: moreCompareModesButton.activeFocus ? "#4b8df8" : "#3b4d67"
            }

            Menu {
                id: moreMenu

                topMargin: 6
                bottomMargin: 6
                padding: 4

                background: Rectangle {
                    radius: 5
                    color: "#1d2635"
                    border.color: "#303d51"
                    border.width: 1
                }

                // qmllint disable import unqualified unresolved-type
                MenuItem {
                    id: threeUpMenuItem

                    objectName: "threeUpMenuItem"
                    text: qsTr("Three up")
                    enabled: control.sourceCount === 3
                    checkable: true
                    checked: control.currentMode === ComparisonSurface.ThreeUp
                    onTriggered: control.modeRequested(ComparisonSurface.ThreeUp)
                    leftPadding: 30
                    rightPadding: 14
                    topPadding: 6
                    bottomPadding: 6

                    contentItem: Text {
                        text: threeUpMenuItem.text
                        color: threeUpMenuItem.enabled ? "#f3f6fb" : "#637086"
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    indicator: Rectangle {
                        x: 8
                        y: threeUpMenuItem.height / 2 - height / 2
                        implicitWidth: 14
                        implicitHeight: 14
                        radius: 3
                        color: threeUpMenuItem.checked ? "#4b8df8" : "#202938"
                        border.color: threeUpMenuItem.checked ? "#4b8df8" : "#40516a"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: threeUpMenuItem.checked ? "✓" : ""
                            color: "#ffffff"
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }

                    background: Rectangle {
                        radius: 3
                        color: threeUpMenuItem.hovered || threeUpMenuItem.highlighted ? "#285da9" : "transparent"
                    }
                }
                MenuItem {
                    id: referenceFocusMenuItem

                    objectName: "referenceFocusMenuItem"
                    text: qsTr("Reference focus")
                    enabled: control.sourceCount === 3
                    checkable: true
                    checked: control.currentMode === ComparisonSurface.ReferenceFocus
                    onTriggered: control.modeRequested(ComparisonSurface.ReferenceFocus)
                    leftPadding: 30
                    rightPadding: 14
                    topPadding: 6
                    bottomPadding: 6

                    contentItem: Text {
                        text: referenceFocusMenuItem.text
                        color: referenceFocusMenuItem.enabled ? "#f3f6fb" : "#637086"
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    indicator: Rectangle {
                        x: 8
                        y: referenceFocusMenuItem.height / 2 - height / 2
                        implicitWidth: 14
                        implicitHeight: 14
                        radius: 3
                        color: referenceFocusMenuItem.checked ? "#4b8df8" : "#202938"
                        border.color: referenceFocusMenuItem.checked ? "#4b8df8" : "#40516a"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: referenceFocusMenuItem.checked ? "✓" : ""
                            color: "#ffffff"
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }

                    background: Rectangle {
                        radius: 3
                        color: referenceFocusMenuItem.hovered || referenceFocusMenuItem.highlighted ? "#285da9" : "transparent"
                    }
                }
                MenuItem {
                    id: analysisGridMenuItem

                    objectName: "analysisGridMenuItem"
                    text: qsTr("Analysis grid")
                    enabled: control.sourceCount === 3
                    checkable: true
                    checked: control.currentMode === ComparisonSurface.AnalysisGrid
                    onTriggered: control.modeRequested(ComparisonSurface.AnalysisGrid)
                    leftPadding: 30
                    rightPadding: 14
                    topPadding: 6
                    bottomPadding: 6

                    contentItem: Text {
                        text: analysisGridMenuItem.text
                        color: analysisGridMenuItem.enabled ? "#f3f6fb" : "#637086"
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    indicator: Rectangle {
                        x: 8
                        y: analysisGridMenuItem.height / 2 - height / 2
                        implicitWidth: 14
                        implicitHeight: 14
                        radius: 3
                        color: analysisGridMenuItem.checked ? "#4b8df8" : "#202938"
                        border.color: analysisGridMenuItem.checked ? "#4b8df8" : "#40516a"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: analysisGridMenuItem.checked ? "✓" : ""
                            color: "#ffffff"
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }

                    background: Rectangle {
                        radius: 3
                        color: analysisGridMenuItem.hovered || analysisGridMenuItem.highlighted ? "#285da9" : "transparent"
                    }
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
        leftPadding: 10
        rightPadding: 10
        onClicked: control.modeRequested(modeValue)

        contentItem: Text {
            text: modeButton.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: !modeButton.enabled ? "#637086" : (modeButton.checked || modeButton.hovered ? "#f3f6fb" : "#93a2ba")
            font.pixelSize: 12
            font.weight: modeButton.checked ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 5
            color: !modeButton.enabled ? "#202938" : (modeButton.checked ? "#243f68" : (modeButton.down ? "#285da9" : (modeButton.hovered ? "#2d69bf" : "#253247")))
            border.width: modeButton.activeFocus ? 2 : 1
            border.color: modeButton.activeFocus ? "#4b8df8" : (modeButton.checked ? "#4b8df8" : "#3b4d67")
        }
    }

    Button {
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

        contentItem: Text {
            text: inspectorToggleButton.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: inspectorToggleButton.enabled ? "#f3f6fb" : "#637086"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 5
            color: !inspectorToggleButton.enabled ? "#202938" : (inspectorToggleButton.down ? "#285da9" : (inspectorToggleButton.hovered ? "#2d69bf" : "#253247"))
            border.width: inspectorToggleButton.activeFocus ? 2 : 1
            border.color: inspectorToggleButton.activeFocus ? "#4b8df8" : "#3b4d67"
        }
    }
}
