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
    required property bool inspectorOpen
    required property bool graphicsReady
    required property int currentFrame
    required property bool alignmentAnalysisRunning
    required property bool chromeVisible
    required property bool fullScreen
    required property int shortcutPreset

    signal openVideosRequested
    signal addVideoRequested
    signal destructiveActionRequested(string kind)
    signal chromeToggleRequested
    signal fullScreenToggleRequested
    signal viewerFocusRequested

    VcsMenu {
        title: qsTr("&File")

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
        objectName: "compareMenu"
        title: qsTr("&Compare")
        enabled: control.sourceCount > 1 && !control.busy

        VcsMenuItem {
            text: qsTr("Side by side")
            checkable: true
            checked: Number(control.preferences.viewMode) === 0
            onTriggered: {
                control.preferences.viewMode = 0;
                control.viewerFocusRequested();
            }
        }
        VcsMenuItem {
            text: qsTr("Wipe")
            checkable: true
            checked: Number(control.preferences.viewMode) === 5
            onTriggered: {
                control.preferences.viewMode = 5;
                control.viewerFocusRequested();
            }
        }
        VcsMenuItem {
            text: qsTr("Difference")
            checkable: true
            checked: Number(control.preferences.viewMode) === 3
            onTriggered: {
                control.preferences.viewMode = 3;
                control.viewerFocusRequested();
            }
        }
        VcsMenuSeparator {}
        VcsMenu {
            title: qsTr("Layout")

            VcsMenuItem {
                text: qsTr("Three up")
                checkable: true
                checked: Number(control.preferences.viewMode) === 1
                onTriggered: {
                    control.preferences.viewMode = 1;
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                text: qsTr("Reference focus")
                checkable: true
                checked: Number(control.preferences.viewMode) === 2
                onTriggered: {
                    control.preferences.viewMode = 2;
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                objectName: "analysisGridMenuItem"
                text: qsTr("Analysis grid")
                enabled: control.sourceCount === 3
                checkable: true
                checked: Number(control.preferences.viewMode) === 4
                onTriggered: {
                    control.preferences.viewMode = 4;
                    control.viewerFocusRequested();
                }
            }
        }
        VcsMenu {
            title: qsTr("Pair")
            enabled: control.sourceCount === 3
            visible: control.sourceCount === 3

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
            title: qsTr("Reference")
            enabled: control.sourceCount > 1

            VcsMenuItem {
                text: qsTr("Video A")
                checkable: true
                checked: control.canonicalSourceIndex === 0
                onTriggered: {
                    control.session.changeReference(0);
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                text: qsTr("Video B")
                checkable: true
                checked: control.canonicalSourceIndex === 1
                onTriggered: {
                    control.session.changeReference(1);
                    control.viewerFocusRequested();
                }
            }
            VcsMenuItem {
                text: qsTr("Video C")
                visible: control.sourceCount > 2
                checkable: true
                checked: control.canonicalSourceIndex === 2
                onTriggered: {
                    control.session.changeReference(2);
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
        objectName: "analyzeMenu"
        title: qsTr("&Analyze")
        enabled: control.sourceCount > 1 && control.graphicsReady && control.currentFrame >= 0 && !control.busy

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
        title: qsTr("&View")

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
