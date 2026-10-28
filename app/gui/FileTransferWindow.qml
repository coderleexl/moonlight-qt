import QtQuick 2.12
import QtQuick.Controls 2.13
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12

ApplicationWindow {
    id: window
    objectName: "fileTransferWindow"
    visible: true
    width: 1100
    height: 760
    minimumWidth: 900
    minimumHeight: 640
    title: qsTr("File transfer") + " · " + hostName
    property bool darkTheme: transferDark
    property bool queueExpanded: false
    readonly property color textColor: darkTheme ? "#E8E8E8" : "#182536"
    readonly property color secondaryColor: darkTheme ? "#A0A0A0" : "#6C7C8F"
    readonly property color cardColor: darkTheme ? "#202020" : "#FFFFFF"
    readonly property color borderColor: darkTheme ? "#373737" : "#E0E7EF"
    readonly property color accentColor: darkTheme ? "#68B5FF" : "#1677FF"
    readonly property color hoverColor: darkTheme ? "#292929" : "#F2F7FC"
    readonly property color selectionColor: darkTheme ? "#253548" : "#E9F3FF"
    readonly property int activeTransfers: {
        var count = 0;
        for (var i = 0; i < transfer.jobs.length; ++i) {
            var state = transfer.jobs[i].state;
            if (state === "queued" || state === "running" || state === "conflict") ++count;
        }
        return count;
    }
    color: darkTheme ? "#171717" : "#F4F7FB"
    Material.theme: darkTheme ? Material.Dark : Material.Light
    Material.accent: accentColor
    ToolTipTheme { darkTheme: window.darkTheme; maximumWidth: Math.min(560, window.width - 48) }
    onClosing: function(close) {
        if (transfer.busy) { close.accepted = false; hide(); }
        else Qt.quit();
    }
    function formatSize(bytes) {
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(1) + " GB";
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB";
        if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB";
        return bytes + " B";
    }
    function statusName(value) {
        switch (value) {
        case "queued": return qsTr("Queued");
        case "running": return qsTr("Transferring");
        case "done": return qsTr("Completed");
        case "failed": return qsTr("Failed");
        case "cancelled": return qsTr("Cancelled");
        case "skipped": return qsTr("Skipped");
        case "conflict": return qsTr("Waiting for your choice");
        }
        return value;
    }
    function chooseFolder(remote) {
        destinationDialog.transferring = false;
        destinationDialog.destinationRemote = remote;
        destinationDialog.frozenNames = [];
        transfer.openFolderPicker(remote, remote ? transfer.remotePath : transfer.localPath);
        destinationDialog.open();
    }
    function chooseTransfer(sourceRemote, names, sourcePath) {
        if (!names.length) return;
        destinationDialog.transferring = true;
        destinationDialog.destinationRemote = !sourceRemote;
        destinationDialog.frozenNames = names.slice();
        destinationDialog.frozenSourcePath = sourcePath;
        transfer.openFolderPicker(!sourceRemote, sourceRemote ? transfer.localPath : transfer.remotePath);
        destinationDialog.open();
    }
    function newFolder(remote, picker) {
        folderDialog.remote = remote;
        folderDialog.picker = picker;
        folderName.clear();
        folderDialog.open();
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Image { source: "qrc:/res/file-transfer.svg"; sourceSize.width: 26; sourceSize.height: 26 }
            Label { textFormat: Text.PlainText; text: qsTr("File transfer"); font.pixelSize: 21; font.bold: true; color: window.textColor }
            Label { text: transfer.ready ? qsTr("Connected") : qsTr("Connecting…"); color: window.secondaryColor; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Speed limit"); color: window.secondaryColor; font.pixelSize: 12 }
            ComboBox {
                model: ["1 MB/s", "4 MB/s", "10 MB/s", qsTr("Unlimited")]
                currentIndex: 1
                onActivated: transfer.limitMiB = [1, 4, 10, 0][currentIndex]
                implicitWidth: 126
                implicitHeight: 36
            }
        }
        SplitView {
            id: browserSplit
            objectName: "fileBrowserSplitView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 12
                color: "transparent"
                Rectangle {
                    anchors.centerIn: parent
                    width: 3; height: 38; radius: 2
                    color: SplitHandle.pressed ? window.accentColor : SplitHandle.hovered ? window.secondaryColor : window.borderColor
                }
            }
            FileTransferPane {
                id: localPane
                SplitView.minimumWidth: 350
                SplitView.preferredWidth: (browserSplit.width - 12) / 2
                onNewFolderRequested: function(remote) { window.newFolder(remote, false); }
                onFolderRequested: function(remote) { window.chooseFolder(remote); }
                onTransferRequested: function(sourceRemote, names, sourcePath) { window.chooseTransfer(sourceRemote, names, sourcePath); }
            }
            FileTransferPane {
                id: remotePane
                remote: true
                SplitView.minimumWidth: 350
                SplitView.fillWidth: true
                onNewFolderRequested: function(remote) { window.newFolder(remote, false); }
                onFolderRequested: function(remote) { window.chooseFolder(remote); }
                onTransferRequested: function(sourceRemote, names, sourcePath) { window.chooseTransfer(sourceRemote, names, sourcePath); }
            }
        }
        Label {
            textFormat: Text.PlainText
            visible: transfer.error.length > 0
            text: transfer.error
            color: darkTheme ? "#FFAAAA" : "#B53838"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.pixelSize: 12
            ToolTip.visible: errorHover.containsMouse
            ToolTip.text: text
            MouseArea { id: errorHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: window.queueExpanded ? Math.min(180, window.height * 0.22) : 48
            radius: 10
            color: window.cardColor
            border.color: window.borderColor
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 5
                anchors.bottomMargin: window.queueExpanded ? 10 : 5
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    ToolButton {
                        objectName: "toggleTransferQueueButton"
                        text: window.queueExpanded ? "▾" : "▸"
                        implicitWidth: 28; implicitHeight: 34
                        Accessible.name: qsTr("Show or hide transfers")
                        onClicked: window.queueExpanded = !window.queueExpanded
                    }
                    Label { text: qsTr("Transfer queue"); color: window.textColor; font.bold: true; font.pixelSize: 13 }
                    Label { text: window.activeTransfers ? qsTr("%1 active").arg(window.activeTransfers) : qsTr("%1 tasks").arg(transfer.jobs.length); color: window.secondaryColor; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    Label { visible: !window.queueExpanded; text: qsTr("Closing this window keeps transfers running"); color: window.secondaryColor; font.pixelSize: 11 }
                    ToolButton { text: qsTr("Clear finished"); onClicked: transfer.clearFinished(); implicitHeight: 32 }
                }
                ListView {
                    id: queue
                    objectName: "transferQueue"
                    visible: window.queueExpanded
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: transfer.jobs
                    spacing: 8
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ColumnLayout {
                        width: queue.width - 12
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                textFormat: Text.PlainText
                                text: (modelData.upload ? "↑ " : "↓ ") + modelData.name
                                color: window.textColor
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                                font.pixelSize: 12
                                ToolTip.visible: jobHover.containsMouse
                                ToolTip.text: modelData.name
                                MouseArea { id: jobHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                            }
                            Label { text: modelData.committing && modelData.state === "running" ? qsTr("Finishing…") : modelData.native && modelData.state === "done" ? qsTr("Data delivered") : window.statusName(modelData.state); color: window.secondaryColor; font.pixelSize: 11 }
                            Label { visible: modelData.state === "running"; text: window.formatSize(modelData.done) + " / " + window.formatSize(modelData.size) + (modelData.native ? "" : " · " + window.formatSize(Math.round(modelData.speed)) + "/s"); color: window.secondaryColor; font.pixelSize: 11 }
                            ToolButton { text: qsTr("Cancel"); visible: modelData.state === "queued" || modelData.state === "running" || modelData.state === "conflict"; enabled: !modelData.committing; implicitHeight: 28; onClicked: transfer.cancel(index) }
                            ToolButton { text: qsTr("Retry"); visible: !modelData.native && (modelData.state === "failed" || modelData.state === "cancelled"); implicitHeight: 28; onClicked: transfer.retry(index) }
                        }
                        ProgressBar { visible: modelData.state === "running"; Layout.fillWidth: true; value: modelData.size > 0 ? modelData.done / modelData.size : 0; implicitHeight: 4 }
                        Label {
                            textFormat: Text.PlainText
                            visible: modelData.destinationDisplay !== undefined && modelData.destinationDisplay.length > 0
                            text: qsTr("Destination: %1").arg(modelData.destinationDisplay || "")
                            color: window.secondaryColor
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            font.pixelSize: 11
                            ToolTip.visible: jobDestinationHover.containsMouse
                            ToolTip.text: text
                            MouseArea { id: jobDestinationHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                        }
                        Label { textFormat: Text.PlainText; visible: modelData.detail.length > 0; text: modelData.detail; color: darkTheme ? "#FFAAAA" : "#B53838"; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11 }
                    }
                    Label { textFormat: Text.PlainText; anchors.centerIn: parent; visible: queue.count === 0; text: qsTr("Select files or folders to start a transfer"); color: window.secondaryColor; font.pixelSize: 12 }
                }
            }
        }
    }
    Dialog {
        id: destinationDialog
        objectName: "destinationDialog"
        property bool transferring: false
        property bool destinationRemote: false
        property var frozenNames: []
        property string frozenSourcePath: ""
        readonly property bool pathEdited: folderPathField.text !== transfer.folderDisplayPath
        anchors.centerIn: parent
        width: Math.min(720, window.width - 64)
        height: Math.min(610, window.height - 64)
        modal: true
        title: transferring ? (destinationRemote ? qsTr("Choose upload destination") : qsTr("Choose download destination")) : qsTr("Choose folder")
        closePolicy: Popup.CloseOnEscape
        padding: 20
        background: Rectangle { color: window.cardColor; radius: 10; border.color: window.borderColor }
        Overlay.modal: Rectangle { color: window.darkTheme ? "#99000000" : "#550C1B2F" }
        header: Label {
            text: destinationDialog.title
            textFormat: Text.PlainText
            color: window.textColor
            font.pixelSize: 18
            font.bold: true
            leftPadding: 20; rightPadding: 20; topPadding: 18; bottomPadding: 10
            wrapMode: Text.Wrap
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Rectangle {
                visible: destinationDialog.transferring
                Layout.fillWidth: true
                implicitHeight: sourceSummary.implicitHeight + 20
                color: window.hoverColor
                radius: 6
                ColumnLayout {
                    id: sourceSummary
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top; anchors.margins: 10
                    spacing: 4
                    Label {
                        textFormat: Text.PlainText
                        text: qsTr("%1 selected: %2").arg(destinationDialog.frozenNames.length).arg(destinationDialog.frozenNames.join(", "))
                        color: window.textColor; font.pixelSize: 12
                        Layout.fillWidth: true; elide: Text.ElideRight
                        ToolTip.visible: sourceHover.containsMouse
                        ToolTip.text: destinationDialog.frozenNames.join("\n")
                        MouseArea { id: sourceHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: qsTr("From: %1").arg(destinationDialog.destinationRemote ? destinationDialog.frozenSourcePath : transfer.displayRemotePath(destinationDialog.frozenSourcePath))
                        color: window.secondaryColor; font.pixelSize: 11
                        Layout.fillWidth: true; elide: Text.ElideMiddle
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ToolButton {
                    text: "↑"; implicitWidth: 34; implicitHeight: 34
                    Accessible.name: qsTr("Parent directory")
                    enabled: !transfer.folderLoading && (transfer.folderParentPath.length > 0
                        || (destinationDialog.destinationRemote && !transfer.remoteFilesystem && transfer.folderPath.length > 0))
                    onClicked: transfer.browseFolderPicker(transfer.folderParentPath)
                }
                ComboBox {
                    Layout.fillWidth: true
                    implicitHeight: 36
                    model: destinationDialog.destinationRemote ? transfer.remotePlaces : transfer.localPlaces
                    textRole: "label"
                    displayText: qsTr("Disks & locations")
                    onActivated: transfer.browseFolderPicker(model[currentIndex].path)
                }
                ToolButton {
                    text: qsTr("New folder"); implicitHeight: 34
                    enabled: transfer.folderValid && !transfer.folderLoading && !destinationDialog.pathEdited
                    onClicked: window.newFolder(destinationDialog.destinationRemote, true)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: folderPathField
                    objectName: "folderPathField"
                    Layout.fillWidth: true
                    implicitHeight: 38
                    text: transfer.folderDisplayPath
                    selectByMouse: true
                    Accessible.name: qsTr("Destination folder")
                    font.pixelSize: 12
                    onAccepted: transfer.browseFolderPicker(text)
                }
                ToolButton {
                    text: "→"; implicitWidth: 34; implicitHeight: 34
                    Accessible.name: qsTr("Go to folder")
                    onClicked: transfer.browseFolderPicker(folderPathField.text)
                }
            }
            Label {
                text: qsTr("Open a folder, then choose this location")
                color: window.secondaryColor; font.pixelSize: 11
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: window.cardColor
                border.color: window.borderColor
                radius: 6
                ListView {
                    id: folderList
                    objectName: "destinationFolderList"
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    model: transfer.folderEntries
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        width: folderList.width
                        height: Math.max(44, directoryLabel.implicitHeight + 16)
                        enabled: !transfer.folderLoading
                        contentItem: RowLayout {
                            spacing: 10
                            Image { source: "qrc:/res/transfer-folder.svg"; sourceSize.width: 22; sourceSize.height: 22; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
                            Label { id: directoryLabel; textFormat: Text.PlainText; text: modelData.name; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; maximumLineCount: 2; elide: Text.ElideRight; color: window.textColor; font.pixelSize: 13 }
                            Label { text: "›"; color: window.secondaryColor }
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.name
                        onClicked: transfer.browseFolderPicker(modelData.path !== undefined ? modelData.path
                            : (transfer.folderPath.length ? transfer.folderPath.replace(/\/+$/, "") + "/" + modelData.name : modelData.name))
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: folderList.count === 0
                        text: transfer.folderLoading ? qsTr("Loading folders…") : transfer.folderError.length ? qsTr("This folder is unavailable") : qsTr("No subfolders · choose this location below")
                        color: window.secondaryColor; font.pixelSize: 12
                    }
                }
            }
            Label { textFormat: Text.PlainText; text: transfer.folderError; visible: text.length > 0; color: window.darkTheme ? "#FFAAAA" : "#B53838"; Layout.fillWidth: true; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight; font.pixelSize: 12 }
            Label {
                textFormat: Text.PlainText
                text: destinationDialog.pathEdited ? qsTr("Press Enter to open this path before choosing it")
                    : qsTr("Destination: %1").arg(transfer.folderDisplayPath)
                color: window.textColor; font.pixelSize: 12
                Layout.fillWidth: true; elide: Text.ElideMiddle
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: qsTr("Cancel"); onClicked: destinationDialog.close() }
                ActionButton {
                    objectName: "chooseFolderButton"
                    visible: !destinationDialog.transferring
                    text: qsTr("Choose this folder")
                    primary: true
                    enabled: transfer.folderValid && !transfer.folderLoading && !destinationDialog.pathEdited
                    onClicked: {
                        if (destinationDialog.destinationRemote) transfer.browseRemote(transfer.folderPath);
                        else transfer.browseLocal(transfer.folderPath);
                        destinationDialog.close();
                    }
                }
                ActionButton {
                    objectName: "confirmTransferButton"
                    visible: destinationDialog.transferring
                    text: destinationDialog.destinationRemote ? qsTr("Upload %1 items").arg(destinationDialog.frozenNames.length) : qsTr("Download %1 items").arg(destinationDialog.frozenNames.length)
                    primary: true
                    enabled: transfer.ready && transfer.folderValid && !transfer.folderLoading
                        && !destinationDialog.pathEdited && destinationDialog.frozenNames.length > 0
                    onClicked: {
                        var destination = transfer.folderPath;
                        if (destinationDialog.destinationRemote) transfer.browseRemote(destination);
                        else transfer.browseLocal(destination);
                        transfer.enqueueTo(destinationDialog.destinationRemote, destinationDialog.frozenNames,
                                           destinationDialog.frozenSourcePath, destination);
                        window.queueExpanded = true;
                        destinationDialog.close();
                    }
                }
            }
        }
    }
    Dialog {
        id: conflictDialog
        title: qsTr("A file with this name already exists")
        anchors.centerIn: parent
        width: Math.min(500, window.width - 64)
        modal: true
        closePolicy: Popup.NoAutoClose
        padding: 20
        background: Rectangle { color: window.cardColor; radius: 10; border.color: window.borderColor }
        Overlay.modal: Rectangle { color: window.darkTheme ? "#99000000" : "#550C1B2F" }
        header: Label {
            text: conflictDialog.title
            textFormat: Text.PlainText
            color: window.textColor
            font.pixelSize: 18
            font.bold: true
            leftPadding: 20; rightPadding: 20; topPadding: 18; bottomPadding: 10
            wrapMode: Text.Wrap
        }
        ColumnLayout {
            anchors.fill: parent
            Label { textFormat: Text.PlainText; text: transfer.conflict; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; maximumLineCount: 3; elide: Text.ElideMiddle; color: window.textColor }
            RowLayout {
                Button { text: qsTr("Skip"); onClicked: transfer.resolveConflict("skip") }
                Button { text: qsTr("Keep both"); onClicked: transfer.resolveConflict("keep") }
                Button { text: qsTr("Overwrite"); onClicked: transfer.resolveConflict("overwrite") }
            }
        }
    }
    Connections {
        target: transfer
        function onConflictChanged() { if (transfer.conflict.length) conflictDialog.open(); else conflictDialog.close(); }
    }
    Dialog {
        id: folderDialog
        property bool remote: false
        property bool picker: false
        title: qsTr("New folder")
        anchors.centerIn: parent
        width: 360
        modal: true
        padding: 20
        background: Rectangle { color: window.cardColor; radius: 10; border.color: window.borderColor }
        Overlay.modal: Rectangle { color: window.darkTheme ? "#99000000" : "#550C1B2F" }
        header: Label {
            text: folderDialog.title
            textFormat: Text.PlainText
            color: window.textColor
            font.pixelSize: 18
            font.bold: true
            leftPadding: 20; rightPadding: 20; topPadding: 18; bottomPadding: 10
            wrapMode: Text.Wrap
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: folderName; width: parent.width; placeholderText: qsTr("Folder name"); selectByMouse: true; onAccepted: folderDialog.accept() }
        onOpened: folderName.forceActiveFocus()
        onAccepted: picker ? transfer.createPickerFolder(folderName.text) : transfer.createFolder(remote, folderName.text)
    }
}
