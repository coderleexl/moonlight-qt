import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12

ApplicationWindow {
    id: window
    objectName: "fileTransferWindow"
    visible: true
    width: minimumWidth
    height: minimumHeight
    minimumWidth: 800
    minimumHeight: 580
    title: qsTr("File transfer") + " · " + hostName
    property bool darkTheme: transferDark
    readonly property color textColor: darkTheme ? "#E8E8E8" : "#182536"
    readonly property color secondaryColor: darkTheme ? "#A0A0A0" : "#6C7C8F"
    readonly property color cardColor: darkTheme ? "#202020" : "#FFFFFF"
    readonly property color borderColor: darkTheme ? "#373737" : "#E0E7EF"
    readonly property color accentColor: darkTheme ? "#68B5FF" : "#1677FF"
    readonly property color hoverColor: darkTheme ? "#292929" : "#F2F7FC"
    readonly property color selectionColor: darkTheme ? "#253548" : "#E9F3FF"
    color: darkTheme ? "#171717" : "#F4F7FB"
    Material.theme: darkTheme ? Material.Dark : Material.Light
    Material.accent: accentColor
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
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Image { source: "qrc:/res/file-transfer.svg"; sourceSize.width: 26; sourceSize.height: 26 }
            Label { textFormat: Text.PlainText; text: qsTr("File transfer"); font.pixelSize: 22; font.bold: true; color: window.textColor }
            Item { Layout.fillWidth: true }
            Label { textFormat: Text.PlainText; text: qsTr("Speed limit"); color: window.secondaryColor; font.pixelSize: 12 }
            ComboBox {
                model: ["1 MB/s", "4 MB/s", "10 MB/s", qsTr("Unlimited")]
                currentIndex: 1
                onActivated: transfer.limitMiB = [1, 4, 10, 0][currentIndex]
                implicitWidth: 130
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            FileTransferPane { Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; onNewFolderRequested: function(remote) { folderDialog.remote = remote; folderName.clear(); folderDialog.open(); } }
            FileTransferPane { remote: true; Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1; onNewFolderRequested: function(remote) { folderDialog.remote = remote; folderName.clear(); folderDialog.open(); } }
        }
        Label { textFormat: Text.PlainText; text: qsTr("Drag between panels to copy. Source files are kept. Closing this window lets transfers continue."); color: window.secondaryColor; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label { textFormat: Text.PlainText; visible: transfer.error.length > 0; text: transfer.error; color: darkTheme ? "#FFAAAA" : "#B53838"; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(162, Math.max(90, window.height * 0.20))
            radius: 10
            color: window.cardColor
            border.color: window.borderColor
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4
                RowLayout {
                    Label { textFormat: Text.PlainText; text: qsTr("Transfer queue"); color: window.textColor; font.bold: true; Layout.fillWidth: true }
                    ToolButton { text: qsTr("Clear finished"); onClicked: transfer.clearFinished(); implicitHeight: 28 }
                }
                ListView {
                    id: queue
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: transfer.jobs
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ColumnLayout {
                        width: queue.width - 12
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Label { textFormat: Text.PlainText; text: (modelData.upload ? "↑ " : "↓ ") + modelData.name; color: window.textColor; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 12 }
                            Label { textFormat: Text.PlainText; text: modelData.committing && modelData.state === "running" ? qsTr("Finishing…") : modelData.native && modelData.state === "done" ? qsTr("Data delivered") : window.statusName(modelData.state); color: window.secondaryColor; font.pixelSize: 11 }
                            Label { textFormat: Text.PlainText; visible: modelData.state === "running"; text: window.formatSize(modelData.done) + " / " + window.formatSize(modelData.size) + (modelData.native ? "" : " · " + window.formatSize(Math.round(modelData.speed)) + "/s"); color: window.secondaryColor; font.pixelSize: 11 }
                            ToolButton { text: qsTr("Cancel"); visible: modelData.state === "queued" || modelData.state === "running" || modelData.state === "conflict"; enabled: !modelData.committing; implicitHeight: 28; onClicked: transfer.cancel(index) }
                            ToolButton { text: qsTr("Retry"); visible: !modelData.native && (modelData.state === "failed" || modelData.state === "cancelled"); implicitHeight: 28; onClicked: transfer.retry(index) }
                        }
                        ProgressBar { visible: modelData.state === "running"; Layout.fillWidth: true; value: modelData.size > 0 ? modelData.done / modelData.size : 0; implicitHeight: 4 }
                        Label { textFormat: Text.PlainText; visible: modelData.detail.length > 0; text: modelData.detail; color: darkTheme ? "#FFAAAA" : "#B53838"; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11 }
                    }
                    Label { textFormat: Text.PlainText; anchors.centerIn: parent; visible: queue.count === 0; text: qsTr("Select files or folders to start a transfer"); color: window.secondaryColor; font.pixelSize: 12 }
                }
            }
        }
    }
    Dialog {
        id: conflictDialog
        title: qsTr("A file with this name already exists")
        anchors.centerIn: parent
        width: 460
        modal: true
        closePolicy: Popup.NoAutoClose
        ColumnLayout {
            anchors.fill: parent
            Label { textFormat: Text.PlainText; text: transfer.conflict; Layout.fillWidth: true; elide: Text.ElideMiddle; color: window.textColor }
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
        title: qsTr("New folder")
        anchors.centerIn: parent
        width: 340
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: folderName; width: parent.width; placeholderText: qsTr("Folder name"); selectByMouse: true }
        onOpened: folderName.forceActiveFocus()
        onAccepted: transfer.createFolder(remote, folderName.text)
    }
}
