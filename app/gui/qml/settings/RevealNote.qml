// Plan 0045 (ADR-0361): above a Settings section or row that the user mode hides but that an
// error, a guide or a link opened - shown for this visit only.
import QtQuick
import QtQuick.Controls
import HardwarioMonitor

Label {
    // UiMode.Advanced or UiMode.Expert: the mode the section / row belongs to.
    property int mode: UiMode.Advanced

    objectName: "revealNote"
    textFormat: Text.PlainText
    text: mode === UiMode.Expert ? qsTr("Shown because it was needed. It is part of the Expert mode.")
                                 : qsTr("Shown because it was needed. It is part of the Advanced mode.")
    font: Theme.fontUiSmall
    color: Theme.textMuted
    wrapMode: Text.Wrap
    leftPadding: Theme.spacingS
    topPadding: Theme.spacingXs
    bottomPadding: Theme.spacingXs
    background: Rectangle {
        color: Theme.accentSoft
        radius: Theme.radiusS
    }
}
