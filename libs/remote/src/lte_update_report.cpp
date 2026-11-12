#include "hwio/remote/lte_update_report.h"

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/lte_update_procedure.h"
#include "hwio/device/operation.h"
#include "hwio/remote/host_operations.h"

#include <QObject>
#include <QTimer>

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
    if (!m_procedure.isNull()) {
        m_procedure->disconnect(this);
    }
}

} // namespace hwio::remote
