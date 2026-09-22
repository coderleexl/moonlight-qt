import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Controls.Material 2.2

ToolButton {
    id: navigableButton
    property bool navigationItem: false
    property string iconSource

    activeFocusOnTab: true

    Material.foreground: checked ? window.accentColor : window.textColor

    icon.source: iconSource
    icon.width: 20
    icon.height: 20
    icon.color: checked ? window.accentColor : window.textColor
    implicitHeight: 44
    leftPadding: 16
    rightPadding: 16
    spacing: 12
    font.pixelSize: 14

    // Material's IconLabel otherwise centers the icon/text pair as a unit.
    // Align the content box so every navigation label starts after the same icon slot.
    Binding {
        target: navigableButton.contentItem
        property: "alignment"
        value: navigableButton.navigationItem && navigableButton.display !== AbstractButton.IconOnly ? Qt.AlignLeft | Qt.AlignVCenter : Qt.AlignCenter
    }
    Accessible.name: text || ToolTip.text
    ToolTip.visible: hovered && display === AbstractButton.IconOnly
    ToolTip.delay: 700
    ToolTip.text: text

    background: Rectangle {
        radius: 8
        color: parent.checked ? window.selectionColor : parent.hovered ? window.hoverColor : "transparent"
        border.width: parent.activeFocus ? 2 : 0
        border.color: window.accentColor
    }

    Layout.preferredHeight: 44

    Keys.onReturnPressed: {
        clicked();
    }

    Keys.onEnterPressed: {
        clicked();
    }

    Keys.onRightPressed: {
        nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocus);
    }

    Keys.onLeftPressed: {
        nextItemInFocusChain(false).forceActiveFocus(Qt.TabFocus);
    }
}
