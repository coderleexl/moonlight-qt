import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import SdlGamepadKeyNavigation 1.0
import Session 1.0
import SystemProperties 1.0

Item {
    property Session session
    property string appName
    property string stageText: isResume ? qsTr("Resuming %1...").arg(appName) : qsTr("Starting %1...").arg(appName)
    property bool isResume: false
    property bool quitAfter: false

    function stageStarting(stage) {
        // Update the spinner text
        stageText = qsTr("Starting %1...").arg(stage);
    }

    function stageFailed(stage, errorCode, failingPorts) {
        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = qsTr("Starting %1 failed: Error %2").arg(stage).arg(errorCode);

        if (failingPorts) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("Check your firewall and port forwarding rules for port(s): %1").arg(failingPorts);
        }
    }

    function connectionStarted() {
        // Hide the UI contents so the user doesn't
        // see them briefly when we pop off the StackView
        connectionStatus.busy = false;
        connectionStatus.visible = false;
        hintText.visible = false;
        launchWarning.visible = false;

        // Hide the window now that streaming has begun
        window.visible = false;
    }

    function displayLaunchError(text) {
        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = text;
        console.error(text);
    }

    function quitStarting() {
        // Avoid the push transition animation
        var component = Qt.createComponent("QuitSegue.qml");
        stackView.replace(stackView.currentItem, component.createObject(stackView, {
            "appName": appName
        }), StackView.Immediate);

        // Show the Qt window again to show quit segue
        window.visible = true;
    }

    function sessionFinished(portTestResult) {
        if (portTestResult !== 0 && portTestResult !== -1 && streamSegueErrorDialog.text) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking Moonlight. Streaming over the Internet may not work while connected to this network.");
        }

        // Re-enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable();

        // Pop the StreamSegue off the stack if this is a GUI-based app launch
        if (!quitAfter) {
            stackView.pop();
        }

        if (quitAfter && !streamSegueErrorDialog.text) {
            // If this was a CLI launch without errors, exit now
            Qt.quit();
        } else {
            // Show the Qt window again after streaming
            window.visible = true;

            // Display any launch errors. We do this after
            // the Qt UI is visible again to prevent losing
            // focus on the dialog which would impact gamepad
            // users.
            if (streamSegueErrorDialog.text) {
                streamSegueErrorDialog.quitAfter = quitAfter;
                streamSegueErrorDialog.open();
            }
        }
    }

    function sessionReadyForDeletion() {
        // Garbage collect the Session object since it's pretty heavyweight
        // and keeps other libraries (like SDL_TTF) around until it is deleted.
        session = null;
        gc();
    }

    StackView.onDeactivating: {
        // Show the toolbar again when popped off the stack
        toolBar.visible = true;

        // Re-enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable();
    }

    StackView.onActivated: {
        // Hide the toolbar before we start loading
        toolBar.visible = false;

        // Hook up our signals
        session.stageStarting.connect(stageStarting);
        session.stageFailed.connect(stageFailed);
        session.connectionStarted.connect(connectionStarted);
        session.displayLaunchError.connect(displayLaunchError);
        session.quitStarting.connect(quitStarting);
        session.sessionFinished.connect(sessionFinished);
        session.readyForDeletion.connect(sessionReadyForDeletion);

        // Ensure the SystemProperties async thread is finished,
        // since it may currently be using the SDL video subsystem
        SystemProperties.waitForAsyncLoad();

        // Kick off the stream
        streamLoader.active = true;
    }

    Timer {
        id: startSessionTimer
        onTriggered: {
            // Garbage collect QML stuff before we start streaming,
            // since we'll probably be streaming for a while and we
            // won't be able to GC during the stream.
            gc();

            // Run the streaming session to completion
            session.start();
        }
    }

    Loader {
        id: streamLoader
        active: false
        asynchronous: true

        onLoaded: {
            // Set the hint text. We do this here rather than
            // in the hintText control itself to synchronize
            // with Session.exec() which requires no concurrent
            // gamepad usage.
            hintText.text = qsTr("Tip:") + " " + qsTr("Press %1 to disconnect your session").arg(SdlGamepadKeyNavigation.getConnectedGamepads() > 0 ? qsTr("Start+Select+L1+R1") : qsTr("Ctrl+Alt+Shift+Q"));

            // Stop GUI gamepad usage now
            SdlGamepadKeyNavigation.disable();

            // Initialize the session and probe for host/client capabilities
            if (!session.initialize(window)) {
                sessionFinished(0);
                sessionReadyForDeletion();
                return;
            }

            // This spinner is shown only after session.initialize() has completed
            // to prevent active animations from running during decoder probing,
            // which causes re-entrant event loop livelocks with libdecor-gtk.
            connectionStatus.busy = true;

            // Don't wait unless we have toasts to display
            startSessionTimer.interval = 0;

            // Keep launch warnings inside the themed page, above the centered status.
            launchWarningLabel.text = session.launchWarnings.join("\n\n");
            launchWarning.visible = session.launchWarnings.length > 0;
            if (launchWarning.visible)
                startSessionTimer.interval = 3500;

            // Start the timer to wait for toasts (or start the session immediately)
            startSessionTimer.start();
        }

        sourceComponent: Item {}
    }

    ConnectionStatus {
        id: connectionStatus
        anchors.fill: parent
        text: stageText
        busy: false
    }

    Rectangle {
        id: launchWarning
        objectName: "launchWarning"
        visible: false
        anchors.top: parent.top
        anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(640, parent.width - 48)
        height: Math.min(launchWarningLabel.implicitHeight + 32, parent.height * 0.3)
        radius: 8
        color: window.surfaceColor
        border.color: window.borderColor
        ScrollView {
            id: warningScroll
            anchors.fill: parent
            anchors.margins: 16
            clip: true
            contentWidth: availableWidth
            Label {
                id: launchWarningLabel
                objectName: "launchWarningText"
                width: warningScroll.availableWidth
                color: window.textColor
                font.pixelSize: 14
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
        }
    }

    Label {
        id: hintText
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 50
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(640, parent.width - 48)
        horizontalAlignment: Text.AlignHCenter
        color: window.secondaryColor
        font.pixelSize: 16
        verticalAlignment: Text.AlignVCenter

        wrapMode: Text.Wrap
    }
}
