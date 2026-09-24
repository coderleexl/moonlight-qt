import QtQuick 2.0
import QtQuick.Controls 2.2

Menu {
    property var initiator

    function openBelow(item) {
        initiator = item
        parent = item
        x = item.width - width
        y = item.height
        open()
    }

    onClosed: {
        if (initiator && initiator.visible && initiator.enabled) initiator.forceActiveFocus(Qt.OtherFocusReason)
    }

    onOpened: {
        // If the initiating object currently has keyboard focus,
        // give focus to the first visible and enabled menu item
        if (initiator && (initiator.focus || initiator.activeFocus)) {
            for (var i = 0; i < count; i++) {
                var item = itemAt(i)
                if (item.visible && item.enabled) {
                    item.forceActiveFocus(Qt.TabFocusReason)
                    break
                }
            }
        }
    }
}
