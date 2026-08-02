pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs as NativeDialogs

Item {
    id: control

    required property var stagedVideos
    required property var fileNameFunction
    required property int initialReferenceIndex
    readonly property bool comparisonVisible: comparisonDialog.visible

    signal openVideosAccepted(var urls)
    signal addVideoAccepted(url url)
    signal moveRequested(int fromIndex, int toIndex)
    signal comparisonAccepted(int referenceIndex)
    signal comparisonRejected

    function openVideos() {
        videoFilesDialog.open();
    }

    function openAddVideo() {
        addVideoDialog.open();
    }

    function openComparison() {
        comparisonDialog.open();
    }

    NativeDialogs.FileDialog {
        id: videoFilesDialog

        objectName: "videoFilesDialog"
        title: qsTr("Open one to three videos")
        fileMode: NativeDialogs.FileDialog.OpenFiles
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: control.openVideosAccepted(selectedFiles)
    }

    NativeDialogs.FileDialog {
        id: addVideoDialog

        objectName: "addSourceFileDialog"
        title: qsTr("Add video")
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: control.addVideoAccepted(selectedFile)
    }

    DropConfirmationDialog {
        id: comparisonDialog

        pendingVideos: control.stagedVideos
        fileNameFunction: control.fileNameFunction
        initialReferenceIndex: control.initialReferenceIndex
        onMoveRequested: (fromIndex, toIndex) => control.moveRequested(fromIndex, toIndex)
        onAccepted: control.comparisonAccepted(referenceIndex)
        onRejected: control.comparisonRejected()
    }
}
