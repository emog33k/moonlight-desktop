import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Window 2.2

import SdlGamepadKeyNavigation 1.0
import Session 1.0
import SystemProperties 1.0

Item {
    property Session session
    property string appName
    property string stageText : isResume ? qsTr("Resuming %1...").arg(appName) :
                                           qsTr("Starting %1...").arg(appName)
    property bool isResume : false
    property bool quitAfter : false

    // LocalRDP: reconnecting after the network dropped (or with new settings).
    // reconnectAttempt > 0: this screen is such an attempt. Same limits as reconnect.h.
    property int reconnectAttempt : 0
    readonly property int maxReconnectAttempts : 20
    property bool reconnectCancelled : false
    property bool nextPending : false
    property var nextSession : null
    property int nextAttempt : 0

    function stageStarting(stage)
    {
        // Update the spinner text (a reconnect keeps saying what it is doing)
        if (reconnectAttempt === 0) {
            stageText = qsTr("Starting %1...").arg(stage)
        }
    }

    function showReconnecting(text, cancellable)
    {
        window.visible = true
        stageSpinner.visible = true
        stageLabel.visible = true
        hintText.visible = false
        stageText = text
        reconnectCancelButton.visible = cancellable
    }

    function finishSegue()
    {
        // As at the end of sessionFinished(): back to the PC/app list
        SdlGamepadKeyNavigation.enable()
        if (!quitAfter) {
            stackView.pop()
        }
        window.visible = true
    }

    Component.onCompleted: {
        if (reconnectAttempt > 0) {
            stageText = qsTr("Связь с ПК потеряна. Переподключение (попытка %1 из %2)…")
                            .arg(reconnectAttempt).arg(maxReconnectAttempts)
        }
    }

    Timer {
        id: reconnectTimer
        repeat: false
        onTriggered: {
            if (reconnectCancelled || !nextSession) {
                return
            }
            var component = Qt.createComponent("StreamSegue.qml")
            stackView.replace(stackView.currentItem,
                              component.createObject(stackView, {"appName": appName, "session": nextSession,
                                                                 "isResume": true, "quitAfter": quitAfter,
                                                                 "reconnectAttempt": nextAttempt}),
                              StackView.Immediate)
        }
    }

    function stageFailed(stage, errorCode, failingPorts)
    {
        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = qsTr("Starting %1 failed: Error %2").arg(stage).arg(errorCode)

        if (failingPorts) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("Check your firewall and port forwarding rules for port(s): %1").arg(failingPorts)
        }
    }

    function connectionStarted()
    {
        // Hide the UI contents so the user doesn't
        // see them briefly when we pop off the StackView
        stageSpinner.visible = false
        stageLabel.visible = false
        hintText.visible = false

        // Hide the window now that streaming has begun
        window.visible = false
    }

    function displayLaunchError(text)
    {
        // Display the error dialog after Session::exec() returns
        streamSegueErrorDialog.text = text
        console.error(text)
    }

    function quitStarting()
    {
        // Avoid the push transition animation
        var component = Qt.createComponent("QuitSegue.qml")
        stackView.replace(stackView.currentItem, component.createObject(stackView, {"appName": appName}), StackView.Immediate)

        // Show the Qt window again to show quit segue
        window.visible = true
    }

    function sessionFinished(portTestResult)
    {
        // LocalRDP: reconnecting (new settings, network dropped, or an attempt
        // that failed while attempts are left): no dialog; the next session
        // starts once this one is fully gone (sessionReadyForDeletion)
        if (reconnectCancelled) {
            streamSegueErrorDialog.text = ""
        }
        else if (session && (session.restartRequested || session.reconnectRequested ||
                             (reconnectAttempt > 0 && reconnectAttempt < maxReconnectAttempts))) {
            streamSegueErrorDialog.text = ""
            nextPending = true
            if (session.restartRequested) {
                nextAttempt = 0
                showReconnecting(qsTr("Переподключение с новыми настройками…"), false)
            }
            else {
                nextAttempt = reconnectAttempt + 1
                showReconnecting(qsTr("Связь с ПК потеряна. Переподключение…"), true)
            }
            return
        }

        if (portTestResult !== 0 && portTestResult !== -1 && streamSegueErrorDialog.text) {
            streamSegueErrorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking Moonlight. Streaming over the Internet may not work while connected to this network.")
        }

        // Re-enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable()

        // Pop the StreamSegue off the stack if this is a GUI-based app launch
        if (!quitAfter) {
            stackView.pop()
        }

        if (quitAfter && !streamSegueErrorDialog.text) {
            // If this was a CLI launch without errors, exit now
            Qt.quit()
        }
        else {
            // Show the Qt window again after streaming
            window.visible = true

            // Display any launch errors. We do this after
            // the Qt UI is visible again to prevent losing
            // focus on the dialog which would impact gamepad
            // users.
            if (streamSegueErrorDialog.text) {
                streamSegueErrorDialog.quitAfter = quitAfter
                streamSegueErrorDialog.open()
            }
        }
    }

    function sessionReadyForDeletion()
    {
        if (nextPending && !reconnectCancelled && session) {
            // The old connection is fully closed now: the next one may start
            nextPending = false
            nextSession = session.createRestartSession()
            // Right away for new settings; a moment after a drop; a few seconds between failed attempts
            reconnectTimer.interval = nextAttempt === 0 ? 0 : (nextAttempt === 1 ? 1000 : 3000)
            session = null
            gc()
            reconnectTimer.start()
            return
        }

        // Garbage collect the Session object since it's pretty heavyweight
        // and keeps other libraries (like SDL_TTF) around until it is deleted.
        session = null
        gc()
    }

    StackView.onDeactivating: {
        // Show the toolbar again when popped off the stack
        toolBar.visible = true

        // Re-enable GUI gamepad usage now
        SdlGamepadKeyNavigation.enable()
    }

    StackView.onActivated: {
        // Hide the toolbar before we start loading
        toolBar.visible = false

        // Hook up our signals
        session.stageStarting.connect(stageStarting)
        session.stageFailed.connect(stageFailed)
        session.connectionStarted.connect(connectionStarted)
        session.displayLaunchError.connect(displayLaunchError)
        session.quitStarting.connect(quitStarting)
        session.sessionFinished.connect(sessionFinished)
        session.readyForDeletion.connect(sessionReadyForDeletion)

        // Ensure the SystemProperties async thread is finished,
        // since it may currently be using the SDL video subsystem
        SystemProperties.waitForAsyncLoad()

        // Kick off the stream
        streamLoader.active = true
    }

    Timer {
        id: startSessionTimer
        onTriggered: {
            // Garbage collect QML stuff before we start streaming,
            // since we'll probably be streaming for a while and we
            // won't be able to GC during the stream.
            gc()

            // Run the streaming session to completion
            session.start()
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
            hintText.text = qsTr("Tip:") + " " + qsTr("Press %1 to disconnect your session").arg(SdlGamepadKeyNavigation.getConnectedGamepads() > 0 ?
                                                  qsTr("Start+Select+L1+R1") : qsTr("Ctrl+Alt+Shift+Q"))

            // Stop GUI gamepad usage now
            SdlGamepadKeyNavigation.disable()

            // Initialize the session and probe for host/client capabilities
            if (!session.initialize(window)) {
                sessionFinished(0);
                sessionReadyForDeletion();
                return;
            }

            // This spinner is shown only after session.initialize() has completed
            // to prevent active animations from running during decoder probing,
            // which causes re-entrant event loop livelocks with libdecor-gtk.
            stageSpinner.visible = true

            // Don't wait unless we have toasts to display
            startSessionTimer.interval = 0

            // Display the toasts together in a vertical centered arrangement
            var yOffset = 0
            for (var i = 0; i < session.launchWarnings.length; i++) {
                var text = session.launchWarnings[i]
                console.warn(text)

                // Show the tooltip for 3 seconds
                var toast = Qt.createQmlObject('import QtQuick.Controls 2.2; ToolTip {}', parent, '')
                toast.timeout = 3000
                toast.text = text
                toast.y += yOffset
                toast.visible = true

                // Offset the next toast below the previous one
                yOffset = toast.y + toast.padding + toast.height

                // Allow an extra 500 ms for the tooltip's fade-out animation to finish
                startSessionTimer.interval = toast.timeout + 500;
            }

            // Start the timer to wait for toasts (or start the session immediately)
            startSessionTimer.start()
        }

        sourceComponent: Item {}
    }

    Row {
        anchors.centerIn: parent
        spacing: 5

        BusyIndicator {
            id: stageSpinner
            running: visible
            visible: false
        }

        Label {
            id: stageLabel
            height: stageSpinner.height
            text: stageText
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter

            wrapMode: Text.Wrap
        }
    }

    Button {
        id: reconnectCancelButton
        visible: reconnectAttempt > 0
        anchors.top: parent.verticalCenter
        anchors.topMargin: 60
        anchors.horizontalCenter: parent.horizontalCenter
        text: qsTr("Отмена")
        onClicked: {
            reconnectCancelled = true
            reconnectTimer.stop()
            nextSession = null
            visible = false
            if (session) {
                // An attempt is connecting: stop it; sessionFinished() then goes back
                session.interrupt()
            }
            else {
                finishSegue()
            }
        }
    }

    Label {
        id: hintText
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 50
        anchors.horizontalCenter: parent.horizontalCenter
        font.pointSize: 18
        verticalAlignment: Text.AlignVCenter

        wrapMode: Text.Wrap
    }
}
