#include "hwio/remote/lte_update_report.h"

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/lte_update_procedure.h"
#include "hwio/device/operation.h"
#include "hwio/remote/consent.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/tunnel.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QtCore/qtmetamacros.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

namespace hwio::remote {

namespace {

using Step = device::LteUpdateProcedure::Step;

// The steps that run an operation with progress (the others wait for the customer).
bool working(Step step) {
    return step == Step::Backup || step == Step::Erase || step == Step::UpdateLte ||
           step == Step::Restore;
}

} // namespace

LteUpdateReporter::LteUpdateReporter(LteUpdateRunner::Progress progress,
                                     LteUpdateRunner::Finished finished,
                                     std::chrono::milliseconds heartbeat, QObject* parent)
    : QObject(parent), m_progress(std::move(progress)), m_finished(std::move(finished)) {
    m_heartbeat.setInterval(heartbeat);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        if (!m_done && m_progress) {
            m_progress(m_last);
        }
    });
    m_heartbeat.start();
}

void LteUpdateReporter::follow(device::LteUpdateProcedure& procedure) {
    if (!m_procedure.isNull()) {
        m_procedure->disconnect(this);
    }
    m_procedure = &procedure;
    connect(&procedure, &device::LteUpdateProcedure::changed, this, &LteUpdateReporter::onChanged);
    connect(&procedure, &device::LteUpdateProcedure::progressChanged, this,
            &LteUpdateReporter::onProgress);
    connect(&procedure, &device::LteUpdateProcedure::finished, this,
            &LteUpdateReporter::onFinished);
    onChanged();
}

void LteUpdateReporter::download(std::uint64_t done, std::uint64_t total) {
    report(
        OperationUpdate{.phase = "download", .step = 1, .steps = 1, .done = done, .total = total});
}

void LteUpdateReporter::onChanged() {
    if (m_procedure.isNull() || m_done) {
        return;
    }
    const Step step = m_procedure->step();
    if (step == Step::ConnectAppAgain || step == Step::ChooseApplication || step == Step::Restore ||
        step == Step::Done) {
        m_lteUpdated = true; // reached only after a successful LTE update
    }
    if (step == Step::Idle || step == Step::Done || step == Step::Failed ||
        step == Step::Cancelled) {
        return;
    }
    if (step == Step::Confirm || step == Step::BackupFailed) {
        askSupport(step); // ADR-0368
    }
    if (!working(step)) {
        report(OperationUpdate{.phase = std::string(device::LteUpdateProcedure::stepId(step)),
                               .step = 1,
                               .steps = 1,
                               .done = 0,
                               .total = 0});
        return;
    }
    onProgress();
}

void LteUpdateReporter::onProgress() {
    if (m_procedure.isNull() || m_done || !working(m_procedure->step())) {
        return;
    }
    const device::OperationProgress& progress = m_procedure->progress();
    const int steps = std::clamp(progress.steps, 1, 16);
    report(OperationUpdate{.phase =
                               std::string(device::LteUpdateProcedure::stepId(m_procedure->step())),
                           .step = std::clamp(progress.step, 1, steps),
                           .steps = steps,
                           .done = std::min(progress.done, progress.total),
                           .total = progress.total});
}

void LteUpdateReporter::onFinished() {
    if (m_procedure.isNull() || m_done) {
        return;
    }
    // A failure the GUI wizard can retry stays open until the wizard closes (finish()).
    if (m_procedure->step() == Step::Failed && m_procedure->canRetry()) {
        report(OperationUpdate{.phase = "failed", .step = 1, .steps = 1, .done = 0, .total = 0});
        return;
    }
    finish();
}

void LteUpdateReporter::askSupport(Step step) {
    if (!m_ask) {
        return;
    }
    bool& asked = step == Step::Confirm ? m_askedContinue : m_askedCopy;
    if (asked) {
        return;
    }
    asked = true;
    m_questionOpen = true;
    const auto kind = step == Step::Confirm ? tunnel::OpQuestionKind::Continue
                                            : tunnel::OpQuestionKind::ContinueWithoutCopy;
    // The answer is applied on the next turn of the event loop (never inside the procedure's
    // signal or the host's call).
    m_ask(kind, [self = QPointer<LteUpdateReporter>(this), step](SupportAnswer answer) {
        if (self.isNull()) {
            return;
        }
        QTimer::singleShot(0, self.data(), [self, step, answer] {
            if (self.isNull()) {
                return;
            }
            self->m_questionOpen = false;
            device::LteUpdateProcedure* procedure = self->m_procedure.data();
            if (procedure == nullptr || procedure->step() != step) {
                return;
            }
            if (answer != SupportAnswer::Yes) {
                self->m_cancelledBy = answer;
                (void)procedure->cancel(); // nothing was changed yet
            } else if (step == Step::Confirm) {
                procedure->confirm();
            } else {
                procedure->continueWithoutBackup();
            }
            emit self->supportQuestionChanged();
        });
    });
    emit supportQuestionChanged(); // the question is open: the customer's view changes
}

QString LteUpdateReporter::cancelledText() const {
    if (!m_cancelledBy) {
        return {};
    }
    return *m_cancelledBy == SupportAnswer::No ? cancelledBySupportText()
                                               : supportDidNotAnswerText();
}

void LteUpdateReporter::report(const OperationUpdate& update) {
    if (m_done) {
        return;
    }
    m_last = update;
    m_heartbeat.start(); // the next heartbeat after a quiet interval
    if (m_progress) {
        m_progress(update);
    }
}

Status LteUpdateReporter::result() const {
    if (m_procedure.isNull()) {
        return std::unexpected(makeError(ErrorCode::Cancelled, "the guided LTE update ended"));
    }
    switch (m_procedure->step()) {
    case Step::Done:
        return {};
    case Step::Failed:
        return std::unexpected(m_procedure->error().value_or(
            makeError(ErrorCode::InternalUnexpected, "the guided LTE update failed")));
    default:
        break;
    }
    if (m_lteUpdated) {
        return std::unexpected(makeError(ErrorCode::RemoteApplicationNotRestored,
                                         "the LTE MCU was updated, the application not restored"));
    }
    return std::unexpected(makeError(ErrorCode::Cancelled, "the guided LTE update was cancelled"));
}

void LteUpdateReporter::finish() { finish(result()); }

void LteUpdateReporter::finish(Status status) {
    if (m_done) {
        return;
    }
    m_done = true;
    m_heartbeat.stop();
    m_progress = {};
    if (LteUpdateRunner::Finished callback = std::exchange(m_finished, {})) {
        callback(std::move(status));
    }
}

void LteUpdateReporter::abandon() {
    m_done = true;
    m_heartbeat.stop();
    m_progress = {};
    m_finished = {};
    // ADR-0368: the host goes away (it answered an open question no first); a later question is
    // the customer's again.
    m_ask = {};
    if (!m_procedure.isNull()) {
        m_procedure->disconnect(this);
    }
    emit supportQuestionChanged();
}

} // namespace hwio::remote
