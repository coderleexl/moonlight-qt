import QtQuick 2.9
import QtQuick.Controls 2.2

// All attached ToolTips share one popup in a QML engine. Each application
// window installs this theme, including the independent file transfer process.
Item {
    id: theme
    visible: false
    property bool darkTheme: false
    property real maximumWidth: 400
    readonly property var tip: ToolTip.toolTip
    readonly property real anchorY: {
        if (!tip.parent) return 0;
        // Observe ancestor movement as well as changes to the hovered control.
        var position = 0;
        for (var item = tip.parent; item; item = item.parent) position += item.y;
        return tip.parent.mapToItem(null, 0, 0).y;
    }
    readonly property bool below: tip.parent !== null && anchorY < tip.implicitHeight + 14

    ToolTip.toolTip.contentWidth: Math.max(1, Math.min(label.implicitWidth, maximumWidth))
    // Keep height independent of pointer direction to avoid a placement binding loop.
    ToolTip.toolTip.implicitHeight: Math.ceil(label.implicitHeight) + 23
    ToolTip.toolTip.horizontalPadding: 12
    ToolTip.toolTip.topPadding: below ? 15 : 8
    ToolTip.toolTip.bottomPadding: below ? 8 : 15
    ToolTip.toolTip.margins: 8
    ToolTip.toolTip.y: below && tip.parent ? tip.parent.height + 6 : -tip.implicitHeight - 6
    ToolTip.toolTip.contentItem: Text {
        id: label
        text: theme.tip.text
        textFormat: Text.PlainText
        font.pixelSize: 12
        wrapMode: Text.Wrap
        color: theme.darkTheme ? "#E8E8E8" : "#182536"
    }
    ToolTip.toolTip.background: Item {
        id: bubble
        readonly property color surfaceColor: theme.darkTheme ? "#2A2A2A" : "#FAFCFE"
        readonly property color edgeColor: theme.darkTheme ? "#454545" : "#DCE5EF"
        // Popup positioning clamps the body to the window. Keep the pointer
        // anchored to the control even when the body's horizontal position shifts.
        readonly property real arrowCenter: Math.max(16, Math.min(width - 16,
            theme.tip.parent ? theme.tip.parent.width / 2 - theme.tip.x : width / 2))
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: theme.below ? 7 : 0
            anchors.bottomMargin: theme.below ? 0 : 7
            radius: 7
            color: bubble.surfaceColor
            border.color: bubble.edgeColor
        }
        Canvas {
            id: arrow
            x: bubble.arrowCenter - width / 2
            y: theme.below ? 0 : parent.height - height
            width: 14
            height: 8
            property bool below: theme.below
            property color fillColor: bubble.surfaceColor
            property color strokeColor: bubble.edgeColor
            onBelowChanged: requestPaint()
            onFillColorChanged: requestPaint()
            onStrokeColorChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                var base = below ? height : 0;
                var point = below ? 1 : height - 1;
                ctx.beginPath();
                ctx.moveTo(0, base);
                ctx.lineTo(width / 2, point);
                ctx.lineTo(width, base);
                ctx.closePath();
                ctx.fillStyle = fillColor;
                ctx.fill();
                ctx.beginPath();
                ctx.moveTo(0, base);
                ctx.lineTo(width / 2, point);
                ctx.lineTo(width, base);
                ctx.strokeStyle = strokeColor;
                ctx.lineWidth = 1;
                ctx.stroke();
            }
        }
    }
}
