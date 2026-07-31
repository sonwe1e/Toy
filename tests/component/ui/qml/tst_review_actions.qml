import QtQuick
import QtTest
import "../../../../src/ui_qml/qml" as Dvs

Item {
    id: root

    QtObject {
        id: controller

        property bool canFirst: true
        property bool canPrevious: true
        property bool canNext: true
        property bool canLast: true
        property bool canPlay: true
        property bool canPause: false
        property bool playing: false
        property int firstCalls: 0
        property int previousCalls: 0
        property int nextCalls: 0
        property int lastCalls: 0
        property int toggleCalls: 0
        property var stepCalls: []

        function first() {
            firstCalls += 1;
        }
        function previous() {
            previousCalls += 1;
        }
        function next() {
            nextCalls += 1;
        }
        function last() {
            lastCalls += 1;
        }
        function togglePlayback() {
            toggleCalls += 1;
        }
        function stepFrames(delta) {
            const nextSteps = stepCalls.slice(0);
            nextSteps.push(delta);
            stepCalls = nextSteps;
        }
    }

    Dvs.ReviewActions {
        id: actions

        controller: controller
        oneSecondStepFrames: 60
        wipeEnabled: true
        wipePosition: 0.5
    }

    SignalSpy {
        id: wipeSpy

        target: actions
        signalName: "wipePositionRequested"
    }

    TestCase {
        name: "ReviewActions"

        function test_review_commands_do_not_depend_on_transport_buttons() {
            actions.firstFrame();
            actions.previousFrame();
            actions.nextFrame();
            actions.lastFrame();
            actions.togglePlayback();
            actions.stepBackwardFive();
            actions.stepForwardFive();
            actions.stepBackwardSecond();
            actions.stepForwardSecond();

            compare(controller.firstCalls, 1);
            compare(controller.previousCalls, 1);
            compare(controller.nextCalls, 1);
            compare(controller.lastCalls, 1);
            compare(controller.toggleCalls, 1);
            compare(controller.stepCalls, [-5, 5, -60, 60]);
        }

        function test_wipe_keyboard_adjustment_is_clamped() {
            wipeSpy.clear();
            actions.moveWipe(-0.05);
            compare(wipeSpy.count, 1);
            compare(wipeSpy.signalArguments[0][0], 0.45);

            actions.wipePosition = 0.99;
            actions.moveWipe(0.05);
            compare(wipeSpy.signalArguments[1][0], 1.0);
        }
    }
}
