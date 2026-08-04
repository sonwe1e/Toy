pragma ComponentBehavior: Bound

import QtQuick

VcsMenuBar {
    id: control

    required property var controller
    required property var preferences
    required property var session
    required property int sourceCount
    required property bool busy
    required property int canonicalSourceIndex
    required property int currentViewMode
    required property bool inspectorOpen
    required property bool graphicsReady
    required property int currentFrame
    required property bool alignmentAnalysisRunning
    required property bool chromeVisible
    required property bool fullScreen
    required property int shortcutPreset
    required property var sourceIdentities

    signal openVideosRequested
    signal addVideoRequested
    signal destructiveActionRequested(string kind)
    signal chromeToggleRequested
    signal fullScreenToggleRequested
    signal viewerFocusRequested

    property bool returnViewerFocusAfterClose: false
    readonly property bool anyMenuOpen: fileMenu.opened || compareMenu.opened || analyzeMenu.opened || viewMenu.opened

    function changeReferenceByIndex(sourceIndex) {
        if (!control.session || sourceIndex < 0 || sourceIndex >= control.sourceIdentities.length)
            return false;
        return control.session.changeReferenceByIdentity(String(control.sourceIdentities[sourceIndex]));
    }

    VcsMenu {
        id: fileMenu

        objectName: "fileMenu"
        title: qsTr("&File")
        onClosed: {
            if (control.returnViewerFocusAfterClose) {
                control.returnViewerFocusAfterClose = false;
                control.viewerFocusRequested();
            }
        }

        VcsMenuItem {
            text: qsTr("Open videos…")
            shortcutText: "Ctrl+O"
            onTriggered: control.openVideosRequested()
        }
        VcsMenuItem {
            text: qsTr("Add video…")
            shortcutText: "Ctrl+Shift+O"
            enabled: control.sourceCount > 0 && control.sourceCount < 3
            onTriggered: control.addVideoRequested()
        }
        VcsMenuItem {
            text: qsTr("Close videos")
            shortcutText: "Ctrl+W"
            enabled: control.sourceCount > 0
            onTriggered: {
                control.destructiveActionRequested("closeReview");
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsMenuSeparator {}
        VcsMenuItem {
            text: qsTr("Exit")
            shortcutText: "Alt+F4"
            onTriggered: {
                control.destructiveActionRequested("exit");
                control.returnViewerFocusAfterClose = true;
            }
        }
    }

    VcsMenu {
        id: compareMenu
        objectName: "compareMenu"
        title: qsTr("&Compare")
        enabled: control.sourceCount > 1 && !control.busy
        onClosed: {
            if (control.returnViewerFocusAfterClose) {
                control.returnViewerFocusAfterClose = false;
                control.viewerFocusRequested();
            }
        }

        VcsRadioMenuItem {
            objectName: "sideBySideMenuItem"
            text: qsTr("Side by side")
            checked: control.currentViewMode === 0
            onTriggered: {
                control.preferences.viewMode = 0;
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsRadioMenuItem {
            objectName: "wipeMenuItem"
            text: qsTr("Wipe")
            checked: control.currentViewMode === 5
            onTriggered: {
                control.preferences.viewMode = 5;
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsRadioMenuItem {
            objectName: "differenceMenuItem"
            text: qsTr("Difference")
            checked: control.currentViewMode === 3
            onTriggered: {
                control.preferences.viewMode = 3;
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsMenuSeparator {
            objectName: "compareModeSeparator"
        }
        VcsMenu {
            id: layoutMenu

            objectName: "layoutMenu"
            title: qsTr("Layout")
            menuItemVisible: control.sourceCount === 3
            menuItemEnabled: control.sourceCount === 3

            VcsRadioMenuItem {
                objectName: "threeUpMenuItem"
                text: qsTr("Three up")
                checked: control.currentViewMode === 1
                onTriggered: {
                    control.preferences.viewMode = 1;
                    control.returnViewerFocusAfterClose = true;
                }
            }
            VcsRadioMenuItem {
                text: qsTr("Reference focus")
                checked: control.currentViewMode === 2
                onTriggered: {
                    control.preferences.viewMode = 2;
                    control.returnViewerFocusAfterClose = true;
                }
            }
            VcsRadioMenuItem {
                objectName: "analysisGridMenuItem"
                text: qsTr("Analysis grid")
                checked: control.currentViewMode === 4
                enabled: control.sourceCount === 3
                onTriggered: {
                    control.preferences.viewMode = 4;
                    control.returnViewerFocusAfterClose = true;
                }
            }
        }
        VcsMenu {
            id: pairMenu

            objectName: "pairMenu"
            title: qsTr("Pair")
            menuItemVisible: control.sourceCount === 3
            menuItemEnabled: control.sourceCount === 3

            VcsRadioMenuItem {
                text: qsTr("A / B")
                checked: Number(control.preferences.differenceEdge) === 0
                onTriggered: {
                    control.preferences.differenceEdge = 0;
                    control.returnViewerFocusAfterClose = true;
                }
            }
            VcsRadioMenuItem {
                text: qsTr("A / C")
                checked: Number(control.preferences.differenceEdge) === 1
                onTriggered: {
                    control.preferences.differenceEdge = 1;
                    control.returnViewerFocusAfterClose = true;
                }
            }
            VcsRadioMenuItem {
                text: qsTr("B / C")
                checked: Number(control.preferences.differenceEdge) === 2
                onTriggered: {
                    control.preferences.differenceEdge = 2;
                    control.returnViewerFocusAfterClose = true;
                }
            }
        }
        VcsMenu {
            id: referenceMenu

            objectName: "referenceMenu"
            title: qsTr("Reference")
            enabled: control.sourceCount > 1

            VcsRadioMenuItem {
                text: qsTr("Video A")
                checked: control.canonicalSourceIndex === 0
                onTriggered: {
                    control.changeReferenceByIndex(0);
                    control.returnViewerFocusAfterClose = true;
                }
            }
            VcsRadioMenuItem {
                text: qsTr("Video B")
                checked: control.canonicalSourceIndex === 1
                onTriggered: {
                    control.changeReferenceByIndex(1);
                    control.returnViewerFocusAfterClose = true;
                }
            }
            VcsRadioMenuItem {
                text: qsTr("Video C")
                visible: control.sourceCount > 2
                checked: control.canonicalSourceIndex === 2
                onTriggered: {
                    control.changeReferenceByIndex(2);
                    control.returnViewerFocusAfterClose = true;
                }
            }
        }
        VcsMenuSeparator {
            objectName: "compareInspectorSeparator"
        }
        VcsMenuItem {
            objectName: "compareInspectorMenuItem"
            text: control.inspectorOpen ? qsTr("Hide Inspector") : qsTr("Show Inspector")
            onTriggered: {
                control.session.inspectorVisible = !control.inspectorOpen;
                control.returnViewerFocusAfterClose = true;
            }
        }
    }

    VcsMenu {
        id: analyzeMenu
        objectName: "analyzeMenu"
        title: qsTr("&Analyze")
        enabled: control.sourceCount > 1 && control.graphicsReady && control.currentFrame >= 0 && !control.busy
        onClosed: {
            if (control.returnViewerFocusAfterClose) {
                control.returnViewerFocusAfterClose = false;
                control.viewerFocusRequested();
            }
        }

        VcsMenuItem {
            text: qsTr("Estimate global frame offset")
            enabled: control.graphicsReady && !control.busy && !control.alignmentAnalysisRunning && Boolean(control.controller && control.controller.canFirst)
            onTriggered: {
                control.controller.estimateAlignment();
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsMenuItem {
            text: control.alignmentAnalysisRunning ? qsTr("Cancel analysis") : qsTr("Analyze missing / duplicate frames")
            enabled: control.graphicsReady && !control.busy && Boolean(control.controller && (control.alignmentAnalysisRunning || control.controller.canFirst))
            onTriggered: {
                control.alignmentAnalysisRunning ? control.controller.cancelAlignmentAnalysis() : control.controller.analyzeSequenceAlignment();
                control.returnViewerFocusAfterClose = true;
            }
        }
    }

    VcsMenu {
        id: viewMenu

        objectName: "viewMenu"
        title: qsTr("&View")
        onClosed: {
            if (control.returnViewerFocusAfterClose) {
                control.returnViewerFocusAfterClose = false;
                control.viewerFocusRequested();
            }
        }

        VcsMenuItem {
            text: control.chromeVisible ? qsTr("Hide interface chrome · Tab") : qsTr("Show interface chrome · Tab")
            onTriggered: {
                control.chromeToggleRequested();
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsMenuItem {
            text: control.fullScreen ? qsTr("Exit full screen · F11") : qsTr("Enter full screen · F11")
            onTriggered: {
                control.fullScreenToggleRequested();
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsMenuSeparator {}
        VcsMenu {
            id: shortcutPresetMenu

            objectName: "shortcutPresetMenu"
            title: qsTr("Shortcut preset")

            VcsRadioMenuItem {
                text: qsTr("Frame review")
                checked: control.shortcutPreset === 0
                onTriggered: {
                    control.preferences.shortcutPreset = 0;
                    control.returnViewerFocusAfterClose = true;
                }
            }
            VcsRadioMenuItem {
                text: qsTr("Player")
                checked: control.shortcutPreset === 1
                onTriggered: {
                    control.preferences.shortcutPreset = 1;
                    control.returnViewerFocusAfterClose = true;
                }
            }
        }
    }
}
