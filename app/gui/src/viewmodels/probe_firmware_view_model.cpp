#include "viewmodels/probe_firmware_view_model.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/probe_tools.h"
#include "hwio/device/session_registry.h"
#include "hwio/hw/jlink/library_locator.h"
#include "hwio/hw/jlink/types.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "workers/probe_firmware_worker.h"

#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtTypes>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace hwio::gui {

ProbeFirmwareViewModel::ProbeFirmwareViewModel(ProbeFirmwareContext context, QObject* parent)
    : QObject(parent), m_context(std::move(context)),
      m_worker(std::make_unique<ProbeFirmwareWorker>(
          m_context.tools,
          [this](ProbeFirmwareWorker::Job job, ProbeFirmwareWorker::Outcome outcome) {
              onJobFinished(job, std::move(outcome));
          })) {}

ProbeFirmwareViewModel::~ProbeFirmwareViewModel() {
    // The job first (it is never interrupted), then the reservation and the scanner suspension.
    m_worker.reset();
    release();
}

QString ProbeFirmwareViewModel::serial() const {
    return m_serial ? QString::number(*m_serial) : QString();
}

QString ProbeFirmwareViewModel::blockedReason(std::uint32_t serial,
                                              const DeviceTabViewModel* requester) const {
    if (busy()) {
        return tr("Another J-Link update is running.");
    }
    if (m_aiHoldsProbe && m_aiHoldsProbe(serial)) {
        return tr("Used by an AI assistant");
    }
    const QList<DeviceTabViewModel*> tabs =
        m_context.tabs ? m_context.tabs() : QList<DeviceTabViewModel*>();
    for (qsizetype i = 0; i < tabs.size(); ++i) {
        const DeviceTabViewModel* tab = tabs.at(i);
        if (tab == nullptr || tab->choosing() || tab->isBle() || tab->probeSerial() != serial) {
            continue;
        }
        if (tab->isBusy()) {
            return tr("Wait for the current operation to finish.");
        }
        if (tab != requester && tab->session() != nullptr) {
            return tr("Disconnect tab %1 first.").arg(i + 1);
        }
    }
    return {};
}

QString ProbeFirmwareViewModel::blockedReasonFor(const QString& serial) const {
    bool ok = false;
    const std::uint32_t value = serial.toUInt(&ok);
    return ok ? blockedReason(value) : QString();
}

void ProbeFirmwareViewModel::requestFromTab(DeviceTabViewModel& tab) {
    const std::optional<std::uint32_t> serial = tab.probeSerial();
    if (!serial || tab.isBle()) {
        return;
    }
    if (const QString reason = blockedReason(*serial, &tab); !reason.isEmpty()) {
        qCInfo(lcGui) << "J-Link firmware update refused:" << reason;
        return;
    }
    // ADR-0246: the tab disconnects first (as the device bar's Disconnect; its log file closes).
    const QString product = tab.product();
    tab.disconnectDevice();
    m_tab = &tab;
    start(*serial, product);
}

void ProbeFirmwareViewModel::requestForSerial(const QString& serial) {
    bool ok = false;
    const std::uint32_t value = serial.toUInt(&ok);
    if (!ok) {
        return;
    }
    if (const QString reason = blockedReason(value); !reason.isEmpty()) {
        toast(reason);
        return;
    }
    m_tab.clear(); // ADR-0256: nothing connects after an update from a wizard
    start(value, QString());
}

void ProbeFirmwareViewModel::start(std::uint32_t serial, const QString& product) {
    m_serial = serial;
    m_product = product;
    m_current.clear();
    m_available.clear();
    m_error = ErrorInfo();
    m_discard = false;
    m_statusUnknown = false;
    if (m_context.registry == nullptr) {
        fail(makeError(ErrorCode::InternalUnexpected, "no session registry"));
        return;
    }
    if (const Status available = m_context.registry->checkAvailable(serial); !available) {
        fail(available.error());
        return;
    }
    const jlink::LocatorOptions options =
        m_context.locatorOptions ? m_context.locatorOptions() : jlink::LocatorOptions{};
    auto location = jlink::locateLibrary(options);
    if (!location) {
        fail(location.error());
        return;
    }
    m_location = std::move(*location);
    m_reservation = m_context.registry->reserve(serial);
    if (!m_reservation.valid()) {
        fail(makeError(ErrorCode::InternalBusy, "the probe could not be reserved"));
        return;
    }
    if (m_context.scanner != nullptr) {
        m_suspension = m_context.scanner->suspend();
    }
    const bool started =
        m_worker->start(ProbeFirmwareWorker::Request{.job = ProbeFirmwareWorker::Job::Inspect,
                                                     .options = m_context.registry->options(),
                                                     .location = m_location,
                                                     .serialNumber = serial});
    if (!started) {
        fail(makeError(ErrorCode::InternalBusy, "a probe firmware job is running"));
        return;
    }
    setState(Reading);
}

void ProbeFirmwareViewModel::confirm() {
    if (m_state != Confirm || !m_serial || m_context.registry == nullptr) {
        return;
    }
    const bool started =
        m_worker->start(ProbeFirmwareWorker::Request{.job = ProbeFirmwareWorker::Job::Update,
                                                     .options = m_context.registry->options(),
                                                     .location = m_location,
                                                     .serialNumber = *m_serial});
    if (!started) {
        fail(makeError(ErrorCode::InternalBusy, "a probe firmware job is running"));
        return;
    }
    qCInfo(lcGui) << "J-Link firmware update started:" << *m_serial;
    setState(Updating);
}

void ProbeFirmwareViewModel::cancel() {
    switch (m_state) {
    case Reading:
        // The inspect job cannot be interrupted (≤ 5 s): its result is dropped; the reservation is
        // released when it arrives.
        m_discard = true;
        m_tab.clear();
        setState(Idle);
        finished(false); // signal
        break;
    case Confirm:
        m_tab.clear(); // Cancel: the tab stays disconnected (ADR-0256)
        release();
        setState(Idle);
        finished(false); // signal
        break;
    case Failed:
        m_tab.clear();
        setState(Idle);
        finished(false); // signal
        break;
    case Idle:
    case Updating: // the update must not be interrupted
        break;
    }
}

void ProbeFirmwareViewModel::onJobFinished(ProbeFirmwareWorker::Job job,
                                           ProbeFirmwareWorker::Outcome outcome) {
    if (m_discard) {
        m_discard = false;
        release();
        changed(); // signal (busy() changed)
        return;
    }
    if (job == ProbeFirmwareWorker::Job::Inspect) {
        auto* details = std::get_if<Result<device::ProbeDetails>>(&outcome);
        if (details == nullptr || !*details) {
            fail(details != nullptr ? details->error()
                                    : makeError(ErrorCode::InternalUnexpected, "no result"));
            return;
        }
        const device::ProbeDetails& probe = **details;
        if (!probe.probe.product.empty()) {
            m_product = toQString(probe.probe.product);
        }
        m_current = toQString(probe.probe.firmware);
        m_available = toQString(probe.libraryFirmware);
        switch (probe.firmwareStatus) {
        case jlink::FirmwareStatus::Current:
            finish(false, true, tr("The J-Link firmware is already up to date."));
            return;
        case jlink::FirmwareStatus::Newer:
            // ADR-0132: no downgrade from the GUI.
            finish(false, true, tr("The J-Link firmware is newer than the J-Link library."));
            return;
        case jlink::FirmwareStatus::Outdated:
        case jlink::FirmwareStatus::Unknown:
            // ADR-0261: Unknown asks explicitly ("Update anyway?").
            m_statusUnknown = probe.firmwareStatus == jlink::FirmwareStatus::Unknown;
            setState(Confirm);
            return;
        }
        return;
    }
    auto* update = std::get_if<Result<device::FirmwareUpdate>>(&outcome);
    if (update == nullptr || !*update) {
        const Error error = update != nullptr
                                ? update->error()
                                : makeError(ErrorCode::InternalUnexpected, "no result");
        qCWarning(lcGui) << "J-Link firmware update failed:" << toQString(idOf(error.code))
                         << toQString(error.detail);
        fail(error);
        return;
    }
    if ((*update)->before == jlink::FirmwareStatus::Current) {
        finish(false, true, tr("The J-Link firmware is already up to date."));
        return;
    }
    qCInfo(lcGui) << "J-Link firmware updated:" << (*update)->serialNumber;
    finish(true, true, tr("J-Link firmware updated."));
}

void ProbeFirmwareViewModel::fail(const Error& error) {
    if (m_quitPending) {
        // ADR-0261: the application stays open and shows the error with its advice.
        m_quitPending = false;
        quitCancelled(); // signal (before changed(): AppViewModel must not quit)
    }
    m_tab.clear();
    m_error = ErrorInfo(error);
    release();
    setState(Failed);
}

void ProbeFirmwareViewModel::finish(bool updated, bool reconnect, const QString& toast) {
    const QPointer<DeviceTabViewModel> tab = std::exchange(m_tab, {});
    release();
    this->toast(toast);
    setState(Idle);
    finished(updated); // signal
    // ADR-0256: the banner's tab connects again to the same probe (it stays on its page when
    // the probe is gone: the connect error is shown there). Not while quitting.
    if (reconnect && !tab.isNull() && !m_quitPending) {
        tab->connectDevice();
    }
}

void ProbeFirmwareViewModel::release() {
    m_reservation.release();
    m_suspension.reset();
}

void ProbeFirmwareViewModel::setState(State state) {
    m_state = state;
    changed(); // signal
}

void ProbeFirmwareViewModel::toast(const QString& text) const {
    if (m_context.showToast && !text.isEmpty()) {
        m_context.showToast(text);
    }
}

void ProbeFirmwareViewModel::notNow(std::uint32_t serial) {
    if (isNotNow(serial)) {
        return;
    }
    if (m_notNow.size() >= maxNotNow) {
        m_notNow.erase(m_notNow.begin()); // bounded: drop the oldest
    }
    m_notNow.push_back(serial);
}

bool ProbeFirmwareViewModel::isNotNow(std::uint32_t serial) const {
    return std::ranges::find(m_notNow, serial) != m_notNow.end();
}

void ProbeFirmwareViewModel::setAiHoldsProbe(std::function<bool(std::uint32_t)> holds) {
    m_aiHoldsProbe = std::move(holds);
    changed(); // signal (blocked reasons)
}

void ProbeFirmwareViewModel::setQuitPending(bool pending) {
    if (pending != m_quitPending) {
        m_quitPending = pending;
        changed(); // signal
    }
}

} // namespace hwio::gui
