#pragma once

// ConsentViewModel - the shown MCP consent prompt for ConsentWindow.qml (plan/features/0014
// "Consent prompt window", ADR-0101, ADR-0108, ADR-0161, ADR-0164, ADR-0242).
//
// Formats GuiConsentBroker::current() in the GUI language: the title ("Claude Code wants to flash
// firmware"), the detail rows (device and tab, file and size, catalog id, target, scope, address
// range, warnings), the remember button ("Allow flashing for Claude Code") and the countdown
// (updated once a second while a prompt is shown). The buttons call the broker. The window itself
// (flags, no transient parent) is QML; placing it on the screen under the cursor and the taskbar /
// dock alert of the main window are done here (QtGui, app/gui only).
//
// Ownership: child of McpController (Qt parent). GUI thread only.

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QWindow>
#include <QtQmlIntegration>
#include <QtTypes>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string_view>

namespace hwio::gui {

class GuiConsentBroker;

class ConsentViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by McpController")

    Q_PROPERTY(bool active READ active NOTIFY changed)
    // The shown prompt's id (0 = none); the buttons answer this id (review H1).
    Q_PROPERTY(qint64 promptId READ promptId NOTIFY changed)
    // False for armDelay after another prompt was shown: the Allow buttons are disabled (review H1,
    // ADR-0244). Deny always works.
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)
    Q_PROPERTY(QString client READ client NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    // [{label, value, warning: bool}]
    Q_PROPERTY(QVariantList details READ details NOTIFY changed)
    Q_PROPERTY(QString rememberLabel READ rememberLabel NOTIFY changed)
    Q_PROPERTY(bool canShowDevice READ canShowDevice NOTIFY changed)
    Q_PROPERTY(int queued READ queued NOTIFY changed)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY secondsLeftChanged)
    Q_PROPERTY(QString countdownText READ countdownText NOTIFY secondsLeftChanged)

public:
    static constexpr std::chrono::milliseconds armDelay{700}; // ADR-0244, review H1

    // How a device id appears in the prompt: the tab title and its index (-1: no tab).
    struct DeviceDescription {
        QString text;
        int tabIndex = -1;
    };
    using DeviceDescriber = std::function<DeviceDescription(std::string_view deviceId)>;

    ConsentViewModel(GuiConsentBroker& broker, DeviceDescriber describe,
                     std::function<void(int tabIndex)> showTab, QObject* parent = nullptr);

    [[nodiscard]] bool active() const;
    [[nodiscard]] qint64 promptId() const;
    [[nodiscard]] bool armed() const noexcept { return m_armed; }
    [[nodiscard]] QString client() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QVariantList details() const;
    [[nodiscard]] QString rememberLabel() const;
    [[nodiscard]] bool canShowDevice() const;
    [[nodiscard]] int queued() const;
    [[nodiscard]] int secondsLeft() const;
    [[nodiscard]] QString countdownText() const;

    // Answer the prompt `promptId` (ignored when another prompt is shown; the Allow answers also
    // while not armed).
    Q_INVOKABLE void deny(qint64 promptId);
    Q_INVOKABLE void allowOnce(qint64 promptId);
    Q_INVOKABLE void allowForClient(qint64 promptId);
    // Raises the main window on the device's tab; the prompt stays open.
    Q_INVOKABLE void showDevice();
    // Centres `prompt` on the screen under the mouse cursor (falling back to `mainWindow`'s
    // screen) and alerts `mainWindow` (taskbar flash / dock bounce). Either may be null.
    Q_INVOKABLE static void place(QWindow* prompt, QWindow* mainWindow);

signals:
    void changed();
    void secondsLeftChanged();
    void armedChanged();

private:
    void update();

    GuiConsentBroker*
        m_broker; // non-owning, outlives this object (both children of the controller)
    DeviceDescriber m_describe;
    std::function<void(int)> m_showTab;
    QTimer m_tick; // 1 s while a prompt is shown
    QTimer m_arm;  // armDelay after another prompt was shown
    std::uint64_t m_shownId = 0;
    bool m_armed = false;
};

} // namespace hwio::gui
