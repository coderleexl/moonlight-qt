import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import "../../app/gui" as Desk

TestCase {
    id: testCase
    name: "AppMenuPosition"
    width: 900
    height: 700
    visible: true
    when: windowShown

    GridView {
        id: view
        x: 40
        y: 80
        width: 800
        height: 540
        property bool listMode: true
        cellWidth: listMode ? 800 : 200
        cellHeight: listMode ? 80 : 180
        model: 32
        delegate: Item {
            width: view.cellWidth
            height: view.cellHeight
            property alias button: more
            property alias menu: menuLoader.item
            ToolButton {
                id: more
                anchors.right: parent.right
                anchors.rightMargin: 12
                y: 20
                text: "⋯"
                onClicked: menuLoader.item.openBelow(more)
                Keys.onMenuPressed: menuLoader.item.openBelow(more)
            }
            Loader {
                id: menuLoader
                sourceComponent: Desk.NavigableMenu {
                    width: 180
                    MenuItem { text: "Launch" }
                    MenuItem { text: "Hide" }
                }
            }
        }
    }

    function test_position_data() {
        return [
            { tag: "list-second", list: true, index: 1, scroll: false },
            { tag: "list-scrolled", list: true, index: 16, scroll: true },
            { tag: "grid-second", list: false, index: 1, scroll: false },
            { tag: "grid-scrolled", list: false, index: 20, scroll: true }
        ];
    }

    function checkPosition(menu, button) {
        tryCompare(menu, "opened", true);
        var expected = button.mapToItem(testCase, button.width, button.height);
        var actual = menu.contentItem.mapToItem(testCase, 0, 0);
        fuzzyCompare(actual.x - menu.leftPadding + menu.width, expected.x, 2);
        fuzzyCompare(actual.y - menu.topPadding, expected.y, 2);
    }

    function test_position(data) {
        view.listMode = data.list;
        view.forceLayout();
        view.positionViewAtBeginning();
        if (data.scroll)
            view.positionViewAtIndex(data.index, GridView.Beginning);
        view.forceLayout();
        tryVerify(function() { return view.itemAtIndex(data.index) !== null; });
        var delegate = view.itemAtIndex(data.index);
        var menu = delegate.menu;
        tryVerify(function() { return delegate.button.visible; });
        mouseClick(delegate.button);
        checkPosition(menu, delegate.button);
        menu.close();
        tryCompare(menu, "visible", false);

        // A cursor-positioned context menu must not change the next button popup.
        menu.parent = delegate;
        menu.popup(5, 5);
        tryCompare(menu, "opened", true);
        menu.close();
        tryCompare(menu, "visible", false);
        delegate.button.forceActiveFocus();
        keyClick(Qt.Key_Menu);
        checkPosition(menu, delegate.button);
        menu.close();
        tryCompare(menu, "visible", false);
        tryCompare(delegate.button, "activeFocus", true);
    }
}
