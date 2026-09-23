import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import QtQuick.Controls.Material 2.2

import ComputerManager 1.0
import AutoUpdateChecker 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SunshineManager 1.0
import SdlGamepadKeyNavigation 1.0

ApplicationWindow {
    id: window
    property bool pollingActive: false

    // Set by SettingsView to force the back operation to pop all
    // pages except the initial view. This is required when doing
    // a retranslate() because AppView breaks for some reason.
    property bool clearOnBack: false
    width: 1100
    height: 680
    minimumWidth: 640
    minimumHeight: 480
    readonly property bool hostPreview: Qt.application.name === "Desk"
    title: hostPreview ? "Desk" : "Moonlight"
    font.pixelSize: 14

    // Session-only appearance preferences. Persistence belongs to P1.
    property bool darkTheme: false
    readonly property bool compactNavigation: width < 860
    readonly property color pageColor: darkTheme ? "#1F1F1F" : "#F5F7FA"
    readonly property color sidebarColor: darkTheme ? "#181818" : "#F0F3F8"
    readonly property color contentColor: darkTheme ? "#1F1F1F" : "#FFFFFF"
    readonly property color surfaceColor: darkTheme ? "#292929" : "#FFFFFF"
    readonly property color textColor: darkTheme ? "#CCCCCC" : "#17212B"
    readonly property color secondaryColor: darkTheme ? "#A0A0A0" : "#657184"
    readonly property color accentColor: darkTheme ? "#4DAAFC" : "#1677FF"
    readonly property color selectionColor: darkTheme ? "#383838" : "#E7F0FF"
    readonly property color borderColor: darkTheme ? "#3C3C3C" : "#E5E9F0"
    readonly property color onlineColor: darkTheme ? "#66D89C" : "#167A48"
    readonly property color warningColor: darkTheme ? "#F4C36A" : "#8D5D00"

    readonly property color dividerColor: darkTheme ? "#2B2B2B" : "#E9EDF3"
    readonly property color hoverColor: darkTheme ? "#333333" : "#F3F6FB"

    Material.theme: darkTheme ? Material.Dark : Material.Light
    Material.background: pageColor
    Material.foreground: textColor
    Material.accent: accentColor
    Material.primary: sidebarColor
    color: pageColor

    function focusPage() {
        if (stackView.currentItem)
            stackView.currentItem.forceActiveFocus(Qt.TabFocusReason);
    }

    function showComputers() {
        if (stackView.depth > 1)
            stackView.pop(null);
        clearOnBack = false;
        focusPage();
    }

    // This function runs prior to creation of the initial StackView item
    function doEarlyInit() {
        SdlGamepadKeyNavigation.enable();
    }

    Component.onCompleted: {
        // Show the window according to the user's preferences
        if (SystemProperties.hasDesktopEnvironment) {
            if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_MAXIMIZED) {
                window.showMaximized();
            } else if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_FULLSCREEN) {
                window.showFullScreen();
            } else {
                window.show();
            }
        } else {
            window.showFullScreen();
        }

        // Display any modal dialogs for configuration warnings
        if (runConfigChecks) {
            if (SystemProperties.isWow64) {
                wow64Dialog.open();
            }

            // Hardware acceleration and unmapped gamepads are checked asynchronously
            SystemProperties.hasHardwareAccelerationChanged.connect(hasHardwareAccelerationChanged);
            SystemProperties.unmappedGamepadsChanged.connect(hasUnmappedGamepadsChanged);
            SystemProperties.startAsyncLoad();
        }
    }

    function hasHardwareAccelerationChanged() {
        if (!SystemProperties.hasHardwareAcceleration && StreamingPreferences.videoDecoderSelection !== StreamingPreferences.VDS_FORCE_SOFTWARE) {
            if (SystemProperties.isRunningXWayland) {
                xWaylandDialog.open();
            } else {
                noHwDecoderDialog.open();
            }
        }
    }

    function hasUnmappedGamepadsChanged() {
        if (SystemProperties.unmappedGamepads) {
            unmappedGamepadDialog.unmappedGamepads = SystemProperties.unmappedGamepads;
            unmappedGamepadDialog.open();
        }
    }

    Item {
        id: tooltipScope
        visible: false
        ToolTip.toolTip.contentWidth: Math.min(tooltipText.implicitWidth, 400, window.width - 32)
        Text {
            id: tooltipText
            text: tooltipScope.ToolTip.toolTip.text
            font: tooltipScope.ToolTip.toolTip.font
        }
    }

    function goBack() {
        if (clearOnBack) {
            // Pop all items except the first one
            stackView.pop(null);
            clearOnBack = false;
        } else {
            stackView.pop();
        }
    }

    // This timer keeps us polling for 5 minutes of inactivity
    // to allow the user to work with Moonlight on a second display
    // while dealing with configuration issues. This will ensure
    // machines come online even if the input focus isn't on Moonlight.
    Timer {
        id: inactivityTimer
        interval: 5 * 60000
        onTriggered: {
            if (!active && pollingActive) {
                ComputerManager.stopPollingAsync();
                pollingActive = false;
            }
        }
    }

    onVisibleChanged: {
        // When we become invisible while streaming is going on,
        // stop polling immediately.
        if (!visible) {
            inactivityTimer.stop();

            if (pollingActive) {
                ComputerManager.stopPollingAsync();
                pollingActive = false;
            }
        } else if (active) {
            // When we become visible and active again, start polling
            inactivityTimer.stop();

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling();
                pollingActive = true;
            }
        }

        // Poll for gamepad input only when the window is in focus
        SdlGamepadKeyNavigation.notifyWindowFocus(visible && active);
    }

    onActiveChanged: {
        if (active) {
            // Stop the inactivity timer
            inactivityTimer.stop();

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling();
                pollingActive = true;
            }
        } else {
            // Start the inactivity timer to stop polling
            // if focus does not return within a few minutes.
            inactivityTimer.restart();
        }

        // Poll for gamepad input only when the window is in focus
        SdlGamepadKeyNavigation.notifyWindowFocus(visible && active);
    }

    function navigateTo(url, objectType) {
        var existingItem = stackView.find(function (item, index) {
            return item instanceof objectType;
        });

        if (existingItem !== null) {
            // Pop to the existing item
            stackView.pop(existingItem);
        } else {
            // Create a new item
            stackView.push(url);
        }
    }

    RowLayout {
        id: applicationLayout
        anchors.fill: parent
        spacing: 0
        Rectangle {
            id: navigation
            objectName: "navigation"
            Layout.preferredWidth: window.compactNavigation ? 68 : 184
            Layout.minimumWidth: Layout.preferredWidth
            Layout.maximumWidth: Layout.preferredWidth
            Layout.fillHeight: true
            color: window.sidebarColor
            visible: runConfigChecks && toolBar.visible

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: window.dividerColor
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 24
                anchors.bottomMargin: 16
                spacing: 8

                Label {
                    text: window.compactNavigation ? (window.hostPreview ? "D" : "M") : window.hostPreview ? "Desk" : "Moonlight"
                    font.pixelSize: window.compactNavigation ? 22 : 20
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    Layout.leftMargin: window.compactNavigation ? 0 : 20
                    Layout.rightMargin: window.compactNavigation ? 0 : 12
                    Layout.bottomMargin: 16
                    horizontalAlignment: window.compactNavigation ? Text.AlignHCenter : Text.AlignLeft
                }

                NavigableToolButton {
                    navigationItem: true
                    objectName: "devicesNavigation"
                    text: qsTranslate("PcView", "Devices")
                    iconSource: "qrc:/res/desktop_windows-48px.svg"
                    display: window.compactNavigation ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    Layout.fillWidth: true
                    checked: stackView.currentItem instanceof PcView || stackView.currentItem instanceof AppView
                    onClicked: window.showComputers()
                    Keys.onDownPressed: (hostButton.visible ? hostButton : settingsButton).forceActiveFocus(Qt.TabFocusReason)
                    Keys.onRightPressed: window.focusPage()
                }

                NavigableToolButton {
                    id: hostButton
                    navigationItem: true
                    objectName: "hostNavigation"
                    text: qsTranslate("HostView", "This computer")
                    iconSource: "qrc:/res/capability-chip.svg"
                    display: window.compactNavigation ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    Layout.fillWidth: true
                    visible: SunshineManager.supported
                    checked: stackView.currentItem instanceof HostView
                    onClicked: navigateTo("qrc:/gui/HostView.qml", HostView)
                    Keys.onDownPressed: settingsButton.forceActiveFocus(Qt.TabFocusReason)
                    Keys.onRightPressed: window.focusPage()
                }

                NavigableToolButton {
                    id: settingsButton
                    navigationItem: true
                    objectName: "settingsNavigation"
                    text: qsTr("Settings")
                    iconSource: "qrc:/res/nav-settings.svg"
                    display: window.compactNavigation ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    Layout.fillWidth: true
                    checked: stackView.currentItem instanceof SettingsView
                    onClicked: navigateTo("qrc:/gui/SettingsView.qml", SettingsView)
                    Keys.onDownPressed: helpButton.forceActiveFocus(Qt.TabFocusReason)
                    Keys.onRightPressed: window.focusPage()
                    Shortcut {
                        sequences: [StandardKey.Preferences]
                        enabled: navigation.visible
                        onActivated: settingsButton.clicked()
                    }
                }

                NavigableToolButton {
                    id: helpButton
                    navigationItem: true
                    objectName: "helpNavigation"
                    text: qsTr("Help")
                    visible: SystemProperties.hasBrowser
                    iconSource: "qrc:/res/nav-help.svg"
                    display: window.compactNavigation ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    Layout.fillWidth: true
                    onClicked: Qt.openUrlExternally("https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide")
                    Keys.onRightPressed: window.focusPage()
                    Shortcut {
                        sequences: [StandardKey.HelpContents]
                        enabled: navigation.visible
                        onActivated: helpButton.clicked()
                    }
                }

                Item {
                    Layout.fillHeight: true
                }

                NavigableToolButton {
                    navigationItem: true
                    objectName: "themeToggle"
                    text: window.darkTheme ? qsTr("Light appearance") : qsTr("Dark appearance")
                    iconSource: window.darkTheme ? "qrc:/res/sun.svg" : "qrc:/res/moon.svg"
                    display: window.compactNavigation ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    Layout.fillWidth: true
                    onClicked: window.darkTheme = !window.darkTheme
                }

                Label {
                    visible: !window.compactNavigation
                    text: qsTr("Version %1").arg(SystemProperties.versionString)
                    font.pixelSize: 12
                    Layout.leftMargin: 20
                    color: window.secondaryColor
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }
        ColumnLayout {
            id: workspace
            objectName: "workspace"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            spacing: 0
            ToolBar {
                id: toolBar
                objectName: "pageHeader"
                Layout.fillWidth: true
                Layout.preferredHeight: stackView.currentItem instanceof PcView ? 100 : 80
                Layout.minimumWidth: 0
                background: Rectangle {
                    color: window.contentColor
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: window.dividerColor
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    spacing: 12

                    NavigableToolButton {
                        text: qsTr("Back")
                        visible: stackView.depth > 1
                        iconSource: "qrc:/res/arrow_left.svg"
                        onClicked: goBack()
                        Keys.onDownPressed: window.focusPage()
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 8
                        Label {
                            objectName: "pageTitle"
                            text: stackView.currentItem ? stackView.currentItem.objectName : "Moonlight"
                            font.pixelSize: 26
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            objectName: "pageSubtitle"
                            visible: stackView.currentItem instanceof PcView
                            text: qsTranslate("PcView", "Your remote workspace")
                            font.pixelSize: 14
                            color: window.secondaryColor
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    NavigableToolButton {
                        text: qsTr("Join our community on Discord")
                        visible: SystemProperties.hasBrowser && stackView.currentItem instanceof SettingsView
                        iconSource: "qrc:/res/discord.svg"
                        onClicked: Qt.openUrlExternally("https://moonlight-stream.org/discord")
                    }
                    NavigableToolButton {
                        id: addPcButton
                        text: qsTr("Add computer")
                        objectName: "addComputer"
                        Material.foreground: window.darkTheme ? "#1F1F1F" : "#FFFFFF"
                        icon.color: Material.foreground
                        background: Rectangle {
                            radius: 8
                            color: addPcButton.down ? Qt.darker(window.accentColor, 1.12) : window.accentColor
                            border.width: addPcButton.activeFocus ? 2 : 0
                            border.color: window.textColor
                        }
                        visible: stackView.currentItem instanceof PcView
                        display: window.width > 850 ? AbstractButton.TextBesideIcon : AbstractButton.IconOnly
                        iconSource: "qrc:/res/add.svg"
                        onClicked: addPcDialog.open()
                        Keys.onDownPressed: window.focusPage()
                        Shortcut {
                            sequences: [StandardKey.New]
                            enabled: addPcButton.visible && toolBar.visible
                            onActivated: addPcButton.clicked()
                        }
                    }
                    NavigableToolButton {
                        id: updateButton
                        property string browserUrl: ""

                        iconSource: "qrc:/res/update.svg"

                        ToolTip.delay: 1000
                        ToolTip.timeout: 3000
                        ToolTip.visible: hovered || visible

                        // Invisible until we get a callback notifying us that
                        // an update is available
                        visible: false

                        onClicked: {
                            if (SystemProperties.hasBrowser) {
                                Qt.openUrlExternally(browserUrl);
                            }
                        }

                        function updateAvailable(version, url) {
                            ToolTip.text = qsTr("Update available for Moonlight: Version %1").arg(version);
                            updateButton.browserUrl = url;
                            updateButton.visible = true;
                        }

                        Component.onCompleted: {
                            AutoUpdateChecker.onUpdateAvailable.connect(updateAvailable);
                            AutoUpdateChecker.start();
                        }

                        Keys.onDownPressed: {
                            stackView.currentItem.forceActiveFocus(Qt.TabFocus);
                        }
                    }
                }
            }
            StackView {
                id: stackView
                objectName: "pageStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 0
                clip: true
                background: Rectangle {
                    color: window.contentColor
                }
                focus: true

                Component.onCompleted: {
                    // Perform our early initialization before constructing
                    // the initial view and pushing it to the StackView
                    doEarlyInit();
                    push(initialView);
                }

                onCurrentItemChanged: {
                    // Ensure focus travels to the next view when going back
                    if (currentItem) {
                        currentItem.forceActiveFocus();
                    }
                }

                Keys.onEscapePressed: {
                    if (depth > 1) {
                        goBack();
                    } else {
                        quitConfirmationDialog.open();
                    }
                }

                Keys.onBackPressed: {
                    if (depth > 1) {
                        goBack();
                    } else {
                        quitConfirmationDialog.open();
                    }
                }

                Keys.onMenuPressed: {
                    settingsButton.clicked();
                }

                // This is a keypress we've reserved for letting the
                // SdlGamepadKeyNavigation object tell us to show settings
                // when Menu is consumed by a focused control.
                Keys.onHangupPressed: {
                    settingsButton.clicked();
                }
            }
        }
    }

    ErrorMessageDialog {
        id: noHwDecoderDialog
        text: qsTr("No functioning hardware accelerated video decoder was detected by Moonlight. " + "Your streaming performance may be severely degraded in this configuration.")
        helpText: qsTr("Click the Help button for more information on solving this problem.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    ErrorMessageDialog {
        id: xWaylandDialog
        text: qsTr("Hardware acceleration doesn't work on XWayland. Continuing on XWayland may result in poor streaming performance. " + "Try running with QT_QPA_PLATFORM=wayland or switch to X11.")
        helpText: qsTr("Click the Help button for more information.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    NavigableMessageDialog {
        id: wow64Dialog
        standardButtons: Dialog.Ok | Dialog.Cancel
        text: qsTr("This version of Moonlight isn't optimized for your PC. Please download the '%1' version of Moonlight for the best streaming performance.").arg(SystemProperties.friendlyNativeArchName)
        onAccepted: {
            Qt.openUrlExternally("https://github.com/moonlight-stream/moonlight-qt/releases");
        }
    }

    ErrorMessageDialog {
        id: unmappedGamepadDialog
        property string unmappedGamepads: ""
        text: qsTr("Moonlight detected gamepads without a mapping:") + "\n" + unmappedGamepads
        helpTextSeparator: "\n\n"
        helpText: qsTr("Click the Help button for information on how to map your gamepads.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Gamepad-Mapping"
    }

    // This dialog appears when quitting via keyboard or gamepad button
    NavigableMessageDialog {
        id: quitConfirmationDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Are you sure you want to quit?")
        // For keyboard/gamepad navigation
        onAccepted: Qt.quit()
    }

    // HACK: This belongs in StreamSegue but keeping a dialog around after the parent
    // dies can trigger bugs in Qt 5.12 that cause the app to crash. For now, we will
    // host this dialog in a QML component that is never destroyed.
    //
    // To repro: Start a stream, cut the network connection to trigger the "Connection
    // terminated" dialog, wait until the app grid times out back to the PC grid, then
    // try to dismiss the dialog.
    ErrorMessageDialog {
        id: streamSegueErrorDialog

        property bool quitAfter: false

        onClosed: {
            if (quitAfter) {
                Qt.quit();
            }

            // StreamSegue assumes its dialog will be re-created each time we
            // start streaming, so fake it by wiping out the text each time.
            text = "";
        }
    }

    NavigableDialog {
        id: addPcDialog
        objectName: "addComputerDialog"
        title: qsTr("Connect to a device")
        property string label: qsTr("Enter the other computer's 9-digit device ID or IP address. Device IDs are looked up on the local network; the access password is entered next.")

        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            // Force keyboard focus on the textbox so keyboard navigation works
            editText.forceActiveFocus();
            standardButton(Dialog.Ok).enabled = editText.text.trim().length > 0;
        }

        onClosed: {
            editText.clear();
        }

        onAccepted: {
            if (editText.text.trim()) {
                var value = editText.text.trim();
                if (/^[0-9]{9}$/.test(value.replace(/\s/g, ""))) {
                    window.showComputers();
                    var page = stackView.get(0);
                    page.connectById(value.replace(/\s/g, ""));
                } else {
                    // Bare IPv4 addresses use Desk's port. An explicit port
                    // still supports standalone Sunshine and legacy hosts.
                    if (/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/.test(value))
                        value += ":48989";
                    ComputerManager.addNewHostManually(value);
                }
            }
        }

        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: addPcDialog.label
                font.bold: true
            }

            TextField {
                id: editText
                onTextChanged: {
                    var button = addPcDialog.standardButton(Dialog.Ok);
                    if (button)
                        button.enabled = text.trim().length > 0;
                }
                Layout.fillWidth: true
                focus: true

                Keys.onReturnPressed: {
                    if (editText.text.trim())
                        addPcDialog.accept();
                }

                Keys.onEnterPressed: {
                    if (editText.text.trim())
                        addPcDialog.accept();
                }
            }
        }
    }
}
