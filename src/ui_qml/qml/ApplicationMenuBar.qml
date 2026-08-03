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
        onClosed: control.viewerFocusRequested()

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
                control.viewerFocusRequested();
            }
        }
        VcsMenuSeparator {}
        VcsMenuItem {
            text: qsTr("Exit")
            shortcutText: "Alt+F4"
            onTriggered: {
                control.destructiveActionRequested("exit");
                control.viewerFocusRequested();
            }
        }
    }

    VcsMenu {
        id: compareMenu
        objectName: "compareMenu"
        title: qsTr("&Compare")
        enabled: control.sourceCount > 1 && !control.busy
        onClosed: control.viewerFocusRequested()

        VcsMenuItem {
            text: qsTr("Side by side")
            checkable: true
            checked: control.currentViewMode === 0
            onTriggered: {
                control.preferences.viewMode = 0;
                control.viewerFocusRequested();
            }
        }
        VcsMenuItem {
            text: qsTr("Wipe")
            checkable: true
            checked: control.currentViewMode === 5
            onTriggered: {
                control.preferences.viewMode = 5;
                control.viewerFocusRequested();
            }
        }
        VcsMenuItem {
            text: qsTr("Difference")
            checkable: true
            checked: control.currentViewMode === 3
            onTriggered: {
                control.preferences.viewMode = 3;
                control.viewerFocusRequested();
            }
        }
        VcsMenuSeparator {
            visible: control.sourceCount === 3
        }
        VcsMenu {
            id: layoutMenu

            objectName: "layoutMenu"
            title: qsTr("Layout")
            menuItemVisible: control.sourceCount === 3
            menuItemEnabled: control.sourceCount === 3

            VcsMenuItem {
                text: qsTr("Three up")
                checkable: true
                checked: control.currentViewMode === 1
                onTriggered: {
                    control.preferences.viewMode = 1;
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                text: qsTr("Reference focus")
                checkable: true
                checked: control.currentViewMode === 2
                onTriggered: {
                    control.preferences.viewMode = 2;
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                objectName: "analysisGridMenuItem"
                text: qsTr("Analysis grid")
                checkable: true
                checked: control.currentViewMode === 4
                enabled: control.sourceCount === 3
                onTriggered: {
                    control.preferences.viewMode = 4;
                    control.viewerFocusRequested();
                }
            }
        }
        VcsMenuSeparator {
            visible: control.sourceCount === 3
        }
        VcsMenu {
            id: pairMenu

            objectName: "pairMenu"
            title: qsTr("Pair")
            menuItemVisible: control.sourceCount === 3
            menuItemEnabled: control.sourceCount === 3

            VcsMenuItem {
                text: qsTr("A / B")
                checkable: true
                checked: Number(control.preferences.differenceEdge) === 0
                onTriggered: {
                    control.preferences.differenceEdge = 0;
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                text: qsTr("A / C")
                checkable: true
                checked: Number(control.preferences.differenceEdge) === 1
                onTriggered: {
                    control.preferences.differenceEdge = 1;
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                text: qsTr("B / C")
                checkable: true
                checked: Number(control.preferences.differenceEdge) === 2
                onTriggered: {
                    control.preferences.differenceEdge = 2;
                    control.viewerFocusRequested();
                }
            }
        }
        VcsMenu {
            id: referenceMenu

            objectName: "referenceMenu"
            title: qsTr("Reference")
            enabled: control.sourceCount > 1

            VcsMenuItem {
                text: qsTr("Video A")
                checkable: true
                checked: control.canonicalSourceIndex === 0
                onTriggered: {
                    control.changeReferenceByIndex(0);
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                text: qsTr("Video B")
                checkable: true
                checked: control.canonicalSourceIndex === 1
                onTriggered: {
                    control.changeReferenceByIndex(1);
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                text: qsTr("Video C")
                visible: control.sourceCount > 2
                checkable: true
                checked: control.canonicalSourceIndex === 2
                onTriggered: {
                    control.changeReferenceByIndex(2);
                    control.viewerFocusRequested();
                }
            }
        }
        VcsMenuSeparator {}
        VcsMenuItem {
            text: control.inspectorOpen ? qsTr("Hide Inspector") : qsTr("Show Inspector")
            onTriggered: {
                control.session.inspectorVisible = !control.inspectorOpen;
                control.viewerFocusRequested();
            }
        }
    }

    VcsMenu {
        id: analyzeMenu
        objectName: "analyzeMenu"
        title: qsTr("&Analyze")
        enabled: control.sourceCount > 1 && control.graphicsReady && control.currentFrame >= 0 && !control.busy
        onClosed: control.viewerFocusRequested()

        VcsMenuItem {
            text: qsTr("Estimate global frame offset")
            enabled: control.graphicsReady && !control.busy && !control.alignmentAnalysisRunning && Boolean(control.controller && control.controller.canFirst)
            onTriggered: {
                control.controller.estimateAlignment();
                control.viewerFocusRequested();
            }
        }
        VcsMenuItem {
            text: control.alignmentAnalysisRunning ? qsTr("Cancel analysis") : qsTr("Analyze missing / duplicate frames")
            enabled: control.graphicsReady && !control.busy && Boolean(control.controller && (control.alignmentAnalysisRunning || control.controller.canFirst))
            onTriggered: {
                control.alignmentAnalysisRunning ? control.controller.cancelAlignmentAnalysis() : control.controller.analyzeSequenceAlignment();
                control.viewerFocusRequested();
            }
        }
    }

    VcsMenu {
        id: viewMenu

        objectName: "viewMenu"
        title: qsTr("&View")
        onClosed: control.viewerFocusRequested()

        VcsMenuItem {
            text: control.chromeVisible ? qsTr("Hide interface chrome · Tab") : qsTr("Show interface chrome · Tab")
            onTriggered: {
                control.chromeToggleRequested();
                control.viewerFocusRequested();
            }
        }
        VcsMenuItem {
            text: control.fullScreen ? qsTr("Exit full screen · F11") : qsTr("Enter full screen · F11")
            onTriggered: {
                control.fullScreenToggleRequested();
                control.viewerFocusRequested();
            }
        }
        VcsMenuSeparator {}
        VcsMenu {
            id: shortcutPresetMenu

            objectName: "shortcutPresetMenu"
            title: qsTr("Shortcut preset")

            VcsMenuItem {
                text: qsTr("Frame review")
                checkable: true
                checked: control.shortcutPreset === 0
                onTriggered: {
                    control.preferences.shortcutPreset = 0;
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                text: qsTr("Player")
                checkable: true
                checked: control.shortcutPreset === 1
                onTriggered: {
                    control.preferences.shortcutPreset = 1;
                    control.viewerFocusRequested();
                }
            }
        }
    }
}
