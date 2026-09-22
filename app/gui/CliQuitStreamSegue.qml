import QtQuick 2.0
import QtQuick.Controls 2.2

import ComputerManager 1.0
import Session 1.0

Item {
    property string stageText: qsTr("Establishing connection to PC...")
    function onSearchingComputer() {
        connectionStatus.text = qsTr("Establishing connection to PC...");
    }

    function onQuittingApp() {
        connectionStatus.text = qsTr("Quitting app...");
    }

    function onFailure(message) {
        errorDialog.text = message;
        errorDialog.open();
    }

    StackView.onActivated: {
        if (!launcher.isExecuted()) {
            toolBar.visible = false;
            launcher.searchingComputer.connect(onSearchingComputer);
            launcher.quittingApp.connect(onQuittingApp);
            launcher.failed.connect(onFailure);
            launcher.execute(ComputerManager);
        }
    }

    ConnectionStatus {
        id: connectionStatus
        anchors.fill: parent
        text: stageText
        busy: true
    }

    ErrorMessageDialog {
        id: errorDialog

        onClosed: {
            Qt.quit();
        }
    }
}
