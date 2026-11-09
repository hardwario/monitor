#pragma once

// DiagnoseRunner - the session work shared by the Connect and Troubleshooting wizards
// (plan/features/0009 § Behaviour): runs `Diagnose` (ADR-0069) on the tab's session, or on a
// probe-only session it opens through the tab (DeviceTabViewModel::ensureSession(false)), and
// shows the steps the caller asks for in a CheckListModel; runs the manual [Reset]
// (reset(target, Go)); releases the probe-only session it opened when the wizard ends without a
// hand-off.
//
// - Rows: the caller names the Diagnose steps a check shows (empty = all six). Diagnose runs up to
//   the last of them (ADR-0296); when that is `power`, which is not a stop point, it runs up to
//   `connect` and the `connect` row is not shown (plan 0009 note, decided 2026-11-09).
// - Busy: while Diagnose or the reset runs, the tab is busy with the owner's id
//   (setBusy(owner, "Checking the connection…", …)), so the Flash page cannot start an operation
//   at the same time.
// - Bounds: one operation at a time (a second start() while one runs is ignored, as repeated
//   [Check again] clicks); one probe-only session per tab (the tab's own rule).
//
// Ownership: a Qt child of the wizard; keeps the tab only as QPointer (wizard contract). GUI thread
// only.

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/operation.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace hwio::device {
class DeviceSession;
} // namespace hwio::device

namespace hwio::gui {

class CheckListModel;
class DeviceTabViewModel;

class DiagnoseRunner : public QObject {
    Q_OBJECT

public:
    // `owner`: the busy page id (the wizard id).
    DiagnoseRunner(QString owner, QObject* parent);
    ~DiagnoseRunner() override;
    DiagnoseRunner(const DiagnoseRunner&) = delete;
    DiagnoseRunner& operator=(const DiagnoseRunner&) = delete;
    DiagnoseRunner(DiagnoseRunner&&) = delete;
    DiagnoseRunner& operator=(DiagnoseRunner&&) = delete;

    void setTab(DeviceTabViewModel* tab);

    // Resets `rows` (Pending) in `model` and starts Diagnose. False when one runs already. When it
    // cannot start (the session cannot be opened, internal.busy, …), the first row is Failed with
    // the error, startError() is set and finished() is emitted at once.
    bool start(const std::vector<std::string>& rows, CheckListModel* model);
    // [Reset]: reset(console target, Go) on the tab's or a probe-only session.
    bool reset();
    // Cancels the running operation (its result still arrives).
    void cancel();

    [[nodiscard]] bool running() const noexcept { return m_operation.has_value(); }
    [[nodiscard]] bool resetting() const noexcept { return m_operation && m_resetting; }
    [[nodiscard]] const std::optional<Error>& startError() const noexcept { return m_startError; }
    // The result of the last reset (nullopt before one finished).
    [[nodiscard]] const std::optional<Status>& resetResult() const noexcept {
        return m_resetResult;
    }
    // The session the runner opened (probe-only) is still the tab's.
    [[nodiscard]] bool openedProbeOnly() const;
    // A hand-off follows: the session stays for the next owner.
    void keepSession() noexcept { m_keep = true; }
    // Closes the probe-only session the runner opened (no running operation, not kept).
    void releaseProbeOnlySession();

    // The ids Diagnose runs for these rows (ADR-0296 stop point): "" = all.
    [[nodiscard]] static std::string lastStepFor(const std::vector<std::string>& rows);

signals:
    void finished(); // Diagnose or the reset ended (also when it could not start)
    void changed();

private:
    struct Operation {
        device::OperationId id = 0;
        QPointer<device::DeviceSession> session;
    };

    [[nodiscard]] Result<device::DeviceSession*> session();
    void watch(device::DeviceSession* session);
    void onStep(device::OperationId id, const device::DiagnoseStep& step);
    void onFinished(device::OperationId id, const device::OperationResult& result);
    void onSessionChanged();
    void end();
    void setBusy();
    void clearBusy();

    QString m_owner;
    QPointer<DeviceTabViewModel> m_tab;
    QPointer<CheckListModel> m_model;
    std::vector<std::string> m_rows; // the shown Diagnose steps, in order
    std::optional<Operation> m_operation;
    bool m_resetting = false;
    std::optional<Error> m_startError;
    std::optional<Status> m_resetResult;
    QPointer<device::DeviceSession> m_opened; // the probe-only session this runner opened
    bool m_keep = false;
    bool m_busy = false;
    std::vector<QMetaObject::Connection> m_connections;
    QPointer<device::DeviceSession> m_watched;
};

} // namespace hwio::gui
