import QtQuick 2.12
import QtQuick.Controls 2.13
import QtQuick.Layouts 1.12

Rectangle {
    id: pane
    objectName: remote ? "remoteFilePane" : "localFilePane"
    property bool remote: false
    property var entries: remote ? transfer.remoteEntries : transfer.localEntries
    property string currentPath: remote ? transfer.remotePath : transfer.localPath
    property string displayPath: remote ? transfer.remoteDisplayPath : transfer.localPath
    property var places: remote ? transfer.remotePlaces : transfer.localPlaces
    property var selected: []
    property int selectionAnchor: -1
    property var dragNames: []
    property string dragSourcePath: ""
    property string dropDirectory: currentPath
    property string dropTargetName: ""
    property var contextEntry: null
    signal newFolderRequested(bool remote)
    signal folderRequested(bool remote)
    signal transferRequested(bool sourceRemote, var names, string sourcePath)
    color: window.cardColor
    radius: 10
    border.color: window.borderColor
    onEntriesChanged: Qt.callLater(pane.reconcileSelection)
    onCurrentPathChanged: clearSelection()
    Component.onCompleted: pathField.text = pane.displayPath

    Connections {
        target: transfer
        function onLocalBrowseSucceeded() { if (!pane.remote) pathField.text = pane.displayPath; }
        function onRemoteBrowseSucceeded() { if (pane.remote) pathField.text = pane.displayPath; }
    }

    function clearSelection() { selected = []; selectionAnchor = -1; }
    function reconcileSelection() {
        var names = [];
        for (var i = 0; i < entries.length; ++i)
            if (selected.indexOf(entries[i].name) >= 0) names.push(entries[i].name);
        if (names.join("\u0000") !== selected.join("\u0000")) selected = names;
        if (selectionAnchor >= entries.length) selectionAnchor = -1;
    }
    function toggle(name) {
        var copy = selected.slice();
        var i = copy.indexOf(name);
        if (i < 0) copy.push(name); else copy.splice(i, 1);
        selected = copy;
    }
    function selectIndex(index, modifiers) {
        if (index < 0 || index >= entries.length) return;
        var additive = modifiers & (Qt.ControlModifier | Qt.MetaModifier);
        if ((modifiers & Qt.ShiftModifier) && selectionAnchor >= 0) {
            var names = additive ? selected.slice() : [];
            for (var i = Math.min(selectionAnchor, index); i <= Math.max(selectionAnchor, index); ++i)
                if (names.indexOf(entries[i].name) < 0) names.push(entries[i].name);
            selected = names;
        } else {
            if (additive) toggle(entries[index].name); else selected = [entries[index].name];
            selectionAnchor = index;
        }
        files.currentIndex = index;
    }
    function selectAll() {
        var names = [];
        for (var i = 0; i < entries.length; ++i) names.push(entries[i].name);
        selected = names;
    }
    function entryPath(entry) {
        if (entry.path !== undefined && entry.path.length) return entry.path;
        return currentPath.length ? currentPath.replace(/\/+$/, "") + "/" + entry.name : entry.name;
    }
    function browse(path) { if (remote) transfer.browseRemote(path); else transfer.browseLocal(path); }
    function openEntry(entry) { if (entry && entry.dir) browse(entryPath(entry)); }
    function requestTransfer() {
        if (selected.length) transferRequested(remote, selected.slice(), currentPath);
    }
    function dropAllowed(drag) {
        return transfer.ready && !transfer.remoteLoading && (drag.supportedActions & Qt.CopyAction)
            && ((drag.source && drag.source !== pane && drag.source.remote !== undefined
                 && drag.source.remote !== pane.remote) || (pane.remote && drag.hasUrls));
    }
    function updateDropTarget(drag) {
        var point = pane.mapToItem(files, drag.x, drag.y);
        var i = point.y >= 0 && point.y < files.height
              ? files.indexAt(point.x + files.contentX, point.y + files.contentY) : -1;
        var entry = i >= 0 ? entries[i] : null;
        dropTargetName = entry && entry.dir ? entry.name : "";
        dropDirectory = entry && entry.dir ? entryPath(entry) : currentPath;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle { width: 7; height: 7; radius: 4; color: pane.remote ? (transfer.ready ? "#35A879" : window.secondaryColor) : window.accentColor }
            Label {
                textFormat: Text.PlainText
                text: pane.remote ? qsTr("Remote") + " · " + hostName : qsTr("This computer")
                font.pixelSize: 16
                font.bold: true
                color: window.textColor
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            BusyIndicator { running: pane.remote && transfer.remoteLoading; visible: running; implicitWidth: 20; implicitHeight: 20 }
            ToolButton {
                implicitWidth: 32; implicitHeight: 30
                text: "↻"
                Accessible.name: qsTr("Refresh")
                ToolTip.visible: hovered; ToolTip.text: qsTr("Refresh")
                enabled: !pane.remote || !transfer.remoteLoading
                onClicked: pane.browse(pane.currentPath)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToolButton {
                implicitWidth: 32; implicitHeight: 32
                text: "↑"
                Accessible.name: qsTr("Parent directory")
                ToolTip.visible: hovered; ToolTip.text: qsTr("Parent directory")
                enabled: !pane.remote || (pane.currentPath.length > 0 && (!transfer.remoteFilesystem || transfer.remoteParentPath.length > 0))
                onClicked: transfer.parentDirectory(pane.remote)
            }
            ToolButton {
                implicitWidth: 32; implicitHeight: 32
                text: "⌂"
                Accessible.name: qsTr("Home directory")
                ToolTip.visible: hovered; ToolTip.text: qsTr("Home directory")
                onClicked: pane.remote ? transfer.browseRemote(transfer.remoteDefaultPath) : transfer.home()
            }
            ComboBox {
                objectName: pane.remote ? "remotePlaces" : "localPlaces"
                Layout.fillWidth: true
                implicitHeight: 34
                model: pane.places
                textRole: "label"
                displayText: qsTr("Disks & locations")
                onActivated: pane.browse(pane.places[currentIndex].path)
            }
            ToolButton {
                objectName: pane.remote ? "remoteChooseFolderButton" : "localChooseFolderButton"
                text: qsTr("Choose…")
                implicitHeight: 34
                enabled: !pane.remote || transfer.ready
                Accessible.name: qsTr("Choose folder")
                onClicked: pane.folderRequested(pane.remote)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: pathField
                objectName: pane.remote ? "remotePathField" : "localPathField"
                implicitHeight: 36
                Layout.fillWidth: true
                selectByMouse: true
                font.pixelSize: 12
                Accessible.name: qsTr("Folder path")
                ToolTip.visible: hovered && !activeFocus
                ToolTip.text: text
                onAccepted: pane.browse(text)
            }
            ToolButton {
                text: "→"; implicitWidth: 32; implicitHeight: 34
                Accessible.name: qsTr("Go to folder")
                ToolTip.visible: hovered; ToolTip.text: qsTr("Go to folder")
                onClicked: pane.browse(pathField.text)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: 24
            spacing: 6
            Label {
                readonly property bool restricted: pane.remote && transfer.ready && !transfer.remoteFilesystem
                Layout.fillWidth: true
                text: restricted ? qsTr("Shared folder only") : qsTr("Double-click folders to open · drag files to copy")
                color: restricted ? window.accentColor : window.secondaryColor
                font.pixelSize: 11
                font.bold: restricted
                elide: Text.ElideRight
                ToolTip.visible: scopeHint.containsMouse
                ToolTip.text: text
                MouseArea { id: scopeHint; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
            ToolButton {
                objectName: "scopeHelpButton"
                visible: pane.remote && transfer.ready && !transfer.remoteFilesystem
                text: qsTr("Other folders…")
                implicitHeight: 24
                font.pixelSize: 11
                onClicked: window.showScopeHelp()
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: window.borderColor }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 7
            Layout.rightMargin: 14
            CheckBox {
                implicitWidth: 36; implicitHeight: 24
                checked: pane.entries.length > 0 && pane.selected.length === pane.entries.length
                enabled: !pane.remote || !transfer.remoteLoading
                Accessible.name: qsTr("Select all")
                onClicked: checked ? pane.selectAll() : pane.clearSelection()
            }
            Label { text: qsTr("Name"); color: window.secondaryColor; font.pixelSize: 11; Layout.leftMargin: 30; Layout.fillWidth: true }
            Label { text: qsTr("Size"); color: window.secondaryColor; font.pixelSize: 11 }
        }
        ListView {
            id: files
            objectName: pane.remote ? "remoteFileList" : "localFileList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 100
            clip: true
            model: pane.entries
            enabled: !pane.remote || !transfer.remoteLoading
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                objectName: (pane.remote ? "remoteFileRow" : "localFileRow") + index
                width: files.width
                height: Math.max(48, fileNameLabel.implicitHeight + 16)
                radius: 5
                color: pane.dropTargetName === modelData.name && paneDrop.containsDrag ? window.selectionColor
                     : pane.selected.indexOf(modelData.name) >= 0 ? window.selectionColor
                     : mouse.containsMouse ? window.hoverColor : "transparent"
                border.width: paneDrop.containsDrag && pane.dropTargetName === modelData.name ? 1 : 0
                border.color: window.accentColor
                RowLayout {
                    id: rowContent
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 14
                    spacing: 8
                    CheckBox {
                        id: selectionBox
                        objectName: "fileCheckBox"
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        checked: pane.selected.indexOf(modelData.name) >= 0
                        enabled: !pane.remote || !transfer.remoteLoading
                        onClicked: { pane.toggle(modelData.name); pane.selectionAnchor = index; files.currentIndex = index; }
                        Accessible.name: modelData.name
                    }
                    Image {
                        source: modelData.dir ? "qrc:/res/transfer-folder.svg" : "qrc:/res/transfer-file.svg"
                        sourceSize.width: 22; sourceSize.height: 22
                        Layout.preferredWidth: 22; Layout.preferredHeight: 22
                    }
                    Label {
                        id: fileNameLabel
                        objectName: "fileNameLabel"
                        textFormat: Text.PlainText
                        text: modelData.name
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        color: window.textColor
                        font.pixelSize: 13
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: modelData.dir ? "" : window.formatSize(modelData.size)
                        color: window.secondaryColor
                        font.pixelSize: 11
                    }
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    anchors.leftMargin: rowContent.x + selectionBox.x + selectionBox.width
                    hoverEnabled: true
                    enabled: !pane.remote || !transfer.remoteLoading
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    preventStealing: true
                    drag.target: dragPreview
                    drag.threshold: 8
                    property bool systemDragStarted: false
                    ToolTip.visible: containsMouse && !pressed && !dragPreview.Drag.active
                    ToolTip.delay: 450
                    ToolTip.text: modelData.name
                    onPressed: function(event) {
                        files.forceActiveFocus(Qt.MouseFocusReason);
                        files.currentIndex = index;
                        systemDragStarted = false;
                        if (event.button === Qt.RightButton) {
                            if (pane.selected.indexOf(modelData.name) < 0) pane.selectIndex(index, Qt.NoModifier);
                        } else if (!(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier))
                                   && pane.selected.indexOf(modelData.name) < 0) {
                            pane.selectIndex(index, Qt.NoModifier);
                        }
                        pane.dragNames = pane.selected.slice();
                        pane.dragSourcePath = pane.currentPath;
                        var point = mapToItem(pane, event.x, event.y);
                        dragPreview.x = point.x; dragPreview.y = point.y;
                    }
                    onPositionChanged: function(event) {
                        if (!drag.active || systemDragStarted) return;
                        var position = mapToItem(window.contentItem, event.x, event.y);
                        if (position.x < 0 || position.y < 0 || position.x > window.width || position.y > window.height) {
                            systemDragStarted = true;
                            dragPreview.Drag.cancel();
                            transfer.systemDrag(pane.remote, pane.dragNames, window);
                        }
                    }
                    onClicked: function(event) {
                        if (event.button === Qt.RightButton) {
                            pane.contextEntry = modelData;
                            entryMenu.popup();
                        } else {
                            pane.selectIndex(index, event.modifiers);
                        }
                    }
                    onDoubleClicked: function(event) { if (event.button === Qt.LeftButton) pane.openEntry(modelData); }
                    onReleased: { if (dragPreview.Drag.active) dragPreview.Drag.drop(); }
                    drag.onActiveChanged: dragPreview.Drag.active = drag.active && !systemDragStarted
                }
            }
            Label {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                visible: files.count === 0
                text: pane.remote && (transfer.remoteLoading || !transfer.ready) ? qsTr("Loading folders…") : qsTr("This folder is empty")
                color: window.secondaryColor
                font.pixelSize: 12
            }
            Keys.onPressed: function(event) {
                if (pane.remote && event.matches(StandardKey.Paste)) {
                    transfer.pasteFiles(); event.accepted = true;
                } else if ((event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) && event.key === Qt.Key_A) {
                    pane.selectAll(); event.accepted = true;
                } else if (event.key === Qt.Key_Escape) {
                    pane.clearSelection(); event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    if (files.currentIndex >= 0) pane.openEntry(pane.entries[files.currentIndex]);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    var next = Math.max(0, Math.min(pane.entries.length - 1, files.currentIndex + (event.key === Qt.Key_Down ? 1 : -1)));
                    pane.selectIndex(next, event.modifiers & Qt.ShiftModifier);
                    files.positionViewAtIndex(next, ListView.Contain);
                    event.accepted = true;
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: window.borderColor }
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: pane.remote ? qsTr("Download to: %1").arg(transfer.localPath)
                                  : qsTr("Upload to: %1").arg(transfer.remoteDisplayPath || "—")
                color: window.secondaryColor
                font.pixelSize: 11
                elide: Text.ElideMiddle
                ToolTip.visible: destinationHint.containsMouse
                ToolTip.text: text
                MouseArea { id: destinationHint; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
            ToolButton { text: qsTr("Change…"); implicitHeight: 26; onClicked: pane.folderRequested(!pane.remote) }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToolButton { text: qsTr("New folder"); implicitHeight: 34; enabled: !pane.remote || transfer.ready; onClicked: pane.newFolderRequested(pane.remote) }
            Label {
                text: pane.selected.length ? qsTr("%1 selected").arg(pane.selected.length) : qsTr("%1 items").arg(files.count)
                color: window.secondaryColor
                font.pixelSize: 11
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            ActionButton {
                objectName: "transferSelectionButton"
                primary: true
                implicitHeight: 36
                implicitWidth: 120
                font.pixelSize: 13
                text: pane.remote ? qsTr("← Download") : qsTr("Upload →")
                enabled: transfer.ready && !transfer.remoteLoading && pane.selected.length > 0
                onClicked: pane.requestTransfer()
            }
        }
    }
    Menu {
        id: entryMenu
        MenuItem { text: qsTr("Open folder"); enabled: pane.contextEntry && pane.contextEntry.dir; onTriggered: pane.openEntry(pane.contextEntry) }
        MenuItem { text: pane.remote ? qsTr("Download to…") : qsTr("Upload to…"); enabled: transfer.ready && !transfer.remoteLoading && pane.selected.length > 0; onTriggered: pane.requestTransfer() }
        MenuSeparator {}
        MenuItem { text: qsTr("Select all"); onTriggered: pane.selectAll() }
        MenuItem { text: qsTr("Clear selection"); enabled: pane.selected.length > 0; onTriggered: pane.clearSelection() }
    }
    DropArea {
        id: paneDrop
        anchors.fill: parent
        onEntered: function(drag) { drag.accepted = pane.dropAllowed(drag); if (drag.accepted) pane.updateDropTarget(drag); }
        onPositionChanged: function(drag) { pane.updateDropTarget(drag); }
        onExited: { pane.dropTargetName = ""; pane.dropDirectory = pane.currentPath; }
        onDropped: function(drop) {
            var destination = pane.dropDirectory;
            if (drop.source && drop.source !== pane && drop.source.dragNames !== undefined) {
                transfer.enqueueTo(pane.remote, drop.source.dragNames, drop.source.dragSourcePath, destination);
                drop.accept(Qt.CopyAction);
                window.queueExpanded = true;
            } else if (pane.remote && drop.hasUrls) {
                transfer.uploadFilesTo(drop.urls, destination);
                drop.accept(Qt.CopyAction);
                window.queueExpanded = true;
            }
            pane.dropTargetName = "";
        }
        Rectangle { anchors.fill: parent; color: "transparent"; radius: 10; border.width: 2; border.color: window.accentColor; visible: paneDrop.containsDrag }
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 8
            height: 38; radius: 6; color: window.accentColor; visible: paneDrop.containsDrag
            Label {
                anchors.fill: parent; anchors.margins: 9
                textFormat: Text.PlainText
                text: qsTr("Copy to: %1").arg(pane.remote ? transfer.displayRemotePath(pane.dropDirectory) : pane.dropDirectory)
                color: "white"; elide: Text.ElideMiddle; font.pixelSize: 12
            }
        }
    }
    Rectangle {
        id: dragPreview
        width: 132; height: 32; radius: 6; z: 100
        color: window.accentColor
        visible: Drag.active
        Drag.source: pane
        Drag.keys: ["desk-files"]
        Drag.supportedActions: Qt.CopyAction
        Drag.hotSpot.x: 12
        Drag.hotSpot.y: 12
        Label { textFormat: Text.PlainText; anchors.centerIn: parent; text: qsTr("%1 selected").arg(pane.dragNames.length); color: "white" }
    }
}
