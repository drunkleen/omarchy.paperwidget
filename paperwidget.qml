import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.Commons

Scope {
  SystemClock {
    id: clock
    precision: SystemClock.Seconds
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
        width: 680
        height: 150

        Text {
          anchors.top: parent.top
          anchors.left: parent.left
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

        Text {
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
      }
    }
  }
}
