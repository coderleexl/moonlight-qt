import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.3

import ComputerModel 1.0
import ComputerManager 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

FocusScope {
    id: pcPage
    objectName: qsTr("Devices")
    focus: true
    property ComputerModel computerModel: createModel()
    readonly property string decoderStatus: !SystemProperties.decoderInfoReady ? qsTr("Not detected") : SystemProperties.hasHardwareAcceleration ? qsTr("Hardware decoding") : qsTr("Software decoding")
    readonly property bool narrow: width < 710
    property bool showDetail: false
    property string pendingDesktopUuid: ""
    property bool pairingBusy: false
    readonly property var selectedDevice: pcList.currentItem

    function resetSelection() {
        pcList.currentIndex = -1;
        showDetail = false;
        deviceMenu.close();
        renamePcDialog.close();
        renamePcDialog.pcIndex = -1;
        deletePcDialog.close();
        deletePcDialog.pcIndex = -1;
    }

    function openApps(browseOnly, showHidden, desktopOnly) {
        var device = selectedDevice;
        if (!device || !device.pcOnline || !device.pcPaired || !device.pcSupported)
            return;
        var component = Qt.createComponent("AppView.qml");
        if (component.status !== Component.Ready) {
            errorDialog.text = component.errorString();
            errorDialog.open();
            return;
        }
        var appView = component.createObject(stackView, {
            "computerIndex": pcList.currentIndex,
            "objectName": device.pcName,
            "showGames": browseOnly,
            "showHiddenGames": showHidden,
            "launchDesktop": desktopOnly === true
        });
        stackView.push(appView);
    }

    function activateDevice() {
        var device = selectedDevice;
        if (!device || device.pcUnknown)
            return;
        if (!device.pcOnline) {
            if (device.pcWakeable)
                computerModel.wakeComputer(pcList.currentIndex);
        } else if (!device.pcSupported) {
            errorDialog.text = qsTr("The version of GeForce Experience on %1 is not supported by this build of Moonlight. You must update Moonlight to stream from %1.").arg(device.pcName);
            errorDialog.helpText = "";
            errorDialog.open();
        } else if (!device.pcPaired) {
            if (pairingBusy)
                return;
            if (device.pcDeskId.length > 0) {
                accessDialog.targetUuid = device.pcUuid;
                accessDialog.deviceName = device.pcName;
                accessDialog.open();
                return;
            }
            pairingBusy = true;
            pairDialog.pin = computerModel.generatePinString();
            computerModel.pairComputer(pcList.currentIndex, pairDialog.pin);
            pairDialog.open();
        } else {
            openApps(true, true, true);
        }
    }

    StackView.onActivated: {
        ComputerManager.computerAddCompleted.connect(addComplete);
        if (pcList.currentIndex < 0 && pcList.count > 0)
            pcList.currentIndex = 0;
    }
    StackView.onDeactivating: ComputerManager.computerAddCompleted.disconnect(addComplete)

    Connections {
        target: computerModel
        function onModelAboutToBeReset() {
            pcPage.resetSelection();
        }
        function onRowsAboutToBeRemoved() {
            pcPage.resetSelection();
        }
    }

    Keys.onEscapePressed: {
        if (narrow && showDetail) {
            showDetail = false;
            pcList.forceActiveFocus();
        } else
            event.accepted = false;
    }

    function connectById(deviceId) {
        var index = computerModel.findDevice(deviceId);
        if (index < 0) {
            errorDialog.text = index === -2 ? qsTr("More than one device has this ID. Select the device from the list or use its IP address.") : qsTr("Device not found. Start sharing on the other computer and check that both devices are on the same local network. If discovery is blocked, add its IP address and port instead.");
            errorDialog.helpText = "";
            errorDialog.open();
            return;
        }
        pcList.currentIndex = index;
        pcList.positionViewAtIndex(index, ListView.Contain);
        showDetail = true;
        Qt.callLater(activateDevice);
    }

    function pairingComplete(error, uuid) {
        pairingBusy = false;
        pairDialog.close();
        accessProgress.close();
        if (error !== undefined) {
            pendingDesktopUuid = "";
            errorDialog.text = error;
            errorDialog.helpText = "";
            errorDialog.open();
        } else if (pendingDesktopUuid === uuid) {
            pendingDesktopUuid = "";
            var index = computerModel.findDevice(uuid);
            if (index >= 0) {
                pcList.currentIndex = index;
                Qt.callLater(function () { openApps(true, true, true); });
            }
        }
    }

    function addComplete(success, detectedPortBlocking) {
        if (!success) {
            errorDialog.text = qsTr("Unable to connect to the specified PC.");

            if (detectedPortBlocking) {
                errorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking Moonlight. Streaming over the Internet may not work while connected to this network.");
            } else {
                errorDialog.helpText = qsTr("Click the Help button for possible solutions.");
            }

            errorDialog.open();
        }
    }

    function createModel() {
        var model = Qt.createQmlObject('import ComputerModel 1.0; ComputerModel {}', pcPage, '');
        model.initialize(ComputerManager);
        model.pairingCompleted.connect(pairingComplete);
        model.connectionTestCompleted.connect(testConnectionDialog.connectionTestComplete);
        return model;
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            objectName: "deviceListPane"
            color: window.contentColor
            Layout.preferredWidth: pcPage.narrow ? pcPage.width : Math.min(420, pcPage.width * 0.36)
            Layout.fillWidth: pcPage.narrow
            Layout.fillHeight: true
            visible: !pcPage.narrow || !pcPage.showDetail

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 0

                ListView {
                    id: pcList
                    objectName: "computerList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    focus: true
                    activeFocusOnTab: true
                    spacing: 8
                    model: computerModel
                    currentIndex: -1
                    keyNavigationWraps: false
                    onCountChanged: {
                        if (count === 0)
                            pcPage.showDetail = false;
                        else if (currentIndex < 0)
                            currentIndex = 0;
                    }
                    ScrollBar.vertical: ScrollBar {}
                    delegate: NavigableItemDelegate {
                        id: deviceRow
                        width: pcList.width
                        height: 80
                        leftPadding: 20
                        rightPadding: 20
                        grid: pcList
                        listNavigation: true
                        property string pcName: model.name
                        property string pcUuid: model.computerUuid
                        property string pcDeskId: model.deskDeviceId
                        property bool pcOnline: model.online
                        property bool pcPaired: model.paired
                        property bool pcSupported: model.serverSupported
                        property bool pcUnknown: model.statusUnknown
                        property bool pcWakeable: model.wakeable
                        property bool pcBusy: model.busy
                        property string pcDetails: model.details
                        readonly property string stateText: pcUnknown ? qsTr("Checking status…") : !pcOnline ? qsTr("Offline") : !pcSupported ? qsTr("Host update required") : !pcPaired ? (pcDeskId.length ? qsTr("Access password required") : qsTr("Pairing required")) : pcBusy ? qsTr("Online · App running") : qsTr("Online · Paired")
                        readonly property color stateColor: pcUnknown || !pcOnline ? window.secondaryColor : !pcPaired || !pcSupported ? window.warningColor : window.onlineColor
                        Accessible.role: Accessible.Button
                        Accessible.name: pcName + ", " + stateText
                        background: Rectangle {
                            radius: 8
                            color: pcList.currentIndex === index ? window.selectionColor : deviceRow.hovered ? window.hoverColor : "transparent"
                            border.width: deviceRow.highlighted && (deviceRow.visualFocus || pcList.focusReason === Qt.TabFocusReason || pcList.focusReason === Qt.BacktabFocusReason) ? 2 : 0
                            border.color: window.accentColor
                        }
                        contentItem: RowLayout {
                            spacing: 16
                            // A small geometric monitor works in both palettes without a raster asset.
                            Item {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                Rectangle {
                                    x: 2
                                    y: 3
                                    width: 28
                                    height: 20
                                    radius: 3
                                    color: "transparent"
                                    border.color: window.secondaryColor
                                    border.width: 2
                                }
                                Rectangle {
                                    x: 15
                                    y: 23
                                    width: 2
                                    height: 5
                                    color: window.secondaryColor
                                }
                                Rectangle {
                                    x: 9
                                    y: 28
                                    width: 14
                                    height: 2
                                    color: window.secondaryColor
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Label {
                                    text: deviceRow.pcName
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                RowLayout {
                                    spacing: 6
                                    Rectangle {
                                        width: 7
                                        height: 7
                                        radius: 4
                                        color: deviceRow.stateColor
                                    }
                                    Label {
                                        text: deviceRow.stateText
                                        color: window.secondaryColor
                                        font.pixelSize: 13
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            Label {
                                text: "›"
                                color: window.secondaryColor
                                font.pixelSize: 24
                            }
                        }
                        onClicked: {
                            pcList.currentIndex = index;
                            pcPage.showDetail = true;
                            primaryAction.forceActiveFocus(Qt.TabFocusReason);
                        }
                        onPressAndHold: {
                            pcList.currentIndex = index;
                            deviceMenu.initiator = deviceRow;
                            deviceMenu.popup();
                        }
                        Keys.onRightPressed: {
                            pcList.currentIndex = index;
                            pcPage.showDetail = true;
                            primaryAction.forceActiveFocus(Qt.TabFocusReason);
                        }
                        Keys.onMenuPressed: {
                            deviceMenu.initiator = deviceRow;
                            deviceMenu.open();
                        }
                        Keys.onDeletePressed: {
                            deletePcDialog.pcIndex = index;
                            deletePcDialog.pcName = pcName;
                            deletePcDialog.open();
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: deviceRow.pressAndHold()
                        }
                    }
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width - 16
                        visible: pcList.count === 0
                        spacing: 16
                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: parent.visible && StreamingPreferences.enableMdns
                            visible: StreamingPreferences.enableMdns
                        }
                        Label {
                            Layout.fillWidth: true
                            text: StreamingPreferences.enableMdns ? qsTr("Searching for compatible hosts on your local network...") : qsTr("Automatic PC discovery is disabled. Add your PC manually.")
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            color: window.secondaryColor
                        }
                        Button {
                            background: Rectangle {
                                implicitWidth: 104
                                implicitHeight: 44
                                radius: 8
                                color: parent.highlighted ? window.accentColor : parent.flat ? "transparent" : window.surfaceColor
                                border.color: parent.activeFocus || parent.hovered ? window.accentColor : window.borderColor
                                border.width: parent.activeFocus ? 2 : parent.flat ? 0 : 1
                                opacity: parent.enabled ? 1 : 0.5
                            }
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Add PC manually")
                            onClicked: addPcDialog.open()
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: window.dividerColor
            visible: !pcPage.narrow
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !pcPage.narrow || pcPage.showDetail
            spacing: 0
            ScrollView {
                id: detailScroll
                objectName: "deviceDetailPane"
                background: Rectangle {
                    color: window.contentColor
                }
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: detailScroll.availableWidth
                    spacing: 12
                    visible: pcPage.selectedDevice !== null
                    Button {
                        background: Rectangle {
                            implicitWidth: 100
                            implicitHeight: 40
                            radius: 8
                            color: parent.highlighted ? window.accentColor : parent.flat ? "transparent" : window.surfaceColor
                            border.color: parent.activeFocus || parent.hovered ? window.accentColor : window.borderColor
                            border.width: parent.activeFocus ? 2 : parent.flat ? 0 : 1
                            opacity: parent.enabled ? 1 : 0.5
                        }
                        text: qsTr("Back")
                        visible: pcPage.narrow
                        Layout.leftMargin: 24
                        onClicked: {
                            pcPage.showDetail = false;
                            pcList.forceActiveFocus();
                        }
                    }
                    Image {
                        objectName: "hostMonitor"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 144
                        Layout.topMargin: 16
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        source: window.darkTheme ? "qrc:/res/host-monitor-dark.png" : "qrc:/res/host-monitor-light.png"
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: 720
                        mipmap: true
                        smooth: true
                        Accessible.ignored: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        spacing: 10
                        Label {
                            text: pcPage.selectedDevice ? pcPage.selectedDevice.pcName : ""
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: 24
                            font.weight: Font.Bold
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label {
                            text: pcPage.selectedDevice ? pcPage.selectedDevice.stateText : ""
                            horizontalAlignment: Text.AlignHCenter
                            color: window.secondaryColor
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        RowLayout {
                            id: deviceActions
                            objectName: "deviceActions"
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 6
                            spacing: 12
                            readonly property real buttonWidth: Math.max(100, Math.min(128, (parent.width - 24) / 3))
                            ActionButton {
                                id: primaryAction
                                objectName: "primaryDeviceAction"
                                primary: true
                                Layout.preferredWidth: deviceActions.buttonWidth
                                text: !pcPage.selectedDevice ? "" : pcPage.selectedDevice.pcUnknown ? qsTr("Checking status…") : !pcPage.selectedDevice.pcOnline ? qsTr("Wake PC") : !pcPage.selectedDevice.pcSupported ? qsTr("Host update required") : !pcPage.selectedDevice.pcPaired ? qsTr("Pair") : qsTr("Connect desktop")
                                enabled: pcPage.selectedDevice && !pcPage.selectedDevice.pcUnknown && (pcPage.selectedDevice.pcOnline || pcPage.selectedDevice.pcWakeable)
                                onClicked: pcPage.activateDevice()
                                Keys.onLeftPressed: {
                                    pcPage.showDetail = false;
                                    pcList.forceActiveFocus();
                                }
                            }
                            ActionButton {
                                id: browseAction
                                objectName: "browseDeviceAction"
                                Layout.preferredWidth: deviceActions.buttonWidth
                                text: qsTr("View")
                                enabled: pcPage.selectedDevice && pcPage.selectedDevice.pcOnline && pcPage.selectedDevice.pcPaired && pcPage.selectedDevice.pcSupported
                                onClicked: pcPage.openApps(true, true, false)
                            }
                            ActionButton {
                                id: moreButton
                                objectName: "manageDeviceAction"
                                Layout.preferredWidth: deviceActions.buttonWidth
                                text: qsTr("Manage")
                                onClicked: {
                                    deviceMenu.initiator = moreButton;
                                    deviceMenu.open();
                                }
                            }
                        }

                        ColumnLayout {
                            objectName: "deviceInformation"
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            Layout.bottomMargin: 16
                            spacing: 10
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: window.dividerColor
                                Layout.topMargin: 12
                                Layout.bottomMargin: 12
                            }
                            Label {
                                text: qsTr("Current configuration")
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: qsTr("%1 × %2 · %3 FPS").arg(StreamingPreferences.width).arg(StreamingPreferences.height).arg(StreamingPreferences.fps)
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                            Label {
                                text: qsTr("Target bitrate: %1 Mbps").arg(StreamingPreferences.bitrateKbps / 1000)
                                color: window.secondaryColor
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: window.dividerColor
                                Layout.topMargin: 8
                                Layout.bottomMargin: 4
                            }
                            Label {
                                text: qsTr("Local display capabilities")
                                font.weight: Font.DemiBold
                            }
                            Repeater {
                                model: SystemProperties.displayCapabilities
                                delegate: Flow {
                                    Layout.fillWidth: true
                                    spacing: 20
                                    CapabilityIndicator {
                                        objectName: "displayCapability"
                                        iconSource: "qrc:/res/capability-display.svg"
                                        text: modelData.width + " × " + modelData.height
                                        description: modelData.name + " · " + qsTr("Native resolution")
                                    }
                                    CapabilityIndicator {
                                        objectName: "refreshCapability"
                                        iconSource: "qrc:/res/capability-refresh.svg"
                                        text: modelData.refreshRate > 0 ? qsTr("Up to %1 Hz").arg(modelData.refreshRate) : qsTr("Not detected")
                                        description: modelData.name + " · " + qsTr("Maximum refresh rate at native resolution")
                                    }
                                    CapabilityIndicator {
                                        objectName: "decoderCapability"
                                        visible: index === 0
                                        iconSource: "qrc:/res/capability-chip.svg"
                                        text: pcPage.decoderStatus
                                        description: qsTr("Local video decoding")
                                    }
                                }
                            }
                            CapabilityIndicator {
                                visible: SystemProperties.displayCapabilities.length === 0
                                iconSource: "qrc:/res/capability-chip.svg"
                                text: pcPage.decoderStatus
                                description: qsTr("Local video decoding")
                            }
                            Label {
                                visible: SystemProperties.displayCapabilities.length === 0
                                text: qsTr("Display information is unavailable")
                                color: window.secondaryColor
                            }
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    visible: !pcPage.selectedDevice
                    text: qsTr("Select a computer to get started")
                    color: window.secondaryColor
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            Label {
                objectName: "capabilityFootnote"
                visible: pcPage.selectedDevice !== null
                text: qsTr("Local detection · Host and network support required")
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 12
                Layout.bottomMargin: 16
                color: window.secondaryColor
                font.pixelSize: 11
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
            }
        }
    }

    NavigableMenu {
        id: deviceMenu
        parent: moreButton
        y: moreButton.height
        NavigableMenuItem {
            text: qsTr("Wake PC")
            visible: pcPage.selectedDevice && !pcPage.selectedDevice.pcOnline && pcPage.selectedDevice.pcWakeable
            onTriggered: computerModel.wakeComputer(pcList.currentIndex)
        }
        NavigableMenuItem {
            text: qsTr("Test Network")
            enabled: pcPage.selectedDevice !== null
            onTriggered: {
                computerModel.testConnectionForComputer(pcList.currentIndex);
                testConnectionDialog.open();
            }
        }
        NavigableMenuItem {
            text: qsTr("Rename PC")
            enabled: pcPage.selectedDevice !== null
            onTriggered: {
                renamePcDialog.pcIndex = pcList.currentIndex;
                renamePcDialog.originalName = pcPage.selectedDevice.pcName;
                renamePcDialog.open();
            }
        }
        NavigableMenuItem {
            text: qsTr("Delete PC")
            enabled: pcPage.selectedDevice !== null
            onTriggered: {
                deletePcDialog.pcIndex = pcList.currentIndex;
                deletePcDialog.pcName = pcPage.selectedDevice.pcName;
                deletePcDialog.open();
            }
        }
        NavigableMenuItem {
            text: qsTr("View Details")
            enabled: pcPage.selectedDevice !== null
            onTriggered: {
                showPcDetailsDialog.pcDetails = pcPage.selectedDevice.pcDetails;
                showPcDetailsDialog.open();
            }
        }
    }

    ErrorMessageDialog {
        id: errorDialog

        // Using Setup-Guide here instead of Troubleshooting because it's likely that users
        // will arrive here by forgetting to enable GameStream or not forwarding ports.
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide"
    }

    NavigableDialog {
        id: accessDialog
        objectName: "deskAccessDialog"
        title: qsTr("Connect to %1").arg(deviceName)
        property string targetUuid: ""
        property string deviceName: ""
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            accessInput.forceActiveFocus();
            standardButton(Dialog.Ok).enabled = false;
        }
        onClosed: accessInput.clear()
        onAccepted: {
            var index = computerModel.findDevice(targetUuid);
            if (index < 0 || pairingBusy)
                return;
            pendingDesktopUuid = targetUuid;
            pairingBusy = true;
            computerModel.pairComputer(index, accessInput.text.trim());
            accessInput.clear();
            accessProgress.open();
        }
        ColumnLayout {
            width: parent.width
            spacing: 16
            Label {
                Layout.fillWidth: true
                text: qsTr("Enter the access password shown on the other computer's This computer page. Authorization is remembered for future connections.")
                wrapMode: Text.Wrap
            }
            TextField {
                id: accessInput
                objectName: "deskAccessPasswordInput"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                selectByMouse: true
                placeholderText: qsTr("Access password")
                maximumLength: 64
                onTextChanged: {
                    var button = accessDialog.standardButton(Dialog.Ok);
                    if (button) button.enabled = text.trim().length > 0;
                }
                onAccepted: if (text.trim().length > 0) accessDialog.accept()
            }
        }
    }

    NavigableMessageDialog {
        id: accessProgress
        text: qsTr("Authorizing this device…")
        showSpinner: true
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose
    }

    NavigableMessageDialog {
        id: pairDialog
        objectName: "pairComputerDialog"
        closePolicy: Popup.CloseOnEscape

        // don't allow edits to the rest of the window while open
        property string pin: "0000"
        text: qsTr("Please enter %1 on your host PC. This dialog will close when pairing is completed.").arg(pin) + "\n\n" + qsTr("If your host PC is running Sunshine, navigate to the Sunshine web UI to enter the PIN.") + "\n\n" + qsTr("Closing this dialog does not cancel pairing on the host.")
        standardButtons: Dialog.Close
        onRejected: {
            // FIXME: We should interrupt pairing here
        }
    }

    NavigableMessageDialog {
        id: deletePcDialog
        objectName: "deleteComputerDialog"
        // don't allow edits to the rest of the window while open
        property int pcIndex: -1
        property string pcName: ""
        text: qsTr("Are you sure you want to remove '%1'?").arg(pcName)
        standardButtons: Dialog.Yes | Dialog.No

        onAccepted: {
            if (pcIndex >= 0)
                computerModel.deleteComputer(pcIndex);
        }
    }

    NavigableMessageDialog {
        id: testConnectionDialog
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok

        onAboutToShow: {
            testConnectionDialog.text = qsTr("Moonlight is testing your network connection to determine if any required ports are blocked.") + "\n\n" + qsTr("This may take a few seconds…");
            showSpinner = true;
        }

        function connectionTestComplete(result, blockedPorts) {
            if (result === -1) {
                text = qsTr("The network test could not be performed because none of Moonlight's connection testing servers were reachable from this PC. Check your Internet connection or try again later.");
                imageSrc = "qrc:/res/baseline-warning-24px.svg";
            } else if (result === 0) {
                text = qsTr("This network does not appear to be blocking Moonlight. If you still have trouble connecting, check your PC's firewall settings.") + "\n\n" + qsTr("If you are trying to stream over the Internet, install the Moonlight Internet Hosting Tool on your gaming PC and run the included Internet Streaming Tester to check your gaming PC's Internet connection.");
                imageSrc = "qrc:/res/baseline-check_circle_outline-24px.svg";
            } else {
                text = qsTr("Your PC's current network connection seems to be blocking Moonlight. Streaming over the Internet may not work while connected to this network.") + "\n\n" + qsTr("The following network ports were blocked:") + "\n";
                text += blockedPorts;
                imageSrc = "qrc:/res/baseline-error_outline-24px.svg";
            }

            // Stop showing the spinner and show the image instead
            showSpinner = false;
        }
    }

    NavigableDialog {
        id: renamePcDialog
        objectName: "renameComputerDialog"
        property string label: qsTr("Enter the new name for this PC:")
        property string originalName
        property int pcIndex: -1

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
            if (pcIndex >= 0 && editText.text.trim()) {
                computerModel.renameComputer(pcIndex, editText.text.trim());
            }
        }

        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: renamePcDialog.label
                font.bold: true
            }

            TextField {
                id: editText
                placeholderText: renamePcDialog.originalName
                onTextChanged: {
                    var button = renamePcDialog.standardButton(Dialog.Ok);
                    if (button)
                        button.enabled = text.trim().length > 0;
                }
                Layout.fillWidth: true
                focus: true

                Keys.onReturnPressed: {
                    if (editText.text.trim())
                        renamePcDialog.accept();
                }

                Keys.onEnterPressed: {
                    if (editText.text.trim())
                        renamePcDialog.accept();
                }
            }
        }
    }

    NavigableMessageDialog {
        id: showPcDetailsDialog
        objectName: "computerDetailsDialog"
        property string pcDetails: ""
        text: showPcDetailsDialog.pcDetails
        imageSrc: "qrc:/res/baseline-help_outline-24px.svg"
        standardButtons: Dialog.Ok
    }
}
