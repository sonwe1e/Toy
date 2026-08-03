pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    width: 720
    height: 160

    ListModel {
        id: sources

        ListElement {
            sourceId: 0
            sourceIdentity: "source-a"
            filename: "reference.mp4"
        }
        ListElement {
            sourceId: 1
            sourceIdentity: "source-b"
            filename: "prediction.mp4"
        }
    }

    Dvs.ActiveSourceStrip {
        id: strip

        width: parent.width
        sourcesModel: sources
        sourceCount: 2
        singleMode: false
        canonicalSourceIndex: 0
        canonicalSourceIdentity: "source-a"
        pendingSourceIdentities: []
        sourceIdentities: ["source-a", "source-b"]
    }

    SignalSpy {
        id: referenceSpy

        target: strip
        signalName: "referenceRequested"
    }

    SignalSpy {
        id: removalSpy

        target: strip
        signalName: "removeRequested"
    }

    TestCase {
        name: "ActiveSourceStrip"
        when: windowShown

        function cleanup() {
            const firstMenu = findChild(strip, "sourceOverflowButton-0").sourceMenuControl;
            const secondMenu = findChild(strip, "sourceOverflowButton-1").sourceMenuControl;
            firstMenu.close();
            secondMenu.close();
            referenceSpy.clear();
            removalSpy.clear();
            strip.pendingSourceIdentities = [];
            wait(0);
        }

        function test_reference_is_a_badge_and_all_sources_have_action_menus() {
            const badge = findChild(strip, "sourceReferenceBadge-0");
            const firstOverflow = findChild(strip, "sourceOverflowButton-0");
            const secondOverflow = findChild(strip, "sourceOverflowButton-1");
            verify(badge !== null);
            verify(firstOverflow !== null);
            verify(secondOverflow !== null);
            verify(badge.visible);

            mouseClick(secondOverflow, secondOverflow.width / 2, secondOverflow.height / 2);
            const menu = secondOverflow.sourceMenuControl;
            tryCompare(menu, "opened", true);
            compare(strip.anyMenuOpen, true);
            compare(menu.popupType, Popup.Window);
            verify(secondOverflow.makeReferenceAction.visible);
            menu.close();
            tryCompare(strip, "anyMenuOpen", false);
        }

        function test_actions_emit_identity_after_the_menu_closes() {
            const secondOverflow = findChild(strip, "sourceOverflowButton-1");
            const menu = secondOverflow.sourceMenuControl;
            const makeReference = secondOverflow.makeReferenceAction;
            menu.popup(root, 300, 24);
            tryCompare(menu, "opened", true);
            makeReference.triggered();
            tryCompare(referenceSpy, "count", 1);
            compare(referenceSpy.signalArguments[0][0], "source-b");
            tryCompare(menu, "opened", false);

            const firstOverflow = findChild(strip, "sourceOverflowButton-0");
            const firstMenu = firstOverflow.sourceMenuControl;
            const remove = firstOverflow.removeSourceAction;
            firstMenu.popup(root, 300, 24);
            tryCompare(firstMenu, "opened", true);
            remove.triggered();
            tryCompare(removalSpy, "count", 1);
            compare(removalSpy.signalArguments[0][0], "source-a");
        }

        function test_pending_identity_disables_only_the_affected_source() {
            const firstOverflow = findChild(strip, "sourceOverflowButton-0");
            const secondOverflow = findChild(strip, "sourceOverflowButton-1");
            strip.pendingSourceIdentities = ["source-b"];
            tryCompare(firstOverflow, "enabled", true);
            tryCompare(secondOverflow, "enabled", false);
        }
    }
}
