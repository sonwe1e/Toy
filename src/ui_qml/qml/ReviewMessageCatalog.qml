import QtQml

QtObject {
    function droppedUrlError(errorKey, detail) {
        switch (errorKey) {
        case "drop-empty":
            return qsTr("No files were dropped.");
        case "drop-too-many":
            return qsTr("Drop at most three video files.");
        case "drop-invalid-local":
            return qsTr("Only local files can be dropped.");
        case "drop-missing":
            return qsTr("Dropped file is missing: %1").arg(detail);
        case "drop-duplicate":
            return qsTr("The same file was dropped more than once: %1").arg(detail);
        default:
            return qsTr("The dropped videos could not be opened.");
        }
    }

    function intentKindText(kind, sourceCount) {
        switch (Number(kind)) {
        case 0:
            return qsTr("Open %1 video(s)").arg(sourceCount);
        case 1:
            return qsTr("Replace videos");
        case 2:
            return qsTr("Add video");
        case 3:
            return qsTr("Remove video");
        case 4:
            return qsTr("Change reference");
        case 5:
            return qsTr("Close videos");
        default:
            return qsTr("Review request");
        }
    }

    function intentErrorText(error) {
        switch (Number(error)) {
        case 2:
            return qsTr("The review request queue is full.");
        case 3:
            return qsTr("The video list changed; please repeat the operation.");
        case 4:
            return qsTr("The review request could not be started.");
        case 5:
            return qsTr("The review request failed.");
        default:
            return qsTr("The review request was rejected.");
        }
    }

    function errorMessage(errorKey) {
        switch (errorKey) {
        case "invalid-argument":
            return qsTr("The request is invalid.");
        case "invalid-rate":
            return qsTr("The media frame rate is invalid.");
        case "invalid-frame-id":
        case "frame-out-of-range":
            return qsTr("The requested frame is outside the available range.");
        case "invalid-frame-count":
            return qsTr("The media frame count is invalid.");
        case "invalid-dimensions":
            return qsTr("The media dimensions are invalid.");
        case "invalid-duration":
            return qsTr("The media duration is invalid.");
        case "invalid-media-descriptor":
            return qsTr("The media description is incomplete or invalid.");
        case "arithmetic-overflow":
            return qsTr("The media timing values are too large to process safely.");
        case "source-frame-rate-mismatch":
            return qsTr("Source frame rates differ; inspect alignment before comparing.");
        case "source-frame-count-mismatch":
            return qsTr("Source frame counts differ; unavailable mapped frames stay Missing.");
        case "source-duration-mismatch":
            return qsTr("Source durations differ; inspect alignment before comparing.");
        case "source-resolution-mismatch":
            return qsTr("Source resolutions differ; visual comparison is resampled.");
        case "source-color-metadata-mismatch":
            return qsTr("Source color metadata differs; comparison applies color conversion.");
        case "source-missing":
            return qsTr("The selected source file is missing or cannot be read.");
        case "source-fingerprint-mismatch":
            return qsTr("The source file no longer matches its recorded identity.");
        case "file-io":
            return qsTr("The file could not be read or written.");
        case "media-open-failed":
            return qsTr("The media file could not be opened.");
        case "media-probe-failed":
            return qsTr("The media information could not be read.");
        case "invalid-cfr-timing":
            return qsTr("The media does not have supported constant-frame-rate timing.");
        case "unsupported-codec":
            return qsTr("The media codec is not supported.");
        case "unsupported-pixel-format":
            return qsTr("The media pixel format is not supported.");
        case "media-decode-failed":
            return qsTr("A video frame could not be decoded.");
        case "frame-timeline-invalid":
            return qsTr("The video timeline cannot address every frame exactly.");
        case "frame-budget-exceeded":
            return qsTr("The frame memory limit was exceeded.");
        case "graphics-unavailable":
            return qsTr("The graphics device is unavailable.");
        case "graphics-device-lost":
            return qsTr("The graphics device was reset or disconnected.");
        case "frame-presentation-timed-out":
            return qsTr("The requested frame was not displayed in time.");
        default:
            return qsTr("An unexpected media error occurred.");
        }
    }
}
