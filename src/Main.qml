import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 400
    height: 568
    minimumWidth: 300
    minimumHeight: 430
    visible: false
    title: "Omacalc"

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color inkColor: backend.themeForeground
    readonly property color accentColor: backend.themeAccent

    // Every hardcoded size in the interface is expressed at the 400 × 568
    // design size; resizing the window scales the whole face with it. The
    // scientific panel adds three key rows, each a 56-pixel key plus its
    // 12-pixel gap. The launch size below already includes the desktop text
    // scale. A scale change while the window is open waits until the next
    // launch.
    readonly property int keyRowHeight: 56
    readonly property int keyGap: 12
    readonly property int keyRows: backend.scientific ? 8 : 5
    readonly property int designHeight: backend.scientific ? 772 : 568
    readonly property real uiScale: Math.min(width / 400, height / designHeight)

    property bool historyOpen: false

    function mixColors(base, tint, amount) {
        return Qt.rgba(
            base.r + (tint.r - base.r) * amount,
            base.g + (tint.g - base.g) * amount,
            base.b + (tint.b - base.b) * amount, 1);
    }
    readonly property color mutedColor: mixColors(pageColor, inkColor, 0.5)

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * uiScale));
    }

    // Thousands separators for reading only; the backend, copy, and paste all
    // keep plain digits. Fractions and exponents are left as they are.
    function grouped(text) {
        return String(text).replace(/\d+(?:\.\d*)?(?:e[+-]?\d+)?/g, function(number) {
            var parts = number.match(/^(\d+)(.*)$/);
            return parts[1].replace(/\B(?=(\d{3})+(?!\d))/g, ",") + parts[2];
        });
    }

    // Grow or shrink the window by the panel's rows so the keys keep their
    // size. A tiled window ignores this and the face scales down instead.
    function toggleScientific() {
        var before = designHeight;
        backend.scientific = !backend.scientific;
        height = Math.round(height * designHeight / before);
    }

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: accentColor
    color: pageColor

    Shortcut {
        sequences: ["Ctrl+C", "Meta+C"]
        context: Qt.ApplicationShortcut
        onActivated: backend.copyResult()
    }

    Shortcut {
        sequences: ["Ctrl+V", "Meta+V"]
        context: Qt.ApplicationShortcut
        onActivated: backend.pasteNumber()
    }

    Shortcut {
        sequence: "Ctrl+H"
        context: Qt.ApplicationShortcut
        onActivated: win.historyOpen = !win.historyOpen
    }

    Shortcut {
        sequence: "Ctrl+T"
        context: Qt.ApplicationShortcut
        onActivated: win.toggleScientific()
    }

    Shortcut {
        sequence: "Ctrl+D"
        context: Qt.ApplicationShortcut
        onActivated: backend.degrees = !backend.degrees
    }

    Shortcut {
        sequence: "Ctrl+Q"
        context: Qt.ApplicationShortcut
        onActivated: win.close()
    }

    // A small text toggle for the toolbar above the display.
    component ToolLabel: Text {
        id: toolLabel
        property bool active: false
        signal activated()

        color: active ? win.accentColor
                      : (toolHit.containsMouse ? win.inkColor : win.mutedColor)
        font.family: "iA Writer Mono S"
        font.pixelSize: win.scaledSize(13)
        font.letterSpacing: win.scaledSize(1)
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.onPressAction: activated()

        MouseArea {
            id: toolHit
            anchors.fill: parent
            anchors.margins: -win.scaledSize(6)
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: toolLabel.activated()
        }
    }

    Item {
        id: face
        anchors.fill: parent
        anchors.margins: win.scaledSize(20)
        focus: true

        // The numpad without NumLock sends navigation keys. A calculator has no
        // use for those, and a compositor can hand a freshly focused window a
        // keymap that drops NumLock, so read the numpad as digits either way.
        readonly property var numpadKeys: ({
            [Qt.Key_Insert]: "0", [Qt.Key_End]: "1", [Qt.Key_Down]: "2",
            [Qt.Key_PageDown]: "3", [Qt.Key_Left]: "4", [Qt.Key_Clear]: "5",
            [Qt.Key_Right]: "6", [Qt.Key_Home]: "7", [Qt.Key_Up]: "8",
            [Qt.Key_PageUp]: "9", [Qt.Key_Delete]: "."
        })

        Keys.onPressed: function(event) {
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                return;

            if (win.historyOpen && event.key === Qt.Key_Escape) {
                win.historyOpen = false;
            } else if ((event.modifiers & Qt.KeypadModifier) && event.key in numpadKeys) {
                backend.pressKey(numpadKeys[event.key]);
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.text === "=") {
                backend.pressKey("=");
            } else if (event.key === Qt.Key_Backspace) {
                backend.pressKey("backspace");
            } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Delete) {
                backend.pressKey("clear");
            } else if (event.text === "," || event.text === ".") {
                backend.pressKey(".");
            } else if (event.text === "s" || event.text === "S") {
                backend.pressKey("sign");
            } else if (event.text === "c" || event.text === "C") {
                backend.pressKey("clear");
            } else if (event.text === "x" || event.text === "X") {
                backend.pressKey("*");
            } else if (event.text === "p" || event.text === "P") {
                backend.pressKey("π");
            } else if (event.text === "e" || event.text === "E") {
                backend.pressKey("e");
            } else if (event.text === "r" || event.text === "R") {
                backend.pressKey("sqrt");
            } else if (/^[0-9+\-*/%^()!]$/.test(event.text)) {
                backend.pressKey(event.text);
            } else {
                return;
            }
            event.accepted = true;
        }

        Item {
            id: displayArea
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: divider.top
            anchors.leftMargin: win.scaledSize(8)
            anchors.rightMargin: win.scaledSize(8)
            anchors.bottomMargin: win.scaledSize(24)

            Item {
                id: toolbar
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: win.scaledSize(18)

                ToolLabel {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: backend.scientific
                    text: backend.degrees ? "DEG" : "RAD"
                    onActivated: backend.degrees = !backend.degrees
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: win.scaledSize(18)

                    ToolLabel {
                        text: "FX"
                        active: backend.scientific
                        Accessible.name: "Scientific keys"
                        onActivated: win.toggleScientific()
                    }

                    ToolLabel {
                        text: "HISTORY"
                        active: win.historyOpen
                        onActivated: win.historyOpen = !win.historyOpen
                    }
                }
            }

            Text {
                id: expressionText
                anchors.top: toolbar.bottom
                anchors.topMargin: win.scaledSize(10)
                anchors.left: parent.left
                anchors.right: parent.right
                text: win.grouped(backend.expression)
                color: win.mutedColor
                elide: Text.ElideLeft
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(21)
            }

            // The answer so far, before equals is pressed.
            Text {
                id: previewText
                anchors.bottom: displayText.top
                anchors.left: parent.left
                anchors.right: parent.right
                horizontalAlignment: Text.AlignRight
                text: backend.preview === "" ? "" : "= " + win.grouped(backend.preview)
                color: win.accentColor
                elide: Text.ElideLeft
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(21)
            }

            Text {
                id: displayText
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: Math.min(win.scaledSize(92),
                                 parent.height - toolbar.height - win.scaledSize(10)
                                 - expressionText.height - previewText.height)
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignBottom
                text: win.grouped(backend.display)
                color: win.inkColor
                fontSizeMode: Text.Fit
                minimumPixelSize: win.scaledSize(22)
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(76)
            }
        }

        Rectangle {
            id: divider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: keypad.top
            anchors.bottomMargin: win.scaledSize(22)
            height: 1
            color: win.mixColors(win.pageColor, win.inkColor, 0.16)
        }

        GridLayout {
            id: keypad
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: win.scaledSize(win.keyRows * win.keyRowHeight
                                   + (win.keyRows - 1) * win.keyGap)
            columns: 4
            rowSpacing: win.scaledSize(win.keyGap)
            columnSpacing: win.scaledSize(win.keyGap)

            readonly property var scientificKeys: [
                { label: "(", key: "(", kind: "function" },
                { label: ")", key: ")", kind: "function" },
                { label: "xʸ", key: "^", kind: "function" },
                { label: "√", key: "sqrt", kind: "function" },
                { label: "sin", key: "sin", kind: "function" },
                { label: "cos", key: "cos", kind: "function" },
                { label: "tan", key: "tan", kind: "function" },
                { label: "x!", key: "!", kind: "function" },
                { label: "ln", key: "ln", kind: "function" },
                { label: "log", key: "log", kind: "function" },
                { label: "π", key: "π", kind: "function" },
                { label: "e", key: "e", kind: "function" }
            ]

            readonly property var basicKeys: [
                { label: "AC", key: "clear", kind: "number" },
                { label: "±", key: "sign", kind: "number" },
                { label: "%", key: "%", kind: "number" },
                { label: "÷", key: "÷", kind: "operator" },
                { label: "7", key: "7", kind: "number" },
                { label: "8", key: "8", kind: "number" },
                { label: "9", key: "9", kind: "number" },
                { label: "×", key: "×", kind: "operator" },
                { label: "4", key: "4", kind: "number" },
                { label: "5", key: "5", kind: "number" },
                { label: "6", key: "6", kind: "number" },
                { label: "−", key: "-", kind: "operator" },
                { label: "1", key: "1", kind: "number" },
                { label: "2", key: "2", kind: "number" },
                { label: "3", key: "3", kind: "number" },
                { label: "+", key: "+", kind: "operator" },
                { label: "0", key: "0", kind: "number" },
                { label: ".", key: ".", kind: "number" },
                { label: "", key: "backspace", kind: "number", icon: "backspace" },
                { label: "=", key: "=", kind: "equals" }
            ]

            Repeater {
                model: backend.scientific
                    ? keypad.scientificKeys.concat(keypad.basicKeys)
                    : keypad.basicKeys

                CalcButton {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    label: modelData.label
                    keyValue: modelData.key
                    kind: modelData.kind
                    iconName: modelData.icon === undefined ? "" : modelData.icon
                    pageColor: win.pageColor
                    inkColor: win.inkColor
                    onActivated: backend.pressKey(keyValue)
                }
            }
        }

        // Past calculations, newest first, laid over the keypad. Picking one
        // drops its result into the current expression.
        Rectangle {
            id: historyPanel
            anchors.fill: keypad
            visible: win.historyOpen
            color: win.pageColor

            // Swallow clicks so they never reach the keys underneath.
            MouseArea {
                anchors.fill: parent
            }

            Text {
                anchors.centerIn: parent
                visible: historyList.count === 0
                text: "No calculations yet"
                color: win.mutedColor
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(16)
            }

            ListView {
                id: historyList
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: historyFooter.top
                anchors.bottomMargin: win.scaledSize(8)
                clip: true
                model: backend.history
                spacing: win.scaledSize(4)
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: historyRow
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: historyColumn.implicitHeight + win.scaledSize(16)
                    radius: win.scaledSize(10)
                    color: rowHit.containsMouse
                        ? win.mixColors(win.pageColor, win.inkColor, 0.07) : "transparent"
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.expression + " equals " + modelData.result

                    Column {
                        id: historyColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: win.scaledSize(12)
                        anchors.rightMargin: win.scaledSize(12)

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignRight
                            text: win.grouped(historyRow.modelData.expression)
                            color: win.mutedColor
                            elide: Text.ElideLeft
                            font.family: "iA Writer Mono S"
                            font.pixelSize: win.scaledSize(15)
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignRight
                            text: "= " + win.grouped(historyRow.modelData.result)
                            color: win.inkColor
                            elide: Text.ElideLeft
                            font.family: "iA Writer Mono S"
                            font.pixelSize: win.scaledSize(24)
                        }
                    }

                    MouseArea {
                        id: rowHit
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            backend.recallHistory(historyRow.index);
                            win.historyOpen = false;
                        }
                    }
                }
            }

            Item {
                id: historyFooter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: win.scaledSize(24)

                ToolLabel {
                    anchors.left: parent.left
                    anchors.leftMargin: win.scaledSize(12)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: historyList.count > 0
                    text: "CLEAR"
                    Accessible.name: "Clear history"
                    onActivated: backend.clearHistory()
                }

                ToolLabel {
                    anchors.right: parent.right
                    anchors.rightMargin: win.scaledSize(12)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "CLOSE"
                    Accessible.name: "Close history"
                    onActivated: win.historyOpen = false
                }
            }
        }
    }

    Component.onCompleted: {
        // Chosen once. Hyprland reports a normal float as maximized, so a
        // saved rectangle was the tiled size and the next launch filled the
        // screen. Position stays with the compositor.
        var size = backend.defaultWindowSize();
        width = size.width;
        height = size.height;
        visible = true;
    }
}
