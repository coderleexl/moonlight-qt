import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Rectangle {
    id: pane
    objectName: remote ? "remoteFilePane" : "localFilePane"
    property bool remote: false
    property var entries: remote ? transfer.remoteEntries : transfer.localEntries
    property string currentPath: remote ? transfer.remotePath : transfer.localPath
    property var selected: []
    signal newFolderRequested(bool remote)
    color: window.cardColor
    radius: 10
    border.color: window.borderColor
    onEntriesChanged: selected = []
    function toggle(name) {
        var copy = selected.slice(); var i = copy.indexOf(name);
        if (i < 0) copy.push(name); else copy.splice(i, 1);
        selected = copy;
    }
    function openEntry(entry) {
        if (!entry.dir) return;
        var path = currentPath.length ? currentPath + "/" + entry.name : entry.name;
        if (remote) transfer.browseRemote(path); else transfer.browseLocal(path);
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label { textFormat: Text.PlainText; text: pane.remote ? qsTr("Remote") + " · " + hostName : qsTr("This computer"); font.pixelSize: 17; font.bold: true; color: window.textColor; Layout.fillWidth: true; elide: Text.ElideRight }
            ToolButton { text: "↻"; Accessible.name: qsTr("Refresh"); onClicked: pane.remote ? transfer.browseRemote(pane.currentPath) : transfer.browseLocal(pane.currentPath) }
        }
        RowLayout {
            Layout.fillWidth: true
            ToolButton { text: "↑"; Accessible.name: qsTr("Parent directory"); enabled: !pane.remote || pane.currentPath.length > 0; onClicked: transfer.parentDirectory(pane.remote) }
            TextField {
                id: pathField
                Layout.fillWidth: true
                text: pane.remote ? "/" + pane.currentPath : pane.currentPath
                selectByMouse: true
                font.pixelSize: 12
                onTextChanged: cursorPosition = 0
                onAccepted: pane.remote ? transfer.browseRemote(text.replace(/^\/+/, "")) : transfer.browseLocal(text)
            }
            ToolButton { text: "⌂"; Accessible.name: qsTr("Home directory"); onClicked: pane.remote ? transfer.browseRemote("") : transfer.home() }
        }
        Label { textFormat: Text.PlainText;
            Layout.fillWidth: true
            text: pane.remote ? qsTr("Shared folder: %1").arg(transfer.remoteRoot || "—") : qsTr("Select files to upload to the folder on the right")
            elide: Text.ElideMiddle
            font.pixelSize: 11
            color: window.secondaryColor
            ToolTip.visible: rootHint.containsMouse
            ToolTip.text: text
            MouseArea { id: rootHint; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: window.borderColor }
        ListView {
            id: files
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: pane.entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                objectName: (pane.remote ? "remoteFileRow" : "localFileRow") + index
                width: files.width
                height: 42
                radius: 5
                color: pane.selected.indexOf(modelData.name) >= 0 ? window.selectionColor : mouse.containsMouse ? window.hoverColor : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 12
                    spacing: 9
                    CheckBox { checked: pane.selected.indexOf(modelData.name) >= 0; onClicked: pane.toggle(modelData.name); Accessible.name: modelData.name }
                    Image { source: modelData.dir ? "qrc:/res/transfer-folder.svg" : "qrc:/res/transfer-file.svg"; sourceSize.width: 22; sourceSize.height: 22; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
                    Label { textFormat: Text.PlainText; text: modelData.name; Layout.fillWidth: true; elide: Text.ElideMiddle; color: window.textColor; font.pixelSize: 13 }
                    Label { textFormat: Text.PlainText; text: modelData.dir ? "" : window.formatSize(modelData.size); color: window.secondaryColor; font.pixelSize: 11 }
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    anchors.leftMargin: 44
                    hoverEnabled: true
                    drag.target: dragPreview
                    drag.threshold: 8
                    onPressed: function(mouse) {
                        if (!(mouse.modifiers & Qt.ControlModifier) && !(mouse.modifiers & Qt.MetaModifier) && pane.selected.indexOf(modelData.name) < 0) pane.selected = [modelData.name];
                        var point = mapToItem(pane, mouseX, mouseY);
                        dragPreview.x = point.x; dragPreview.y = point.y;
                    }
                    onClicked: function(mouse) { if (mouse.modifiers & Qt.ControlModifier || mouse.modifiers & Qt.MetaModifier) pane.toggle(modelData.name); }
                    onDoubleClicked: pane.openEntry(modelData)
                    onReleased: { if (dragPreview.Drag.active) dragPreview.Drag.drop(); }
                    drag.onActiveChanged: dragPreview.Drag.active = drag.active
                }
            }
            Label { textFormat: Text.PlainText; anchors.centerIn: parent; visible: files.count === 0; text: pane.remote && !transfer.ready ? qsTr("Connecting…") : qsTr("This folder is empty"); color: window.secondaryColor }
            Keys.onPressed: function(event) {
                if ((event.modifiers & Qt.ControlModifier || event.modifiers & Qt.MetaModifier) && event.key === Qt.Key_A) {
                    var names = []; for (var i = 0; i < pane.entries.length; ++i) names.push(pane.entries[i].name); pane.selected = names; event.accepted = true;
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ToolButton { text: qsTr("New folder"); enabled: !pane.remote || transfer.ready; onClicked: pane.newFolderRequested(pane.remote) }
            Item { Layout.fillWidth: true }
            ActionButton {
                primary: true
                text: pane.remote ? qsTr("← Download") : qsTr("Upload →")
                enabled: transfer.ready && pane.selected.length > 0
                onClicked: transfer.enqueue(!pane.remote, pane.selected)
            }
        }
    }
    DropArea {
        anchors.fill: parent
        keys: ["desk-files"]
        onEntered: function(drag) { drag.accepted = drag.source && drag.source !== pane && transfer.ready; }
        onDropped: function(drop) {
            if (drop.source && drop.source !== pane) { transfer.enqueue(pane.remote, drop.source.selected); drop.acceptProposedAction(); }
        }
        Rectangle { anchors.fill: parent; color: "transparent"; radius: 10; border.width: 2; border.color: window.accentColor; visible: parent.containsDrag }
    }
    Rectangle {
        id: dragPreview
        width: 120; height: 30; radius: 6; z: 100
        color: window.accentColor
        visible: Drag.active
        Drag.source: pane
        Drag.keys: ["desk-files"]
        Drag.supportedActions: Qt.CopyAction
        Drag.hotSpot.x: 12
        Drag.hotSpot.y: 12
        Label { textFormat: Text.PlainText; anchors.centerIn: parent; text: qsTr("%1 selected").arg(pane.selected.length); color: "white" }
    }
}
