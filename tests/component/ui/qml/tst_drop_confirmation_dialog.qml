import QtQuick
import QtQuick.Controls
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    width: 720
    height: 520

    Rectangle {
        anchors.fill: parent
        color: "#ffff00ff"
    }

    Dvs.DropConfirmationDialog {
        id: dialog

        pendingVideos: ["file:///C:/video%20one.mp4", "file:///C:/视频%20二.mp4"]
        fileNameFunction: function (url) {
            return decodeURIComponent(url.toString()).split("/").pop();
        }
    }

    SignalSpy {
        id: acceptedSpy

        target: dialog
        signalName: "accepted"
    }

    SignalSpy {
        id: rejectedSpy

        target: dialog
        signalName: "rejected"
    }

    TestCase {
        name: "DropConfirmationDialog"
        when: windowShown

        function test_opaque_complete_popup() {
            const windowContent = root.Window.window.contentItem;
            const beforeOpen = grabImage(windowContent);
            const originalVideos = dialog.pendingVideos.slice();

            dialog.open();
            tryCompare(dialog, "visible", true);
            wait(50);

            const background = findChild(dialog, "dropDialogBackground");
            const header = findChild(dialog, "dropDialogHeader");
            const footer = findChild(dialog, "dropDialogFooter");
            const reference = findChild(dialog, "dropReferenceCombo");
            verify(background !== null);
            verify(header !== null);
            verify(footer !== null);
            verify(reference !== null);
            compare(background.color.a, 1.0);
            compare(header.color.a, 1.0);
            compare(footer.background.color.a, 1.0);
            verify(dialog.modal);
            verify(dialog.dim);
            compare(dialog.popupType, Popup.Item);
            verify(dialog.width <= root.width - 48);
            verify(dialog.width > 400);
            verify(footer.standardButton(DialogButtonBox.Ok).visible);
            verify(footer.standardButton(DialogButtonBox.Cancel).visible);
            verify(footer.height * Screen.devicePixelRatio >= 40);

            const opened = grabImage(windowContent);
            const scaleX = opened.width / windowContent.width;
            const scaleY = opened.height / windowContent.height;
            const headerPoint = header.mapToItem(windowContent, header.width - 24, 24);
            const headerX = Math.round(headerPoint.x * scaleX);
            const headerY = Math.round(headerPoint.y * scaleY);
            compare(opened.alpha(headerX, headerY), 255);
            fuzzyCompare(opened.red(headerX, headerY), 17, 3);
            fuzzyCompare(opened.green(headerX, headerY), 24, 3);
            fuzzyCompare(opened.blue(headerX, headerY), 35, 3);

            const outsideX = Math.round(12 * scaleX);
            const outsideY = Math.round(12 * scaleY);
            verify(opened.red(outsideX, outsideY) < beforeOpen.red(outsideX, outsideY));
            verify(opened.blue(outsideX, outsideY) < beforeOpen.blue(outsideX, outsideY));

            mouseClick(footer.standardButton(DialogButtonBox.Ok));
            tryCompare(dialog, "visible", false);
            compare(acceptedSpy.count, 1);
            compare(dialog.pendingVideos, originalVideos);

            dialog.open();
            tryCompare(dialog, "visible", true);
            mouseClick(footer.standardButton(DialogButtonBox.Cancel));
            tryCompare(dialog, "visible", false);
            compare(rejectedSpy.count, 1);
            compare(dialog.pendingVideos, originalVideos);
        }
    }
}
