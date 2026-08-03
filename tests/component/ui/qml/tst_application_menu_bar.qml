pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    width: 960
    height: 640

    QtObject {
        id: controller

        property bool canFirst: true
        function estimateAlignment() {
        }
        function analyzeSequenceAlignment() {
        }
        function cancelAlignmentAnalysis() {
        }
    }

    QtObject {
        id: preferences

        property int viewMode: 0
        property int differenceEdge: 0
        property int shortcutPreset: 0
    }

    QtObject {
        id: session

        property string lastReferenceIdentity: ""
        property bool inspectorVisible: false
        function changeReferenceByIdentity(sourceIdentity) {
            lastReferenceIdentity = sourceIdentity;
            return true;
        }
    }

    Dvs.ApplicationMenuBar {
        id: menuBar

        controller: controller
        preferences: preferences
        session: session
        sourceCount: 2
        busy: false
        canonicalSourceIndex: 0
        currentViewMode: 0
        inspectorOpen: false
        graphicsReady: true
        currentFrame: 0
        alignmentAnalysisRunning: false
        chromeVisible: true
        fullScreen: false
        shortcutPreset: 0
        sourceIdentities: ["source-a", "source-b", "source-c"]
    }

    TestCase {
        name: "ApplicationMenuBar"
        when: windowShown

        function cleanup() {
            findChild(menuBar, "compareMenu").close();
            findChild(menuBar, "viewMenu").close();
            menuBar.sourceCount = 2;
            wait(0);
        }

        function test_hides_three_source_layout_for_a_pair() {
            const layout = findChild(menuBar, "layoutMenu");
            const pair = findChild(menuBar, "pairMenu");
            verify(layout !== null);
            verify(pair !== null);
            compare(layout.menuItemVisible, false);
            compare(pair.menuItemVisible, false);

            menuBar.sourceCount = 3;
            tryCompare(layout, "menuItemVisible", true);
            tryCompare(pair, "menuItemVisible", true);
        }

        function test_open_state_and_reference_use_source_identity() {
            const view = findChild(menuBar, "viewMenu");
            verify(view !== null);
            view.popup(root, 24, 24);
            tryCompare(view, "opened", true);
            compare(menuBar.anyMenuOpen, true);
            compare(view.popupType, Popup.Window);
            view.close();
            tryCompare(menuBar, "anyMenuOpen", false);

            verify(menuBar.changeReferenceByIndex(1));
            compare(session.lastReferenceIdentity, "source-b");
        }

        function test_all_compare_and_view_submenus_are_opaque_window_popups() {
            menuBar.sourceCount = 3;
            const menuNames = ["compareMenu", "layoutMenu", "pairMenu", "referenceMenu", "viewMenu", "shortcutPresetMenu"];
            for (const menuName of menuNames) {
                const menu = findChild(menuBar, menuName);
                verify(menu !== null, menuName);
                compare(menu.popupType, Popup.Window, menuName);
                compare(menu.background.color.a, 1.0, menuName);
                menu.popup(root, 24, 24);
                tryCompare(menu, "opened", true);
                verify(menu.x >= 0, menuName);
                verify(menu.y >= 0, menuName);
                menu.close();
                tryCompare(menu, "opened", false);
            }
        }
    }
}
