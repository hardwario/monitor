// The frame of a view in the page area (plan 0052, ADR-0386, ADR-0387): a wizard (WizardView) or a
// content view (report, J-Link firmware, join, share, mode choice, keyboard shortcuts) replaces the
// page area of the tab instead of opening a dialog. Header: the title (a focusable heading), extra
// header content (`headerData`, e.g. the wizard step list) and × (`closeRequested`). Content
// (default property): a ScrollView with the ADR-0384 hints (the scroll bar while the content
// overflows, a fade at the bottom while more is below, `ensureVisible()`). Footer (`footerData`):
// pinned at the bottom. Header, content and footer are at most `maxContentWidth` wide and centred.
// Esc = × while the view has focus (`closeEnabled`); Enter in the content = `defaultRequested`.
// Object names: <namePrefix>Title, <namePrefix>Content, <namePrefix>ScrollBar, <namePrefix>Fade
// and `closeObjectName` (default <namePrefix>Close). Presentation only (ADR-0003).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FocusScope {
    id: frame

    property string title: ""
    property string namePrefix: "view"
    property string closeText: qsTr("Close")
    property bool closeEnabled: true
    property string closeObjectName: frame.namePrefix + "Close"
    property int maxContentWidth: 880
    default property alias content: column.data
    property alias headerData: headerExtra.data
    property alias footerData: footerArea.data
    readonly property Flickable flickable: scroll.contentItem as Flickable
    readonly property real columnWidth: Math.max(0, Math.min(frame.maxContentWidth, frame.width - 2 * Theme.spacingXl))
    readonly property bool overflows: frame.flickable !== null
                                      && frame.flickable.contentHeight > frame.flickable.height + 1
    readonly property bool moreBelow: frame.overflows
                                      && frame.flickable.contentY + frame.flickable.height
                                         < frame.flickable.contentHeight - 1

    signal closeRequested
    signal defaultRequested

    Accessible.role: Accessible.Pane
    Accessible.name: frame.title

    // Moves the focus to the title (a view opened or shown again).
    function focusTitle() {
        titleLabel.forceActiveFocus();
    }

    // ADR-0384: scrolls the content so that `item` is in view, its top at the top of the view as
    // far as possible (animated).
    function ensureVisible(item: Item) {
        const flick = frame.flickable;
        if (item === null || flick === null)
            return;
        const top = item.mapToItem(flick.contentItem, 0, 0).y;
        const target = Math.max(0, Math.min(top, flick.contentHeight - flick.height));
        scrollAnimation.stop();
        scrollAnimation.from = flick.contentY;
        scrollAnimation.to = target;
        scrollAnimation.start();
    }

    Keys.onEscapePressed: event => {
        if (frame.closeEnabled)
            frame.closeRequested();
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            objectName: frame.namePrefix + "Header"
            // A nested layout fills by default: the header keeps the column width (centred) and only
            // the content takes the free height.
            Layout.fillWidth: false
            Layout.fillHeight: false
            Layout.preferredWidth: frame.columnWidth
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingL
            spacing: Theme.spacingS

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Label {
                    id: titleLabel

                    textFormat: Text.PlainText
                    objectName: frame.namePrefix + "Title"
                    Layout.fillWidth: true
                    text: frame.title
                    font: Theme.fontTitle
                    color: Theme.text
                    wrapMode: Text.Wrap
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: titleLabel.text
                }

                HToolButton {
                    objectName: frame.closeObjectName
                    Layout.alignment: Qt.AlignTop
                    iconName: "close"
                    text: frame.closeText
                    display: AbstractButton.IconOnly
                    enabled: frame.closeEnabled
                    onClicked: frame.closeRequested()
                }
            }

            ColumnLayout {
                id: headerExtra

                Layout.fillWidth: true
                spacing: Theme.spacingS
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingS
            implicitHeight: 1
            color: Theme.border
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                id: scroll

                objectName: frame.namePrefix + "Content"
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                contentHeight: column.implicitHeight + 2 * Theme.spacingL
                // ADR-0384: the scroll bar stays visible while the content is taller than the view.
                ScrollBar.vertical: ScrollBar {
                    objectName: frame.namePrefix + "ScrollBar"
                    parent: scroll
                    x: scroll.mirrored ? 0 : scroll.width - width
                    y: scroll.topPadding
                    height: scroll.availableHeight
                    active: scroll.ScrollBar.horizontal.active
                    policy: frame.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }

                Keys.onReturnPressed: frame.defaultRequested()
                Keys.onEnterPressed: frame.defaultRequested()

                Item {
                    width: scroll.availableWidth
                    height: scroll.contentHeight

                    ColumnLayout {
                        id: column

                        x: Math.max(0, (parent.width - width) / 2)
                        y: Theme.spacingL
                        width: frame.columnWidth
                        spacing: Theme.spacingM
                    }
                }
            }

            Rectangle {
                objectName: frame.namePrefix + "Fade"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: frame.overflows ? scroll.ScrollBar.vertical.width : 0
                height: 24
                visible: frame.moreBelow
                gradient: Gradient {
                    GradientStop {
                        position: 0
                        color: Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b, 0)
                    }
                    GradientStop {
                        position: 1
                        color: Theme.bg
                    }
                }
            }

            NumberAnimation {
                id: scrollAnimation

                target: frame.flickable
                property: "contentY"
                duration: Theme.animationMs
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }

        ColumnLayout {
            id: footerArea

            objectName: frame.namePrefix + "Footer"
            Layout.fillWidth: false
            Layout.fillHeight: false
            Layout.preferredWidth: frame.columnWidth
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingM
            Layout.bottomMargin: Theme.spacingM
            spacing: Theme.spacingS
        }
    }
}
