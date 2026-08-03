pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: control

    required property var host

    height: control.host.chromeVisible && !control.host.singleMode ? 48 : 0
    visible: control.host.chromeVisible && !control.host.singleMode
    color: control.host.panelColor
    border.color: control.host.borderColor
    Flickable {
        id: comparisonScroller

        objectName: "comparisonScroller"
        clip: true
        contentWidth: comparisonControls.implicitWidth
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        anchors {
            fill: parent
            leftMargin: 14
            rightMargin: 14
        }

        Row {
            id: comparisonControls

            spacing: 8
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: qsTr("Reference")
                color: control.host.mutedTextColor
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            ToolbarCombo {
                id: referenceSourceCombo

                objectName: "referenceSourceCombo"
                implicitWidth: 112
                model: control.host.sourceCount >= 3 ? [qsTr("Source A"), qsTr("Source B"), qsTr("Source C")] : [qsTr("Source A"), qsTr("Source B")]
                currentIndex: Math.max(0, control.host.canonicalSourceIndex)
                Accessible.name: qsTr("Canonical reference source")
                onActivated: index => {
                    if (!control.host.changeReferenceAtIndex(index))
                        currentIndex = Math.max(0, control.host.canonicalSourceIndex);
                }
            }

            Text {
                text: qsTr("View")
                color: control.host.mutedTextColor
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            ToolbarCombo {
                id: viewModeCombo

                objectName: "viewModeCombo"
                enabled: control.host.sourceCount > 1
                model: control.host.availableViewModes
                textRole: "label"
                valueRole: "value"
                currentIndex: {
                    for (let index = 0; index < control.host.availableViewModes.length; ++index) {
                        if (Number(control.host.availableViewModes[index].value) === control.host.effectiveViewMode)
                            return index;
                    }
                    return 0;
                }
                Accessible.name: qsTr("Comparison view")
                onActivated: {
                    if (control.host.preferences)
                        control.host.preferences.viewMode = Number(currentValue);
                }
            }

            Text {
                visible: control.host.differenceMode
                text: qsTr("Metric")
                color: control.host.mutedTextColor
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            ToolbarCombo {
                id: differenceMetricCombo

                objectName: "differenceMetricCombo"
                visible: control.host.differenceMode
                model: [qsTr("RGB absolute"), qsTr("Luma"), qsTr("Chroma"), qsTr("Heatmap"), qsTr("Exact planes")]
                currentIndex: control.host.preferences ? Number(control.host.preferences.differenceMetric) : 0
                Accessible.name: qsTr("Difference metric")
                onActivated: index => {
                    if (control.host.preferences)
                        control.host.preferences.differenceMetric = index;
                }
            }

            ToolbarCombo {
                id: differenceGainCombo

                objectName: "differenceGainCombo"
                visible: control.host.differenceMode
                implicitWidth: 76
                model: ["1x", "2x", "4x", "8x", "16x"]
                currentIndex: control.host.preferences ? Number(control.host.preferences.differenceGain) : 0
                Accessible.name: qsTr("Difference gain")
                onActivated: index => {
                    if (control.host.preferences)
                        control.host.preferences.differenceGain = index;
                }
            }

            ToolbarCombo {
                id: differenceEdgeCombo

                objectName: "differenceEdgeCombo"
                visible: control.host.differenceMode || control.host.wipeMode
                implicitWidth: 94
                model: control.host.differenceEdges
                textRole: "label"
                currentIndex: control.host.differenceEdgeIndex(control.host.differenceEdge)
                Accessible.name: qsTr("Difference source pair")
                onActivated: index => {
                    if (control.host.preferences && index >= 0 && index < control.host.differenceEdges.length)
                        control.host.preferences.differenceEdge = Number(control.host.differenceEdges[index].preferenceValue);
                }
            }

            ToolbarCombo {
                id: differenceFilterCombo

                objectName: "differenceFilterCombo"
                visible: control.host.differenceMode
                implicitWidth: 104
                model: [qsTr("Nearest"), qsTr("Bilinear"), qsTr("Bicubic")]
                currentIndex: control.host.preferences ? Number(control.host.preferences.differenceFilter) : 1
                Accessible.name: qsTr("Spatial resampling filter")
                onActivated: index => {
                    if (control.host.preferences)
                        control.host.preferences.differenceFilter = index;
                }
            }

            Text {
                visible: control.host.differenceMode
                text: qsTr("Size mismatch is resampled; result is not pixel-exact.")
                color: control.host.mutedTextColor
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
