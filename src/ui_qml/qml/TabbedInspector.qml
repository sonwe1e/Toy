pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: control

    required property var host

    objectName: "tabbedInspector"
    width: 360
    color: "#111823"
    border.color: control.host.borderColor

    TabBar {
        id: tabs

        width: parent.width
        TabButton {
            text: qsTr("Compare")
        }
        TabButton {
            text: qsTr("Alignment")
        }
        TabButton {
            text: qsTr("Review")
        }
        TabButton {
            text: qsTr("Info")
        }
    }

    StackLayout {
        currentIndex: tabs.currentIndex
        anchors {
            top: tabs.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        Flickable {
            clip: true
            contentHeight: compareColumn.implicitHeight + 28

            Column {
                id: compareColumn

                width: parent.width - 28
                spacing: 10
                x: 14
                y: 14

                Label {
                    text: qsTr("Comparison")
                    color: control.host.primaryTextColor
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Label {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Use the Viewer context bar for Side, Wipe, Diff and Pair. Advanced difference filtering and thresholds remain available directly on the Viewer.")
                    color: control.host.mutedTextColor
                }
                Button {
                    text: qsTr("Reset zoom and pan")
                    onClicked: control.host.resetViewport()
                }
            }
        }

        AlignmentInspector {
            host: control.host
        }

        Flickable {
            clip: true
            contentHeight: reviewColumn.implicitHeight + 28

            Column {
                id: reviewColumn

                width: parent.width - 28
                spacing: 10
                x: 14
                y: 14

                Label {
                    text: qsTr("Review")
                    color: control.host.primaryTextColor
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("In: %1").arg(control.host.inFrame >= 0 ? control.host.inFrame + 1 : "—")
                    color: control.host.mutedTextColor
                }
                Label {
                    text: qsTr("Out: %1").arg(control.host.outFrame >= 0 ? control.host.outFrame + 1 : "—")
                    color: control.host.mutedTextColor
                }
                Button {
                    text: qsTr("Export current Bad Case…")
                    enabled: control.host.currentFrame >= 0
                    onClicked: control.host.openBadCaseExport()
                }
            }
        }

        Flickable {
            clip: true
            contentHeight: infoColumn.implicitHeight + 28

            Column {
                id: infoColumn

                width: parent.width - 28
                spacing: 8
                x: 14
                y: 14

                Label {
                    text: qsTr("Media and renderer")
                    color: control.host.primaryTextColor
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: control.host.controller ? control.host.controller.sources : null
                    Label {
                        required property int sourceId
                        required property string filename
                        width: infoColumn.width
                        text: qsTr("Source %1 · %2").arg(String.fromCharCode(65 + sourceId)).arg(filename)
                        color: control.host.mutedTextColor
                        elide: Text.ElideMiddle
                    }
                }
                Label {
                    text: control.host.graphicsReady ? qsTr("D3D11 renderer ready") : qsTr("Graphics unavailable")
                    color: control.host.graphicsReady ? "#8ce2c2" : "#efbf83"
                }
            }
        }
    }
}
