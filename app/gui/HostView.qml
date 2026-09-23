import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import SunshineManager 1.0
import ComputerManager 1.0

FocusScope {
    id: hostPage
    objectName: qsTr("This computer")
    readonly property bool busy: SunshineManager.state === SunshineManager.Starting || SunshineManager.state === SunshineManager.Stopping
    readonly property bool running: SunshineManager.state === SunshineManager.Running
    NavigableMessageDialog {
        id: resetAccessDialog
        text: qsTr("Change the access password and revoke all saved authorizations? Previously connected devices will need the new password.")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            if (!SunshineManager.resetAccess()) {
                accessError.text = qsTr("Unable to reset access. Stop sharing and check that the host configuration is writable.");
                accessError.open();
            }
        }
    }
    NavigableMessageDialog {
        id: accessError
        standardButtons: Dialog.Ok
    }
    ScrollView {
        id: scroll
        objectName: "hostPageScroll"
        anchors.fill: parent
        anchors.margins: 24
        clip: true
        rightPadding: ScrollBar.vertical.width + 8
        contentWidth: availableWidth
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 20
            Label {
                Layout.fillWidth: true
                text: qsTr("Share your desktop with another Desk on the same local network. Start sharing, then give the other person your device ID and access password.")
                wrapMode: Text.Wrap
                color: window.secondaryColor
            }
            RowLayout {
                spacing: 10
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: hostPage.running ? window.onlineColor : window.secondaryColor
                }
                Label {
                    objectName: "hostStatus"
                    text: !SunshineManager.supported ? qsTr("Hosting is not supported on this platform") : !SunshineManager.installed ? qsTr("Built-in host is not included in this build") : hostPage.running ? qsTr("Host running") : SunshineManager.state === SunshineManager.Starting ? qsTr("Starting host…") : SunshineManager.state === SunshineManager.Stopping ? qsTr("Stopping host…") : SunshineManager.state === SunshineManager.Failed ? qsTr("Host could not start") : qsTr("Host stopped")
                    font.pixelSize: 20
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
            Label {
                visible: SunshineManager.error.length > 0
                text: SunshineManager.error
                color: window.warningColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Switch {
                objectName: "hostLocalOnly"
                text: qsTr("Local test only (127.0.0.1)")
                checked: SunshineManager.localOnly
                enabled: !hostPage.running && !hostPage.busy
                onToggled: SunshineManager.localOnly = checked
            }
            Label {
                text: SunshineManager.localOnly ? qsTr("Only this computer can connect. Turn this off while the host is stopped to allow LAN connections.") : qsTr("Paired devices on your local network can connect. Automatic router port forwarding stays disabled.")
                color: window.secondaryColor
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Flow {
                Layout.fillWidth: true
                spacing: 12
                ActionButton {
                    objectName: "hostStartStop"
                    primary: true
                    text: hostPage.running || SunshineManager.state === SunshineManager.Starting ? qsTr("Stop hosting") : qsTr("Start hosting")
                    enabled: SunshineManager.installed && SunshineManager.state !== SunshineManager.Stopping
                    onClicked: hostPage.running || SunshineManager.state === SunshineManager.Starting ? SunshineManager.stop() : SunshineManager.start()
                }
                ActionButton {
                    text: qsTr("Advanced host settings")
                    enabled: hostPage.running
                    onClicked: SunshineManager.openConfiguration()
                }
                ActionButton {
                    objectName: "hostTestLocal"
                    text: qsTr("Add this computer")
                    enabled: hostPage.running
                    onClicked: {
                        window.showComputers();
                        ComputerManager.addNewHostManually(SunshineManager.localAddress);
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 12
                Label {
                    text: qsTr("Device ID")
                    color: window.secondaryColor
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: deviceIdField
                        objectName: "hostDeviceId"
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        text: SunshineManager.deviceId
                        font.pixelSize: 24
                        font.letterSpacing: 3
                    }
                    ActionButton {
                        text: qsTr("Copy ID")
                        onClicked: { deviceIdField.selectAll(); deviceIdField.copy(); deviceIdField.deselect(); }
                    }
                }
                Label {
                    text: qsTr("Access password")
                    color: window.secondaryColor
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: passwordField
                        objectName: "hostAccessPassword"
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        echoMode: showPassword.checked ? TextInput.Normal : TextInput.Password
                        text: SunshineManager.accessPassword
                    }
                    ActionButton {
                        text: qsTr("Copy password")
                        onClicked: {
                            // Password-mode TextInput intentionally blocks copy.
                            passwordField.echoMode = TextInput.Normal;
                            passwordField.selectAll(); passwordField.copy(); passwordField.deselect();
                            passwordField.echoMode = Qt.binding(function () { return showPassword.checked ? TextInput.Normal : TextInput.Password; });
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        id: showPassword
                        text: qsTr("Show password")
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        text: qsTr("Reset access")
                        enabled: !hostPage.running && !hostPage.busy
                        onClicked: resetAccessDialog.open()
                    }
                }
                Label {
                    text: qsTr("Only share the password with people you trust. Reset access while sharing is stopped to change the password and revoke saved authorizations.")
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: window.secondaryColor
                    font.pixelSize: 12
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: window.dividerColor
            }
            Label {
                visible: SunshineManager.needsMacPermissions
                text: qsTr("macOS permissions")
                font.weight: Font.DemiBold
            }
            Label {
                visible: SunshineManager.needsMacPermissions
                text: qsTr("Screen recording is required to share the desktop. Accessibility is required for remote keyboard and mouse input. Restart hosting after changing permissions.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: window.secondaryColor
            }
            Flow {
                visible: SunshineManager.needsMacPermissions
                Layout.fillWidth: true
                spacing: 12
                ActionButton {
                    text: qsTr("Screen recording")
                    onClicked: SunshineManager.openScreenRecordingSettings()
                }
                ActionButton {
                    text: qsTr("Accessibility")
                    onClicked: SunshineManager.openAccessibilitySettings()
                }
            }
            Label {
                visible: Qt.platform.os === "windows"
                text: qsTr("For LAN connections on Windows, allow the bundled Sunshine through Windows Firewall on private networks. Hosting runs while you are signed in; it does not install a system service.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: window.secondaryColor
            }
            Label {
                visible: Qt.platform.os === "linux"
                text: qsTr("On Linux, screen sharing may require approval in your desktop portal. Remote input requires uinput access; sign out and back in after installing Desk if input is unavailable.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: window.secondaryColor
            }
            Label {
                text: qsTr("Host address: %1").arg(SunshineManager.localOnly ? SunshineManager.localAddress : SunshineManager.lanAddresses.join(" / "))
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: window.secondaryColor
            }
            Label {
                text: qsTr("Hosting stops when the app exits. Streaming this screen back to itself creates a mirror effect; use another device to check picture quality and input.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: window.secondaryColor
                font.pixelSize: 12
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: window.dividerColor
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Host log")
                    Layout.fillWidth: true
                    font.weight: Font.DemiBold
                }
                ActionButton {
                    text: qsTr("Open log folder")
                    onClicked: SunshineManager.openLogs()
                }
            }
            ScrollView {
                id: logScroll
                objectName: "hostLogScroll"
                implicitWidth: 0
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                Layout.minimumHeight: 180
                Layout.maximumHeight: 180
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AlwaysOn
                background: Rectangle {
                    color: window.surfaceColor
                    radius: 8
                    border.color: window.borderColor
                }
                TextArea {
                    objectName: "hostLog"
                    readOnly: true
                    selectByMouse: true
                    text: SunshineManager.logText
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.WrapAnywhere
                    font.pixelSize: 12
                    color: window.secondaryColor
                    leftPadding: 12
                    rightPadding: 12 + logScroll.ScrollBar.vertical.width
                    topPadding: 10
                    bottomPadding: 10
                    background: null
                }
            }
        }
    }
}
