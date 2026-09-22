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
                text: qsTr("Allow other Moonlight devices to connect to this computer using the built-in Sunshine host.")
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
                    text: !SunshineManager.supported ? qsTr("Hosting is currently available on Windows and macOS only") : !SunshineManager.installed ? qsTr("Built-in host is not included in this build") : hostPage.running ? qsTr("Host running") : SunshineManager.state === SunshineManager.Starting ? qsTr("Starting host…") : SunshineManager.state === SunshineManager.Stopping ? qsTr("Stopping host…") : SunshineManager.state === SunshineManager.Failed ? qsTr("Host could not start") : qsTr("Host stopped")
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
                    text: qsTr("Configuration & pairing")
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
            Label {
                text: qsTr("For the first connection, create a host login in Configuration & pairing, then add this computer and enter the pairing PIN in the host configuration.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: window.secondaryColor
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
                visible: !SunshineManager.needsMacPermissions
                text: qsTr("For LAN connections on Windows, allow the bundled Sunshine through Windows Firewall on private networks. This preview runs while you are signed in; it does not install a system service.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: window.secondaryColor
            }
            Label {
                text: qsTr("Host address: %1").arg(SunshineManager.localAddress)
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: window.secondaryColor
            }
            Label {
                text: qsTr("This preview stops hosting when Moonlight exits. Streaming this screen back to itself creates a mirror effect; use another device to check picture quality and input.")
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
