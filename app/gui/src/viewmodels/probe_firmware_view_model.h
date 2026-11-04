#pragma once

// ProbeFirmwareViewModel - the J-Link firmware update of the GUI
// (plan/features/0036-jlink-firmware-update-gui.md; ADR-0132, ADR-0246, ADR-0255..0257).
//
// One shared action for every entry point: the tab banner of probe.firmware_outdated
// (requestFromTab: the tab disconnects first, ADR-0246, and connects again after a successful
// update, ADR-0256) and, once they exist, the setup wizard's `probe-firmware` row (0008) and the
// troubleshooting topic `probe-firmware` (0009) (requestForSerial: refused while a tab uses the
// probe; nothing connects afterwards). ProbeFirmwareDialog.qml shows the states:
//   Idle -> Reading (inspectProbe on the worker, "Reading the J-Link…") -> Confirm (product, SN,
//   current and new firmware strings; [Cancel] [Update]) -> Updating (no Cancel) -> Idle with a
//   toast, or Failed (ErrorPanel, [Close]).
//
// Exclusivity (ADR-0257): from the request until the job's result has been handled the probe is
// reserved in the SessionRegistry (no tab of this process can connect to it; the pickers show
// "Updating firmware…") and the ProbeScanner is suspended. The job holds the probe's ProbeLock
// (other processes). One update at a time per application.
//
// Blocking (blockedReason): another update runs; an AI assistant holds the probe (the GUI MCP
// server of plan 0014 plugs in through setAiHoldsProbe(); until then an MCP stdio process is
// caught by the ProbeLock); the tab using the probe is busy (flash, LTE update, Diagnose); for a
// wizard request: a tab uses the probe ("Disconnect tab %1 first.").
//
// Bounds: one job at a time; "Not now" remembers ≤ maxNotNow serial numbers (oldest dropped)
// until the application ends; the inspect job takes ≤ 5 s (vendor default), the update has no own
// timeout (vendor call, 10–30 s).
//
// Ownership: created by AppViewModel (Qt parent; deleted by its destructor after the tabs, before
// the registry and the scanner). GUI thread only.

#include "hwio/device/session_registry.h"
#include "hwio/hw/jlink/library_locator.h"
#include "viewmodels/error_info.h"
#include "workers/probe_firmware_worker.h"
#include "workers/probe_scanner.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace hwio::gui {

class DeviceTabViewModel;

// What the view model needs from the application (AppViewModel; tests build their own).
struct ProbeFirmwareContext {
    device::SessionRegistry* registry = nullptr; // outlives the view model
    ProbeScanner* scanner = nullptr;             // may be null; outlives the view model
    std::function<jlink::LocatorOptions()> locatorOptions;
    std::function<QList<DeviceTabViewModel*>()> tabs; // the device tabs, in order
    std::function<void(const QString&)> showToast;    // may be empty
    ProbeFirmwareWorker::Tools tools = ProbeFirmwareWorker::defaultTools();
};

class ProbeFirmwareViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString product READ product NOTIFY changed)
    Q_PROPERTY(QString serial READ serial NOTIFY changed)
    Q_PROPERTY(QString currentString READ currentString NOTIFY changed)
    Q_PROPERTY(QString availableString READ availableString NOTIFY changed)
    Q_PROPERTY(hwio::gui::ErrorInfo error READ error NOTIFY changed)
    // The user asked to quit while the update runs: the application quits when it has finished
    // (ADR-0261: not after a failure, which cancels the quit and is shown).
    Q_PROPERTY(bool quitPending READ quitPending NOTIFY changed)
    // ADR-0261: the probe's firmware status could not be determined; Confirm asks "Update anyway?".
    Q_PROPERTY(bool statusUnknown READ statusUnknown NOTIFY changed)

public:
    // Unscoped: QML compares ProbeFirmwareViewModel.Confirm etc. with the int property.
    enum State : quint8 { Idle, Reading, Confirm, Updating, Failed }; // NOLINT(*-use-enum-class)
    Q_ENUM(State)

    static constexpr std::size_t maxNotNow = 32;

    explicit ProbeFirmwareViewModel(ProbeFirmwareContext context, QObject* parent = nullptr);
    ~ProbeFirmwareViewModel() override; // waits for a running job (never interrupted)
    ProbeFirmwareViewModel(const ProbeFirmwareViewModel&) = delete;
    ProbeFirmwareViewModel& operator=(const ProbeFirmwareViewModel&) = delete;
    ProbeFirmwareViewModel(ProbeFirmwareViewModel&&) = delete;
    ProbeFirmwareViewModel& operator=(ProbeFirmwareViewModel&&) = delete;

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] QString product() const { return m_product; }
    [[nodiscard]] QString serial() const;
    [[nodiscard]] QString currentString() const { return m_current; }
    [[nodiscard]] QString availableString() const { return m_available; }
    [[nodiscard]] ErrorInfo error() const { return m_error; }
    [[nodiscard]] bool quitPending() const noexcept { return m_quitPending; }
    [[nodiscard]] bool statusUnknown() const noexcept { return m_statusUnknown; }
    // A request is open or a job runs (also a job whose dialog was cancelled while reading).
    [[nodiscard]] bool busy() const noexcept { return m_state != Idle || m_worker->busy(); }
    // The update itself runs (quit waits for it).
    [[nodiscard]] bool updating() const noexcept { return m_state == Updating; }

    // "" when the update can start; otherwise the translated reason. `requester`: the tab whose
    // banner asks (it disconnects itself, so its own use of the probe is not a reason).
    [[nodiscard]] QString blockedReason(std::uint32_t serial,
                                        const DeviceTabViewModel* requester = nullptr) const;
    // For the wizards (0008/0009): blockedReason() of a decimal serial number.
    Q_INVOKABLE [[nodiscard]] QString blockedReasonFor(const QString& serial) const;
    // From a tab banner: the tab disconnects first (ADR-0246) and connects again after a
    // successful update (ADR-0256). Ignored while blocked.
    void requestFromTab(DeviceTabViewModel& tab);
    // From the setup wizard / troubleshooting: refused with blockedReason (a toast) when a tab
    // uses the probe or the update is blocked.
    Q_INVOKABLE void requestForSerial(const QString& serial);
    Q_INVOKABLE void confirm();
    Q_INVOKABLE void cancel(); // only in Reading / Confirm / Failed

    // "Not now" of the banner: the banner stays hidden for this probe until the application ends.
    void notNow(std::uint32_t serial);
    [[nodiscard]] bool isNotNow(std::uint32_t serial) const;

    // Plan 0014 (GUI MCP server, ADR-0105): true while an AI assistant holds the probe. Empty
    // (the default) = never.
    void setAiHoldsProbe(std::function<bool(std::uint32_t serial)> holds);
    // AppViewModel::canQuit while updating.
    void setQuitPending(bool pending);

signals:
    void changed();
    void finished(bool updated);
    // ADR-0261: the update failed while a quit was pending; the quit is cancelled (AppViewModel).
    void quitCancelled();

private:
    void start(std::uint32_t serial, const QString& product);
    void onJobFinished(ProbeFirmwareWorker::Job job, ProbeFirmwareWorker::Outcome outcome);
    void fail(const Error& error);
    // The request ends without an error; `reconnect`: the banner's tab connects again.
    void finish(bool updated, bool reconnect, const QString& toast);
    void release();
    void setState(State state);
    void toast(const QString& text) const;

    ProbeFirmwareContext m_context;
    std::unique_ptr<ProbeFirmwareWorker> m_worker;
    std::function<bool(std::uint32_t)> m_aiHoldsProbe;
    State m_state = Idle;
    std::optional<std::uint32_t> m_serial;
    QString m_product;
    QString m_current;
    QString m_available;
    ErrorInfo m_error;
    jlink::LibraryLocation m_location;
    QPointer<DeviceTabViewModel> m_tab; // the banner's tab (reconnect), null for the wizards
    bool m_discard = false;             // cancelled while reading: drop the result
    bool m_quitPending = false;
    bool m_statusUnknown = false;        // ADR-0261
    std::vector<std::uint32_t> m_notNow; // ≤ maxNotNow, oldest first
    // Held from the request until the job's result has been handled (ADR-0257).
    device::ProbeReservation m_reservation;
    ScanSuspension m_suspension;
};

} // namespace hwio::gui
