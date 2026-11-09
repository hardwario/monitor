#include "wizard/diagnose_runner.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/diag/check.h"
#include "viewmodels/device_tab_view_model.h"
#include "wizard/check_list_model.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QtCore/qtmetamacros.h>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hwio::gui {

DiagnoseRunner::DiagnoseRunner(QString owner, QObject* parent)
    : QObject(parent), m_owner(std::move(owner)) {}

DiagnoseRunner::~DiagnoseRunner() {
    for (const QMetaObject::Connection& connection : m_connections) {
        disconnect(connection);
    }
    cancel();
    clearBusy();
    releaseProbeOnlySession();
}

void DiagnoseRunner::setTab(DeviceTabViewModel* tab) {
    if (m_tab.data() == tab) {
        return;
    }
    if (!m_tab.isNull()) {
        disconnect(m_tab, nullptr, this, nullptr);
    }
    m_tab = tab;
    if (tab != nullptr) {
        connect(tab, &DeviceTabViewModel::sessionChanged, this, &DiagnoseRunner::onSessionChanged);
    }
    watch(tab != nullptr ? tab->session() : nullptr);
}

std::string DiagnoseRunner::lastStepFor(const std::vector<std::string>& rows) {
    if (rows.empty()) {
        return {};
    }
    // ADR-0296: "power" is not a stop point (its reading may wait for the connect).
    return rows.back() == "power" ? std::string("connect") : rows.back();
}

Result<device::DeviceSession*> DiagnoseRunner::session() {
    if (m_tab.isNull()) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "the tab was closed"));
    }
    const bool hadSession = m_tab->session() != nullptr;
    if (Status opened = m_tab->ensureSession(false); !opened) {
        return std::unexpected(std::move(opened).error());
    }
    device::DeviceSession* session = m_tab->session();
    if (session == nullptr) {
        return std::unexpected(
            makeError(ErrorCode::InternalUnexpected, "no session after ensureSession"));
    }
    if (!hadSession && session->connection().state != device::ConnectionState::Connected) {
        m_opened = session;
    }
    watch(session);
    return session;
}

bool DiagnoseRunner::start(const std::vector<std::string>& rows, CheckListModel* model) {
    if (m_operation || model == nullptr) {
        return false;
    }
    m_model = model;
    m_startError.reset();
    // The rows and their titles (the console target's step titles, plan 0007).
    std::vector<std::string> ids = rows;
    if (ids.empty()) {
        ids.assign(device::diagnoseStepIds.begin(), device::diagnoseStepIds.end());
    }
    m_rows = ids;
    std::vector<QString> rowIds;
    std::vector<QString> titles;
    const device::DeviceProfile* profile =
        m_tab.isNull() ? nullptr : device::ProfileRegistry::find(toStdString(m_tab->profileId()));
    const device::TargetProfile* target =
        profile != nullptr && !m_tab.isNull()
            ? device::findTarget(*profile, toStdString(m_tab->consoleTargetId()))
            : nullptr;
    for (const std::string& id : ids) {
        QString title = toQString(id);
        if (target != nullptr) {
            for (const device::DiagnoseStepInfo& info : device::diagnoseSteps(*target)) {
                if (info.id == id) {
                    title = toQString(info.title);
                }
            }
        }
        rowIds.push_back(toQString(id));
        titles.push_back(title);
    }
    model->reset(rowIds, titles);
    const std::string lastStep = lastStepFor(rows);
    Result<device::OperationId> started = [&]() -> Result<device::OperationId> {
        Result<device::DeviceSession*> opened = session();
        if (!opened) {
            return std::unexpected(std::move(opened).error());
        }
        return (*opened)->diagnose(toStdString(m_tab->consoleTargetId()), lastStep);
    }();
    if (!started) {
        Error error = std::move(started).error();
        model->update(device::DiagnoseStep{.id = m_rows.front(),
                                           .status = diag::CheckStatus::Failed,
                                           .result = error.message,
                                           .error = error});
        for (std::size_t i = 1; i < m_rows.size(); ++i) {
            model->update(device::DiagnoseStep{.id = m_rows.at(i),
                                               .status = diag::CheckStatus::Skipped,
                                               .result = tr("Not checked").toStdString(),
                                               .error = std::nullopt});
        }
        m_startError = std::move(error);
        emit finished();
        emit changed();
        return true;
    }
    m_operation = Operation{.id = *started, .session = m_tab->session()};
    m_resetting = false;
    model->update(device::DiagnoseStep{.id = m_rows.front(),
                                       .status = diag::CheckStatus::Running,
                                       .result = {},
                                       .error = std::nullopt});
    setBusy();
    emit changed();
    return true;
}

bool DiagnoseRunner::reset() {
    if (m_operation) {
        return false;
    }
    m_resetResult.reset();
    Result<device::OperationId> started = [&]() -> Result<device::OperationId> {
        Result<device::DeviceSession*> opened = session();
        if (!opened) {
            return std::unexpected(std::move(opened).error());
        }
        return (*opened)->reset(toStdString(m_tab->consoleTargetId()), device::ResetMode::Go);
    }();
    if (!started) {
        m_resetResult = Status(std::unexpected(std::move(started).error()));
        emit finished();
        emit changed();
        return true;
    }
    m_operation = Operation{.id = *started, .session = m_tab->session()};
    m_resetting = true;
    m_rows.clear();
    setBusy();
    emit changed();
    return true;
}

void DiagnoseRunner::cancel() {
    if (!m_operation || m_operation->session.isNull()) {
        return;
    }
    if (const Status status = m_operation->session->cancel(m_operation->id); !status) {
        qCDebug(lcGui).noquote() << "Wizard: cancel refused:" << toQString(status.error().detail);
    }
}

void DiagnoseRunner::watch(device::DeviceSession* session) {
    if (session == m_watched.data() && (session == nullptr || !m_connections.empty())) {
        return;
    }
    for (const QMetaObject::Connection& connection : m_connections) {
        disconnect(connection);
    }
    m_connections.clear();
    m_watched = session;
    if (session == nullptr) {
        return;
    }
    m_connections.push_back(
        connect(session, &device::DeviceSession::diagnoseStep, this, &DiagnoseRunner::onStep));
    m_connections.push_back(connect(session, &device::DeviceSession::operationFinished, this,
                                    &DiagnoseRunner::onFinished));
}

void DiagnoseRunner::onStep(device::OperationId id, const device::DiagnoseStep& step) {
    if (!m_operation || id != m_operation->id || m_model.isNull()) {
        return;
    }
    const auto it = std::ranges::find(m_rows, step.id);
    if (it == m_rows.end()) {
        return; // a step this check does not show (e.g. `connect` after `power`)
    }
    m_model->update(step);
    if (std::next(it) != m_rows.end()) {
        m_model->update(device::DiagnoseStep{.id = *std::next(it),
                                             .status = diag::CheckStatus::Running,
                                             .result = {},
                                             .error = std::nullopt});
    }
    emit changed();
}

void DiagnoseRunner::onFinished(device::OperationId id, const device::OperationResult& result) {
    if (!m_operation || id != m_operation->id) {
        return;
    }
    if (m_resetting) {
        m_resetResult = result.status;
    } else if (!result.status && !m_model.isNull() && !m_rows.empty() &&
               m_model->status(toQString(m_rows.front())) == diag::CheckStatus::Running) {
        // The operation itself could not run (busy, cancelled): the first row says why.
        const Error& error = result.status.error();
        if (error.code != ErrorCode::Cancelled) {
            m_model->update(device::DiagnoseStep{.id = m_rows.front(),
                                                 .status = diag::CheckStatus::Failed,
                                                 .result = error.message,
                                                 .error = error});
        }
    }
    end();
}

void DiagnoseRunner::onSessionChanged() {
    device::DeviceSession* current = m_tab.isNull() ? nullptr : m_tab->session();
    if (m_operation && m_operation->session.data() != current) {
        end(); // the session went away: the remaining rows are not checked
    }
    watch(current);
}

void DiagnoseRunner::end() {
    m_operation.reset();
    clearBusy();
    if (!m_model.isNull()) {
        // Rows a cancelled or failed Diagnose did not report stay "not checked".
        for (const std::string& row : m_rows) {
            const diag::CheckStatus status = m_model->status(toQString(row));
            if (status == diag::CheckStatus::Pending || status == diag::CheckStatus::Running) {
                m_model->update(device::DiagnoseStep{.id = row,
                                                     .status = diag::CheckStatus::Skipped,
                                                     .result = tr("Not checked").toStdString(),
                                                     .error = std::nullopt});
            }
        }
    }
    emit finished();
    emit changed();
}

bool DiagnoseRunner::openedProbeOnly() const {
    return !m_opened.isNull() && !m_tab.isNull() && m_tab->session() == m_opened.data() &&
           m_opened->connection().state == device::ConnectionState::ProbeOpen;
}

void DiagnoseRunner::releaseProbeOnlySession() {
    // A probe-only session the wizard opened is closed when it ends without a hand-off (plan 0009
    // § Behaviour); a running operation keeps it.
    if (m_keep || !openedProbeOnly() || m_tab->isBusy() ||
        m_opened->runningOperation().has_value()) {
        return;
    }
    m_tab->disconnectDevice();
    m_opened.clear();
}

void DiagnoseRunner::setBusy() {
    if (m_tab.isNull() || m_busy) {
        return;
    }
    m_tab->setBusy(m_owner, tr("Checking the connection…"),
                   tr("The connection is being checked. Cancel the check and close the tab?"),
                   [this] { cancel(); });
    m_busy = true;
}

void DiagnoseRunner::clearBusy() {
    if (m_busy && !m_tab.isNull()) {
        m_tab->clearBusy(m_owner);
    }
    m_busy = false;
}

} // namespace hwio::gui
