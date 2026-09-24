import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.3

import AppModel 1.0
import ComputerManager 1.0
import SdlGamepadKeyNavigation 1.0

CenteredGridView {
    id: appGrid
    property int computerIndex
    property AppModel appModel: createModel()
    property bool activated
    property bool showHiddenGames
    property bool launchDesktop: false
    property int desktopTargetIndex: -1
    property bool showGames
    focus: true
    activeFocusOnTab: true
    topMargin: 12
    clip: true
    property bool listMode: true
    bottomMargin: 5
    cellWidth: listMode ? Math.max(1, width - 2 * minMargin) : 230
    cellHeight: listMode ? 88 : 326
    header: Rectangle {
        z: 3
        width: appGrid.width
        height: 68
        color: window.pageColor
        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            Label {
                text: qsTr("Choose an app to start or resume")
                color: window.secondaryColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
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
                text: qsTr("List")
                checked: appGrid.listMode
                onClicked: {
                    appGrid.listMode = true;
                    appGrid.positionViewAtBeginning();
                }
            }
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
                text: qsTr("Grid")
                checked: !appGrid.listMode
                onClicked: {
                    appGrid.listMode = false;
                    appGrid.positionViewAtBeginning();
                }
            }
        }
    }

    function resolveDesktop() {
        if (!activated || !launchDesktop)
            return;
        desktopTargetIndex = appModel.getDesktopAppIndex();
        if (desktopTargetIndex < 0)
            return;
        currentIndex = desktopTargetIndex;
        positionViewAtIndex(currentIndex, GridView.Contain);
        Qt.callLater(startDesktop);
    }

    function startDesktop() {
        if (!activated || !launchDesktop || desktopTargetIndex < 0 || currentIndex !== desktopTargetIndex || !currentItem)
            return;
        launchDesktop = false;
        desktopTimeout.stop();
        currentItem.launchOrResumeSelectedApp(true);
    }

    onCurrentItemChanged: Qt.callLater(startDesktop)
    onCountChanged: Qt.callLater(resolveDesktop)

    Timer {
        id: desktopTimeout
        interval: 6000
        onTriggered: {
            if (!appGrid.launchDesktop)
                return;
            appGrid.launchDesktop = false;
            desktopMissingDialog.open();
        }
    }
    ErrorMessageDialog {
        id: desktopMissingDialog
        objectName: "desktopMissingDialog"
        text: qsTr("This host does not provide a Desktop app. Add Desktop in Sunshine, then try again, or choose an app from the list.")
    }

    function computerLost() {
        // Go back to the PC view on PC loss
        stackView.pop();
    }

    Component.onCompleted: {
        // Don't show any highlighted item until interacting with them.
        // We do this here instead of onActivated to avoid losing the user's
        // selection when backing out of a different page of the app.
        currentIndex = -1;
    }

    StackView.onActivated: {
        appModel.computerLost.connect(computerLost);
        activated = true;

        // Highlight the first item if a gamepad is connected
        if (currentIndex === -1 && SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
            currentIndex = 0;
        }

        if (launchDesktop) {
            desktopTimeout.start();
            resolveDesktop();
        } else if (!showGames && !showHiddenGames) {
            // Check if there's a direct launch app
            var directLaunchAppIndex = model.getDirectLaunchAppIndex();
            if (directLaunchAppIndex >= 0) {
                // Start the direct launch app if nothing else is running
                currentIndex = directLaunchAppIndex;
                // Mark this before launching: returning from streaming must not relaunch.
                showGames = true;
                Qt.callLater(function () {
                    if (activated && currentItem)
                        currentItem.launchOrResumeSelectedApp(false);
                });
            }
        }
    }

    StackView.onDeactivating: {
        appModel.computerLost.disconnect(computerLost);
        activated = false;
        desktopTimeout.stop();
    }

    function createModel() {
        var model = Qt.createQmlObject('import AppModel 1.0; AppModel {}', appGrid, '');
        model.initialize(ComputerManager, computerIndex, showHiddenGames);
        return model;
    }

    model: appModel

    delegate: NavigableItemDelegate {
        id: appDelegate
        width: appGrid.cellWidth - 8
        height: appGrid.cellHeight - 8
        listNavigation: appGrid.listMode
        Accessible.role: Accessible.Button
        Accessible.name: model.name
        background: Rectangle {
            radius: 8
            color: appDelegate.highlighted ? window.selectionColor : window.surfaceColor
            border.color: appDelegate.highlighted ? window.accentColor : window.borderColor
            border.width: appDelegate.highlighted ? 2 : 1
        }
        grid: appGrid

        property alias appContextMenu: appContextMenuLoader.item

        // Dim the app if it's hidden
        opacity: model.hidden ? 0.4 : 1.0

        Image {
            id: appIcon
            x: appGrid.listMode ? 12 : (parent.width - width) / 2
            y: 10
            width: appGrid.listMode ? 44 : 174
            height: appGrid.listMode ? 58 : 222
            source: model.boxart
            fillMode: Image.PreserveAspectFit
            // Box art is optional; the name and actions remain usable if loading fails.
        }
        Rectangle {
            x: appIcon.x
            y: appIcon.y
            width: appIcon.width
            height: appIcon.height
            visible: appIcon.status !== Image.Ready
            color: window.selectionColor
            radius: 6
            Label {
                anchors.centerIn: parent
                text: model.name.slice(0, 1).toUpperCase()
                font.pixelSize: appGrid.listMode ? 24 : 48
                color: window.accentColor
            }
        }
        Label {
            x: appGrid.listMode ? 72 : 12
            y: appGrid.listMode ? 12 : 240
            width: appGrid.listMode ? Math.max(20, parent.width - 72 - appActions.width - 20) : parent.width - 24
            height: appGrid.listMode ? 24 : 30
            text: model.name
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            ToolTip.visible: appDelegate.hovered && truncated
            ToolTip.text: model.name
        }
        Label {
            x: 72
            y: 43
            width: Math.max(20, parent.width - 72 - appActions.width - 20)
            visible: appGrid.listMode
            text: model.running ? qsTr("Running") : model.hidden ? qsTr("Hidden") : model.directLaunch ? qsTr("Direct Launch") : qsTr("Ready to launch")
            color: model.running ? window.onlineColor : window.secondaryColor
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Row {
            id: appActions
            anchors.right: parent.right
            anchors.rightMargin: 8
            y: appGrid.listMode ? 16 : 272
            spacing: 4
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
                visible: model.running
                text: qsTr("Resume Game")
                onClicked: appDelegate.launchOrResumeSelectedApp(true)
            }
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
                visible: model.running && appGrid.listMode
                text: qsTr("Quit Game")
                onClicked: appDelegate.doQuitGame()
            }
            ToolButton {
                id: appMoreButton
                text: "⋯"
                Accessible.name: qsTr("App actions")
                onClicked: appContextMenu.openBelow(appMoreButton)
            }
        }

        function launchOrResumeSelectedApp(quitExistingApp) {
            var runningId = appModel.getRunningAppId();
            if (runningId !== 0 && runningId !== model.appid) {
                if (quitExistingApp) {
                    quitAppDialog.appName = appModel.getRunningAppName();
                    quitAppDialog.segueToStream = true;
                    quitAppDialog.nextAppName = model.name;
                    quitAppDialog.nextAppIndex = index;
                    quitAppDialog.open();
                }

                return;
            }

            var component = Qt.createComponent("StreamSegue.qml");
            var segue = component.createObject(stackView, {
                "appName": model.name,
                "session": appModel.createSessionForApp(index),
                "isResume": runningId === model.appid
            });
            stackView.push(segue);
        }

        onClicked: {
            appGrid.currentIndex = index;
            launchOrResumeSelectedApp(true);
        }

        onPressAndHold: {
            appContextMenu.initiator = appDelegate;
            appContextMenu.parent = appDelegate;
            // popup() ensures the menu appears under the mouse cursor
            if (appContextMenu.popup) {
                appContextMenu.popup();
            } else {
                // Qt 5.9 doesn't have popup()
                appContextMenu.openBelow(appMoreButton);
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: {
                parent.pressAndHold();
            }
        }

        Keys.onReturnPressed: launchOrResumeSelectedApp(true)
        Keys.onEnterPressed: launchOrResumeSelectedApp(true)

        Keys.onMenuPressed: {
            appContextMenu.openBelow(appMoreButton);
        }

        function doQuitGame() {
            quitAppDialog.appName = appModel.getRunningAppName();
            quitAppDialog.segueToStream = false;
            quitAppDialog.open();
        }

        Loader {
            id: appContextMenuLoader
            asynchronous: false
            sourceComponent: NavigableMenu {
                id: appContextMenu
                parent: appDelegate
                initiator: appContextMenuLoader.parent
                NavigableMenuItem {
                    text: model.running ? qsTr("Resume Game") : qsTr("Launch Game")
                    onTriggered: launchOrResumeSelectedApp(true)
                }
                NavigableMenuItem {
                    text: qsTr("Quit Game")
                    onTriggered: doQuitGame()
                    visible: model.running
                }
                NavigableMenuItem {
                    checkable: true
                    checked: model.directLaunch
                    text: qsTr("Direct Launch")
                    onTriggered: appModel.setAppDirectLaunch(model.index, !model.directLaunch)
                    enabled: !model.hidden

                    ToolTip.text: qsTr("Launch this app immediately when the host is selected, bypassing the app selection grid.")
                    ToolTip.delay: 1000
                    ToolTip.timeout: 3000
                    ToolTip.visible: hovered
                }
                NavigableMenuItem {
                    checkable: true
                    checked: model.hidden
                    text: qsTr("Hide Game")
                    onTriggered: appModel.setAppHidden(model.index, !model.hidden)
                    enabled: model.hidden || (!model.running && !model.directLaunch)

                    ToolTip.text: qsTr("Hide this game from the app grid. To access hidden games, right-click on the host and choose %1.").arg(qsTr("View All Apps"))
                    ToolTip.delay: 1000
                    ToolTip.timeout: 5000
                    ToolTip.visible: hovered
                }
            }
        }
    }

    Row {
        anchors.centerIn: parent
        width: parent.width - 48
        spacing: 5
        visible: appGrid.count === 0

        Label {
            text: qsTr("This computer doesn't seem to have any applications or some applications are hidden")
            width: parent.width
            font.pixelSize: 18
            color: window.secondaryColor
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    ConnectionStatus {
        parent: appGrid
        anchors.fill: parent
        z: 4
        visible: appGrid.launchDesktop
        text: qsTr("Finding Desktop…")
        MouseArea {
            anchors.fill: parent
        }
    }

    NavigableMessageDialog {
        id: quitAppDialog
        objectName: "quitAppDialog"
        property string appName: ""
        property bool segueToStream: false
        property string nextAppName: ""
        property int nextAppIndex: 0
        text: qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml");
            var params = {
                "appName": appName,
                "quitRunningAppFn": function () {
                    appModel.quitRunningApp();
                }
            };
            if (segueToStream) {
                // Store the session and app name if we're going to stream after
                // successfully quitting the old app.
                params.nextAppName = nextAppName;
                params.nextSession = appModel.createSessionForApp(nextAppIndex);
            } else {
                params.nextAppName = null;
                params.nextSession = null;
            }

            stackView.push(component.createObject(stackView, params));
        }

        onAccepted: quitApp()
    }

    ScrollBar.vertical: ScrollBar {}
}
