import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons

Scope {
  id: paperWidget
  readonly property string analyzerPath: Qt.resolvedUrl("analyzer").toString().replace("file://", "")

  SystemClock {
    id: clock
    precision: SystemClock.Seconds
  }

  property var bandValues: [0, 0, 0, 0, 0, 0, 0, 0]
  property var bandOrder: [7, 4, 3, 6, 2, 5, 0, 1]
  property var bandGain: [1.0, 1.0, 2.0, 1.0, 3.5, 1.0, 5.0, 3.5]

  Process {
    id: analyzerProcess
    command: [paperWidget.analyzerPath]
    running: true
    stdout: SplitParser {
      onRead: function(line) {
        var trimmed = String(line || "").trim();
        if (!trimmed.startsWith("{")) return
        try {
          var obj = JSON.parse(trimmed)
          if (Array.isArray(obj.bands) && obj.bands.length === 8)
            paperWidget.bandValues = obj.bands
        } catch (e) {}
      }
    }
    stderr: StdioCollector {}
    onStarted: restartTimer.stop()
    onExited: function(code) { restartTimer.restart() }
  }

  Timer {
    id: restartTimer
    interval: 1500
    onTriggered: analyzerProcess.running = true
  }

  Component.onDestruction: {
    if (analyzerProcess.running) analyzerProcess.running = false
  }

  Variants {
    model: Quickshell.screens

    PanelWindow {
      required property var modelData

      screen: modelData
      anchors { top: true; bottom: true; left: true; right: true }
      color: "transparent"
      exclusionMode: ExclusionMode.Ignore
      mask: Region {}

      WlrLayershell.namespace: "drunkleen-desktop-clock"
      WlrLayershell.layer: WlrLayer.Bottom
      WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

      Item {
        anchors.left: parent.left
        anchors.leftMargin: 48
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        width: timeText.implicitWidth
        height: 260

        Text {
          id: dateText
          anchors.left: parent.left
          anchors.bottom: parent.bottom
          height: 42
          text: Qt.formatDate(clock.date, "dddd, MMMM d")
          color: Color.foreground
          opacity: 0.9
          horizontalAlignment: Text.AlignLeft
          verticalAlignment: Text.AlignVCenter
          font.family: "Stencil Pixel-7"
          font.pixelSize: 20
          font.letterSpacing: 1
          renderType: Text.NativeRendering
        }

        Text {
          id: timeText
          anchors.left: parent.left
          anchors.bottom: dateText.top
          anchors.bottomMargin: 0
          height: 100
          text: Qt.formatTime(clock.date, "HH:mm")
          color: Color.foreground
          horizontalAlignment: Text.AlignLeft
          verticalAlignment: Text.AlignVCenter
          font.family: "Stencil Pixel-7"
          font.pixelSize: 110
          font.letterSpacing: 3
          renderType: Text.NativeRendering
        }

        Item {
          id: eqContainer
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.bottom: timeText.top
          anchors.bottomMargin: 16
          height: 100

          Row {
            anchors.fill: parent
            anchors.leftMargin: 0
            anchors.rightMargin: 0
            spacing: 8

            Repeater {
              model: 8
              delegate: Item {
                property int idx: index
                property int bandIdx: paperWidget.bandOrder[idx]
                width: (eqContainer.width - (9 * 8)) / 8
                height: eqContainer.height

                Rectangle {
                  anchors.bottom: parent.bottom
                  width: parent.width
                  height: Math.max(2, paperWidget.bandValues[bandIdx] * paperWidget.bandGain[idx] * 100)
                  radius: 4
                  color: Color.foreground
                  opacity: 0.85
                  Behavior on height {
                    NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
