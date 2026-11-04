#include "viewmodels/flash_view_model.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/program_progress.h"
#include "pages/firmware_services.h"
#include "settings_store.h"
#include "viewmodels/dependency_status.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/firmware_source_view_model.h"
#include "viewmodels/settings_view_model.h"
#include "workers/firmware_loader.h"

#include <QLocale>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hwio::gui {

namespace {

using device::OperationKind;
using firmware::ProgramPhase;
using Kind = FirmwareSourceViewModel::Kind;

bool isFlashKind(OperationKind kind) {
    return kind == OperationKind::Flash || kind == OperationKind::LteBundle ||
           kind == OperationKind::Modem;
}

QString secondsText(std::chrono::milliseconds duration) {
    const double seconds = static_cast<double>(duration.count()) / 1000.0;
    return seconds < 10.0 ? QLocale().toString(seconds, 'f', 1)
                          : QLocale().toString(static_cast<qlonglong>(std::lround(seconds)));
}

QString elapsedText(std::chrono::milliseconds elapsed) {
    const auto total = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString partNames(const device::TargetProfile& target) {
    QStringList names;
    for (const device::ChipPart& part : target.parts) {
        names.append(toQString(part.displayName));
    }
    return names.join(QStringLiteral(" / "));
}

} // namespace

FlashViewModel::FlashViewModel(DeviceTabViewModel* tab, FirmwareServices& services)
    : QObject(tab), m_tab(tab), m_services(services),
      // Qt parent ownership: this object.
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      m_source(new FirmwareSourceViewModel(
          services,
          [tab]() -> const device::DeviceProfile* {
              return device::ProfileRegistry::find(toStdString(tab->profileId()));
          },
          this)) {
    connect(m_source, &FirmwareSourceViewModel::sourceChanged, this, [this] {
        if (needsNrfUtil()) {
            m_services.dependencies()->ensureChecked();
        }
        stateChanged(); // signal
    });
    connect(m_source, &FirmwareSourceViewModel::allowUicrChanged, this,
            &FlashViewModel::stateChanged);
    connect(m_source, &FirmwareSourceViewModel::kindSuggestionAccepted, this,
            &FlashViewModel::setKind);
    connect(m_tab, &DeviceTabViewModel::sessionChanged, this, &FlashViewModel::onSessionChanged);
    connect(m_tab, &DeviceTabViewModel::busyChanged, this, &FlashViewModel::stateChanged);
    connect(m_tab, &DeviceTabViewModel::stateChanged, this, &FlashViewModel::stateChanged);
    connect(m_tab, &DeviceTabViewModel::configurationChanged, this, [this] {
        applyDefaultKind();
        // Another device: the maintenance MCU goes back to the console target and a revealed
        // [Recover…] is hidden again (plan 0033).
        m_maintenanceTarget = m_tab->consoleTargetId();
        m_revealedRecover.clear();
        kindChanged();        // signal
        textsChanged();       // signal: the MCU names of the profile
        maintenanceChanged(); // signal
        stateChanged();       // signal
    });
    if (SettingsStore* store = m_services.settings(); store != nullptr) {
        connect(store, &SettingsStore::valueChanged, this, [this](const QString& key) {
            if (key == FirmwareServices::expertActionsKey) {
                onExpertActionsChanged();
            }
        });
    }
    connect(m_services.dependencies(), &DependencyStatus::nrfUtilChanged, this,
            &FlashViewModel::stateChanged);
    // 0021 (ADR-0166): "In the device" and, after an application flash, "Now in the device".
    connect(m_tab, &DeviceTabViewModel::deviceInfoChanged, this,
            &FlashViewModel::onDeviceInfoChanged);
    m_deviceInfoDeadline.setSingleShot(true);
    m_deviceInfoDeadline.setInterval(deviceInfoWindow);
    connect(&m_deviceInfoDeadline, &QTimer::timeout, this, [this] { m_deviceInfoRetry.stop(); });
    m_deviceInfoRetry.setInterval(deviceInfoRetryInterval);
    connect(&m_deviceInfoRetry, &QTimer::timeout, this, [this] { m_tab->refreshDeviceInfo(); });
    connect(m_tab, &DeviceTabViewModel::consoleAttached, this, [this] {
        if (std::exchange(m_awaitingAttach, false)) {
            startDeviceInfoWindow();
        }
    });
    connect(&m_services, &FirmwareServices::languageChanged, this, &FlashViewModel::retranslate);

    applyDefaultKind();
    m_source->setKind(m_kind);
    m_maintenanceTarget = m_tab->consoleTargetId();
    onSessionChanged();
}

FlashViewModel::~FlashViewModel() {
    for (const QMetaObject::Connection& connection : m_sessionConnections) {
        disconnect(connection);
    }
}

// ---- kind ---------------------------------------------------------------------------------------

void FlashViewModel::setKind(int kind) {
    if (kind < FirmwareSourceViewModel::AppFirmware ||
        kind > FirmwareSourceViewModel::LteFirmware || m_running) {
        return;
    }
    m_kindChosen = true;
    if (static_cast<Kind>(kind) == m_kind) {
        return;
    }
    m_kind = static_cast<Kind>(kind);
    m_source->setKind(m_kind);
    if (needsNrfUtil()) {
        m_services.dependencies()->ensureChecked();
    }
    kindChanged();  // signal
    stateChanged(); // signal
}

void FlashViewModel::applyDefaultKind() {
    if (m_kindChosen || m_tab->choosing()) {
        return;
    }
    // ADR-0233: a tab always works with the console target (the application MCU), so the default
    // kind is application firmware; a kind chosen in this tab is kept for the tab's lifetime (in
    // memory only, user decision 2026-10-16).
    m_kind = FirmwareSourceViewModel::AppFirmware;
    m_source->setKind(m_kind);
}

QString FlashViewModel::targetId() const { return FirmwareSourceViewModel::targetIdFor(m_kind); }

const device::DeviceProfile* FlashViewModel::profile() const {
    return device::ProfileRegistry::find(toStdString(m_tab->profileId()));
}

const device::TargetProfile* FlashViewModel::targetProfile(const QString& id) const {
    const device::DeviceProfile* deviceProfile = profile();
    return deviceProfile != nullptr ? device::findTarget(*deviceProfile, toStdString(id)) : nullptr;
}

QVariantList FlashViewModel::kinds() const {
    const device::TargetProfile* app = targetProfile(QStringLiteral("app"));
    const device::TargetProfile* lte = targetProfile(QStringLiteral("lte"));
    const auto mcu = [](const QString& title, const device::TargetProfile* target) {
        return target != nullptr && !target->parts.empty()
                   ? QStringLiteral("%1 (%2)").arg(title, partNames(*target))
                   : title;
    };
    return {
        QVariantMap{
            {QStringLiteral("kind"), static_cast<int>(FirmwareSourceViewModel::AppFirmware)},
            {QStringLiteral("title"), tr("Application firmware")},
            {QStringLiteral("mcu"), mcu(tr("Application MCU"), app)},
            {QStringLiteral("accepts"), tr(".hex file or catalog ID")}},
        // 0021 (ADR-0174): one LTE card for the LTE MCU application, the modem and the bundle.
        QVariantMap{
            {QStringLiteral("kind"), static_cast<int>(FirmwareSourceViewModel::LteFirmware)},
            {QStringLiteral("title"), tr("LTE firmware")},
            {QStringLiteral("mcu"), tr("%1 + modem").arg(mcu(tr("LTE MCU"), lte))},
            {QStringLiteral("accepts"), tr("The HARDWARIO LTE firmware or your own .hex / .zip")}},
    };
}

// ---- prerequisites and the Flash bar
// ----------------------------------------------------------------

bool FlashViewModel::needsNrfUtil() const {
    if (m_kind != FirmwareSourceViewModel::LteFirmware) {
        return false;
    }
    // 0021 (ADR-0174): whenever the LTE source contains modem firmware - the designated bundle
    // always does.
    if (m_source->designatedActive()) {
        return true;
    }
    const LoadedSource* loaded = m_source->loadedSource();
    return loaded != nullptr && (loaded->sourceClass == SourceClass::LteBundle ||
                                 loaded->sourceClass == SourceClass::ModemZip);
}

bool FlashViewModel::haltAvailable() const {
    // Not for a modem-only file (nRF Util programs the modem; nothing to halt).
    const LoadedSource* loaded = m_source->loadedSource();
    return m_kind == FirmwareSourceViewModel::AppFirmware || m_source->designatedActive() ||
           loaded == nullptr || loaded->sourceClass != SourceClass::ModemZip;
}

QString FlashViewModel::prerequisiteText() const {
    if (!needsNrfUtil()) {
        return {};
    }
    const std::optional<Error>& problem = m_services.dependencies()->nrfUtilProblem();
    if (!problem) {
        return {};
    }
    if (problem->code == ErrorCode::DependencyNrfUtilNotFound) {
        return tr("nRF Util was not found. It is needed for LTE firmware.");
    }
    return toQString(problem->message) + QLatin1Char(' ') + toQString(problem->suggestion);
}

void FlashViewModel::setHalt(bool halt) {
    if (halt == m_halt) {
        return;
    }
    m_halt = halt;
    haltChanged(); // signal
}

bool FlashViewModel::otherOperationRunning() const {
    if (m_tab->isBusy() && m_tab->busyPage() != pageId) {
        return true;
    }
    return !m_running && !m_session.isNull() && m_session->runningOperation().has_value();
}

QString FlashViewModel::cannotFlashReason() const {
    if (m_tab->choosing()) {
        return tr("Choose a device first");
    }
    if (m_kind == FirmwareSourceViewModel::LteFirmware) {
        // ADR-0234: the LTE MCU is updated by the guided procedure only.
        return tr("Use Update LTE firmware… above: the J-Link has to be moved between the "
                  "connectors.");
    }
    if (m_running || otherOperationRunning()) {
        return tr("Another operation is running");
    }
    switch (static_cast<FirmwareSourceViewModel::SourceState>(m_source->sourceState())) {
    case FirmwareSourceViewModel::None:
        if (m_source->designatedActive()) {
            return tr("The LTE firmware is not downloaded yet");
        }
        if (!m_source->catalogAvailable()) {
            return tr("Choose a firmware file");
        }
        return tr("Choose a firmware file or enter a catalog ID");
    case FirmwareSourceViewModel::Loading:
        return tr("Loading the firmware file…");
    case FirmwareSourceViewModel::Downloading:
        return tr("Downloading the firmware…");
    case FirmwareSourceViewModel::Invalid:
        if (m_source->designatedActive()) {
            return tr("The LTE firmware could not be downloaded");
        }
        return m_source->suggestedKind() >= 0 ? tr("The file does not match the chosen kind")
                                              : tr("The firmware cannot be used");
    case FirmwareSourceViewModel::Ready:
        break;
    }
    if (needsNrfUtil()) {
        if (const std::optional<Error>& problem = m_services.dependencies()->nrfUtilProblem()) {
            return problem->code == ErrorCode::DependencyNrfUtilNotFound
                       ? tr("nRF Util was not found")
                       : toQString(problem->message);
        }
    }
    if (m_source->hasUicr() && !m_source->allowUicr()) {
        return tr("The firmware writes the UICR: allow it in Options");
    }
    return {};
}

QString FlashViewModel::flashSummary() const {
    const QString kindTitle = kinds().at(m_kind).toMap().value(QStringLiteral("title")).toString();
    return m_source->sourceState() == FirmwareSourceViewModel::Ready
               ? QStringLiteral("%1 · %2").arg(kindTitle, m_source->sourceName())
               : kindTitle;
}

// ---- operations
// -----------------------------------------------------------------------------------

void FlashViewModel::flash() {
    if (!canFlash() || m_kind != FirmwareSourceViewModel::AppFirmware) {
        return;
    }
    const LoadedSource* loaded = m_source->readySource();
    if (loaded == nullptr) {
        return;
    }
    // 0021 (ADR-0174): the operation follows the classification of the input (ADR-0040).
    OperationKind kind = OperationKind::Flash;
    if (loaded->sourceClass == SourceClass::ModemZip) {
        kind = OperationKind::Modem;
    } else if (loaded->sourceClass == SourceClass::LteBundle) {
        kind = OperationKind::LteBundle;
    }
    runFlash(FlashRequest{
        .kind = kind,
        .targetId = targetId(),
        .source = *loaded,
        .options = device::FlashOptions{.halt = haltAvailable() && m_halt,
                                        .allowUicr = m_source->hasUicr() && m_source->allowUicr()},
        .sourceName = m_source->sourceName()});
}

void FlashViewModel::runFlash(const FlashRequest& request) {
    m_lastFlash = request;
    m_lastSourceName = request.sourceName;
    const std::string targetText = toStdString(request.targetId);
    const device::FlashOptions options = request.options;
    if (request.kind == OperationKind::Modem) {
        start(OperationKind::Modem, request.targetId,
              [targetText, zip = request.source.path](device::DeviceSession& session) {
                  return session.updateModem(targetText, zip);
              });
    } else if (request.kind == OperationKind::LteBundle) {
        start(
            OperationKind::LteBundle, request.targetId,
            [targetText, bundle = request.source.bundle, options](device::DeviceSession& session) {
                return session.flashLteBundle(targetText, bundle, options);
            });
    } else {
        start(OperationKind::Flash, request.targetId,
              [targetText, image = request.source.image, options](device::DeviceSession& session) {
                  return session.flash(targetText, image, options);
              });
    }
}

void FlashViewModel::reset(bool halt) {
    if (!canRunMaintenance()) {
        return;
    }
    const std::string target = toStdString(m_maintenanceTarget);
    m_resetHalt = halt;
    start(OperationKind::Reset, m_maintenanceTarget,
          [target, halt](device::DeviceSession& session) {
              return session.reset(target, halt ? device::ResetMode::Halt : device::ResetMode::Go);
          });
}

void FlashViewModel::eraseCode() {
    if (!canRunMaintenance() || !eraseCodeAvailable()) {
        return;
    }
    const std::string target = toStdString(m_maintenanceTarget);
    start(OperationKind::EraseCode, m_maintenanceTarget, [target](device::DeviceSession& session) {
        return session.erase(target, device::EraseScope::Code);
    });
}

void FlashViewModel::eraseAll() { runEraseAll(m_maintenanceTarget); }

void FlashViewModel::recover() { runRecover(m_maintenanceTarget); }

void FlashViewModel::runEraseAll(const QString& target) {
    if (!canRunMaintenance() || !eraseAllAvailable()) {
        return;
    }
    start(OperationKind::EraseAll, target,
          [targetText = toStdString(target)](device::DeviceSession& session) {
              return session.erase(targetText, device::EraseScope::All);
          });
}

void FlashViewModel::runRecover(const QString& target) {
    if (!canRunMaintenance() || !recoverAvailable()) {
        return;
    }
    if (target == m_revealedRecover) {
        m_revealedRecover.clear(); // shown "this once" (ADR-0236)
        maintenanceChanged();      // signal
    }
    start(OperationKind::Recover, target,
          [targetText = toStdString(target)](device::DeviceSession& session) {
              return session.recover(targetText);
          });
}

bool FlashViewModel::eraseCodeAvailable() const {
    // The profile's default erase (CHESTER: app Code, lte All; as `hwio chester lte erase`).
    const device::TargetProfile* target = targetProfile(m_maintenanceTarget);
    return target == nullptr || target->defaultErase == device::EraseScope::Code;
}

bool FlashViewModel::canRunMaintenance() const {
    return !m_tab->choosing() && !m_running && !otherOperationRunning();
}

// ---- plan 0033: expert maintenance actions (ADR-0236) and the LTE update (ADR-0234) ------------

bool FlashViewModel::expertActions() const {
    const SettingsStore* store = m_services.settings();
    return store != nullptr && store->value(FirmwareServices::expertActionsKey).toBool();
}

void FlashViewModel::onExpertActionsChanged() {
    // Without the expert actions only the console target and a revealed MCU remain choosable.
    if (!expertActions() && m_maintenanceTarget != m_tab->consoleTargetId() &&
        m_maintenanceTarget != m_revealedRecover) {
        m_maintenanceTarget = m_tab->consoleTargetId();
    }
    maintenanceChanged(); // signal
}

void FlashViewModel::setMaintenanceTarget(const QString& targetId) {
    if (targetId == m_maintenanceTarget || m_running || targetProfile(targetId) == nullptr) {
        return;
    }
    if (!expertActions() && targetId != m_tab->consoleTargetId()) {
        return; // the LTE MCU maintenance is an expert action
    }
    m_maintenanceTarget = targetId;
    m_pending.reset();    // a confirmation names the MCU it was opened for
    maintenanceChanged(); // signal
}

QVariantList FlashViewModel::maintenanceTargets() const {
    QVariantList list;
    const device::DeviceProfile* deviceProfile = profile();
    if (deviceProfile == nullptr || !expertActions()) {
        return list;
    }
    for (const device::TargetProfile& target : deviceProfile->targets) {
        list.append(QVariantMap{
            {QStringLiteral("targetId"), toQString(target.id)},
            {QStringLiteral("title"), tr("%1 MCU").arg(device::targetDisplayName(target))}});
    }
    return list;
}

bool FlashViewModel::recoverAvailable() const {
    return expertActions() ||
           (!m_revealedRecover.isEmpty() && m_revealedRecover == m_maintenanceTarget);
}

void FlashViewModel::setMaintenanceExpanded(bool expanded) {
    if (expanded == m_maintenanceExpanded) {
        return;
    }
    m_maintenanceExpanded = expanded;
    maintenanceChanged(); // signal
}

void FlashViewModel::revealRecover(const QString& targetId) {
    if (m_running || targetProfile(targetId) == nullptr) {
        return;
    }
    qCInfo(lcGui).noquote() << "Flash page: Recover shown once for" << targetId;
    m_revealedRecover = targetId;
    m_maintenanceTarget = targetId;
    m_maintenanceExpanded = true;
    m_pending.reset();
    maintenanceChanged(); // signal
}

void FlashViewModel::requestLteUpdate() {
    if (m_tab->choosing() || m_running || otherOperationRunning()) {
        return;
    }
    lteUpdateRequested(); // signal
    m_tab->openWizard(QStringLiteral("lte-update"));
}

void FlashViewModel::start(
    OperationKind kind, const QString& target,
    const std::function<Result<device::OperationId>(device::DeviceSession&)>& run) {
    if (m_running) {
        return;
    }
    m_lastKind = kind;
    m_lastTarget = target;
    m_error.reset();
    m_chip.reset();
    stopDeviceInfoWatch();
    m_cancelRequested = false;
    // A tab without a working session opens one on its console target (review S2); the operation
    // carries its own target (ADR-0057). sessionChanged may be emitted inside this call.
    if (Status opened = m_tab->ensureSession(); !opened) {
        finish(kind, Status(std::unexpected(std::move(opened).error())), {}, std::nullopt);
        return;
    }
    device::DeviceSession* session = m_tab->session();
    if (session == nullptr) {
        finish(kind,
               Status(std::unexpected(
                   makeError(ErrorCode::InternalUnexpected, "no session after ensureSession"))),
               {}, std::nullopt);
        return;
    }
    Result<device::OperationId> id = run(*session);
    if (!id) {
        finish(kind, Status(std::unexpected(std::move(id).error())), {}, std::nullopt);
        return;
    }
    const device::TargetProfile* targetInfo = targetProfile(target);
    const bool crossTarget = target != m_tab->consoleTargetId();
    const bool recoverImage = kind == OperationKind::Recover && targetInfo != nullptr &&
                              targetInfo->recoverImage != device::RecoverImage::None;
    m_running = RunningOperation{.id = *id,
                                 .kind = kind,
                                 .targetId = target,
                                 .sourceName = m_lastSourceName,
                                 .session = session};
    m_plannedPhases = device::plannedPhases(kind, crossTarget, recoverImage);
    m_currentPhase = 0;
    m_phase = m_plannedPhases.empty() ? ProgramPhase::Connecting : m_plannedPhases.front();
    m_step = 1;
    m_steps = kind == OperationKind::LteBundle ? 2 : 1;
    m_done = 0;
    m_total = 0;
    m_elapsed = {};
    m_opState = Running;
    qCInfo(lcGui).noquote() << "Flash page: operation" << toQString(device::operationKindId(kind))
                            << "on" << target << "started";
    m_tab->setBusy(pageId, busyText(kind),
                   isFlashKind(kind)
                       ? tr("Flashing is in progress. Cancel it and close the tab?")
                       : tr("An operation is running on this device. Cancel it and close the tab?"),
                   [this] { cancel(); });
    progressChanged();              // signal
    stateChanged();                 // signal
    announcement(operationTitle()); // signal
}

void FlashViewModel::cancel() {
    if (!m_running || m_opState != Running) {
        return;
    }
    m_cancelRequested = true;
    if (m_running->session.isNull()) {
        finish(m_running->kind,
               Status(std::unexpected(makeError(ErrorCode::Cancelled, "the session is gone"))), {},
               std::nullopt);
        return;
    }
    if (const Status status = m_running->session->cancel(m_running->id); !status) {
        qCInfo(lcGui).noquote() << "Flash page: cancel refused:"
                                << toQString(status.error().detail);
    }
    m_opState = Cancelling;
    stateChanged(); // signal
}

void FlashViewModel::onSessionChanged() {
    for (const QMetaObject::Connection& connection : m_sessionConnections) {
        disconnect(connection);
    }
    m_sessionConnections.clear();
    m_session = m_tab->session();
    if (!m_session.isNull()) {
        m_sessionConnections.push_back(connect(m_session, &device::DeviceSession::operationProgress,
                                               this, &FlashViewModel::onProgress));
        m_sessionConnections.push_back(connect(m_session, &device::DeviceSession::operationFinished,
                                               this, &FlashViewModel::onFinished));
    }
    if (m_running && m_running->session.data() != m_session.data()) {
        // The session the operation runs on was replaced or closed (should not happen: the tab
        // is busy): report the operation as failed with the session's error.
        const ErrorInfo shown = m_tab->connection()->error();
        Error error = makeError(ErrorCode::TargetNotConnected,
                                "the session was closed while the operation was running");
        if (shown.valid()) {
            error.message = toStdString(shown.message());
            error.cause = toStdString(shown.cause());
            error.suggestion = toStdString(shown.suggestion());
        }
        finish(m_running->kind, Status(std::unexpected(std::move(error))), m_elapsed, std::nullopt);
        return;
    }
    stateChanged(); // signal
}

void FlashViewModel::onProgress(device::OperationId id, const device::OperationProgress& progress) {
    if (!m_running || id != m_running->id) {
        return; // another page's or a wizard's operation
    }
    if (!progress.plannedPhases.empty()) {
        if (progress.plannedPhases.data() != m_plannedPhases.data()) {
            m_currentPhase = 0;
        }
        m_plannedPhases = progress.plannedPhases;
    }
    // Phases only move forward.
    for (std::size_t i = static_cast<std::size_t>(std::max(m_currentPhase, 0));
         i < m_plannedPhases.size(); ++i) {
        if (m_plannedPhases[i] == progress.phase) {
            m_currentPhase = static_cast<int>(i);
            break;
        }
    }
    m_phase = progress.phase;
    m_step = progress.step;
    m_steps = progress.steps;
    m_done = progress.done;
    m_total = progress.total;
    m_elapsed = progress.elapsed;
    progressChanged(); // signal
}

void FlashViewModel::onFinished(device::OperationId id, const device::OperationResult& result) {
    if (!m_running || id != m_running->id) {
        stateChanged(); // signal: another operation ended (Flash may be possible again)
        return;
    }
    finish(m_running->kind, result.status, result.duration, result.chip);
}

void FlashViewModel::finish(OperationKind kind, const Status& status,
                            std::chrono::milliseconds duration,
                            const std::optional<device::ChipInfo>& chip) {
    const bool wasRunning = m_running.has_value();
    m_running.reset();
    m_lastKind = kind;
    m_duration = duration;
    m_chip = chip;
    if (status) {
        m_opState = Succeeded;
        m_error.reset();
    } else if (status.error().code == ErrorCode::Cancelled) {
        m_opState = Cancelled;
        m_error.reset();
    } else {
        m_opState = Failed;
        m_error = status.error();
        qCInfo(lcGui).noquote() << "Flash page: operation failed:"
                                << toQString(idOf(status.error().code))
                                << toQString(status.error().detail);
    }
    if (wasRunning) {
        m_tab->clearBusy(pageId);
    }
    if (m_opState == Succeeded && !m_services.isFlashPageVisible(m_tab)) {
        m_tab->showToast(resultText());
    }
    if (m_opState == Succeeded && kind == OperationKind::Flash &&
        m_lastTarget == QStringLiteral("app")) {
        watchNewDeviceInfo();
    }
    progressChanged(); // signal
    stateChanged();    // signal
    const QString text =
        m_error && resultText().isEmpty() ? toQString(m_error->message) : resultText();
    announcement(text); // signal
}

void FlashViewModel::dismissResult() {
    if (m_running) {
        return;
    }
    m_opState = Idle;
    m_error.reset();
    stopDeviceInfoWatch();
    stateChanged(); // signal
}

// ---- 0021 (ADR-0166): the firmware in the device --------------------------------------------

QString FlashViewModel::deviceFirmwareText() const {
    const QString text = m_tab->deviceInfo().value(QStringLiteral("text")).toString();
    return text.isEmpty() ? QString() : tr("In the device: %1").arg(text);
}

QString FlashViewModel::resultDeviceText() const {
    return m_newDeviceFirmware.isEmpty() ? QString()
                                         : tr("Now in the device: %1").arg(m_newDeviceFirmware);
}

void FlashViewModel::watchNewDeviceInfo() {
    // "Now in the device" when `info show` answers within 10 s after the console re-attaches
    // after a successful application flash (review P9, as the plan says): asked again shortly
    // after (the firmware boots) until then. The session restores the console before the
    // operation ends, so a console that is connected now has re-attached.
    m_newDeviceFirmware.clear();
    if (m_tab->state() == DeviceTabViewModel::Connected) {
        startDeviceInfoWindow();
    } else {
        m_awaitingAttach = true;
    }
}

void FlashViewModel::startDeviceInfoWindow() {
    m_deviceInfoDeadline.start();
    m_deviceInfoRetry.start();
}

void FlashViewModel::stopDeviceInfoWatch() {
    m_awaitingAttach = false;
    m_deviceInfoDeadline.stop();
    m_deviceInfoRetry.stop();
    m_newDeviceFirmware.clear();
}

void FlashViewModel::onDeviceInfoChanged() {
    deviceInfoChanged(); // signal
    if (!m_deviceInfoDeadline.isActive()) {
        return;
    }
    const QString text = m_tab->deviceInfo().value(QStringLiteral("text")).toString();
    if (!text.isEmpty()) {
        m_deviceInfoDeadline.stop();
        m_deviceInfoRetry.stop();
        m_newDeviceFirmware = text;
        stateChanged(); // signal
    }
}

void FlashViewModel::openConsole() {
    dismissResult();
    m_tab->setCurrentPage(QStringLiteral("console"));
}

void FlashViewModel::triggerErrorAction(int action) {
    if (m_running) {
        return;
    }
    const bool flashAgain = flashOperation() && action == error_action::Retry;
    const QString section = error().settingsSection(); // before the result is dismissed
    dismissResult();
    if (action == error_action::ChooseFile) {
        chooseFileRequested(); // signal
    } else if (action == error_action::OpenSettings) {
        settingsRequested(section); // signal
    } else if (flashAgain && m_lastFlash && canRunMaintenance()) {
        // The stored request: a source loaded since then is not flashed by [Try again].
        runFlash(*m_lastFlash);
    }
}

QString FlashViewModel::nrfUtilInstallUrl() { return SettingsViewModel::nrfUtilInstallUrl(); }

// ---- texts
// ----------------------------------------------------------------------------------------

bool FlashViewModel::flashOperation() const { return isFlashKind(m_lastKind); }

QString FlashViewModel::busyText(OperationKind kind) {
    switch (kind) {
    case OperationKind::Flash:
    case OperationKind::LteBundle:
        return tr("Flashing…");
    case OperationKind::Modem:
        return tr("Updating the modem…");
    case OperationKind::EraseCode:
    case OperationKind::EraseAll:
        return tr("Erasing…");
    case OperationKind::Recover:
        return tr("Recovering…");
    case OperationKind::Reset:
        return tr("Resetting…");
    case OperationKind::ReadMemory:
    case OperationKind::Diagnose:
    case OperationKind::Halt:
    case OperationKind::Go:
    case OperationKind::TargetStatus:
    case OperationKind::WriteMemory:
    case OperationKind::ReadRegisters:
    case OperationKind::MemoryZones:
    case OperationKind::WriteFlash:
    case OperationKind::Identify:
        break;
    }
    return tr("Working…");
}

QString FlashViewModel::operationTitle() const {
    const device::TargetProfile* target = targetProfile(m_lastTarget);
    const QString mcu = target != nullptr ? device::targetDisplayName(*target) : m_lastTarget;
    switch (m_lastKind) {
    case OperationKind::Flash:
        return tr("Flashing %1 to the %2 MCU").arg(m_lastSourceName, mcu);
    case OperationKind::LteBundle:
        return tr("Flashing the LTE bundle %1").arg(m_lastSourceName);
    case OperationKind::Modem:
        return tr("Updating the modem firmware with %1").arg(m_lastSourceName);
    case OperationKind::EraseCode:
        return tr("Erasing the application firmware of the %1 MCU").arg(mcu);
    case OperationKind::EraseAll:
        return tr("Erasing everything on the %1 MCU").arg(mcu);
    case OperationKind::Recover:
        return tr("Recovering the %1 MCU").arg(mcu);
    case OperationKind::Reset:
        return tr("Resetting the %1 MCU").arg(mcu);
    case OperationKind::ReadMemory:
    case OperationKind::Diagnose:
    case OperationKind::Halt:
    case OperationKind::Go:
    case OperationKind::TargetStatus:
    case OperationKind::WriteMemory:
    case OperationKind::ReadRegisters:
    case OperationKind::MemoryZones:
    case OperationKind::WriteFlash:
    case OperationKind::Identify:
        break;
    }
    return {};
}

QString FlashViewModel::phaseTitle(ProgramPhase phase) {
    switch (phase) {
    case ProgramPhase::Connecting:
        return tr("Connecting");
    case ProgramPhase::SwitchingTarget:
        return tr("Switching MCU");
    case ProgramPhase::Erasing:
        return tr("Erasing");
    case ProgramPhase::Programming:
        return tr("Programming");
    case ProgramPhase::Verifying:
        return tr("Verifying");
    case ProgramPhase::Modem:
        return tr("Updating the modem");
    case ProgramPhase::Recovering:
        return tr("Recovering");
    case ProgramPhase::Resetting:
        return tr("Resetting");
    case ProgramPhase::Restoring:
        return tr("Restoring console");
    }
    return {};
}

QVariantList FlashViewModel::phases() const {
    QVariantList list;
    for (std::size_t i = 0; i < m_plannedPhases.size(); ++i) {
        const int index = static_cast<int>(i);
        QString state = QStringLiteral("pending");
        if (index < m_currentPhase || m_opState == Succeeded) {
            state = QStringLiteral("done");
        } else if (index == m_currentPhase && m_running) {
            state = QStringLiteral("current");
        }
        list.append(QVariantMap{{QStringLiteral("title"), phaseTitle(m_plannedPhases[i])},
                                {QStringLiteral("state"), state}});
    }
    return list;
}

QString FlashViewModel::stepText() const {
    return m_steps > 1 ? tr("Step %1 of %2").arg(m_step).arg(m_steps) : QString();
}

qreal FlashViewModel::progress() const {
    return m_total > 0 ? static_cast<qreal>(m_done) / static_cast<qreal>(m_total) : -1;
}

QString FlashViewModel::progressText() const {
    QStringList parts;
    if (m_total > 0) {
        if (m_phase != ProgramPhase::Modem) { // nRF Util reports percent, not bytes
            const QLocale locale;
            parts.append(QStringLiteral("%1 / %2").arg(
                locale.formattedDataSize(static_cast<qint64>(m_done), 0, QLocale::DataSizeSIFormat),
                locale.formattedDataSize(static_cast<qint64>(m_total), 0,
                                         QLocale::DataSizeSIFormat)));
        }
        parts.append(
            QStringLiteral("%1 %").arg(static_cast<int>((100 * m_done + m_total / 2) / m_total)));
    }
    parts.append(elapsedText(m_elapsed));
    return parts.join(QStringLiteral(" · "));
}

bool FlashViewModel::cancelAvailable() const {
    return m_running && m_opState == Running && m_running->kind != OperationKind::Reset;
}

QString FlashViewModel::consoleNote() const {
    if (!m_running) {
        return {};
    }
    const device::TargetProfile* console = targetProfile(m_tab->consoleTargetId());
    if (console == nullptr || console->rttTerminal.empty()) {
        return {};
    }
    switch (m_running->kind) {
    case OperationKind::Modem:
    case OperationKind::LteBundle:
        return tr("The console is disconnected during the modem update and resumes automatically "
                  "afterwards.");
    case OperationKind::EraseCode:
    case OperationKind::EraseAll:
        return tr("The console is paused. The erased MCU stays halted: reconnect after flashing.");
    case OperationKind::Flash:
        return m_halt ? tr("The console is paused. The CPU stays halted after flashing.")
                      : tr("The console is paused and resumes automatically after flashing.");
    case OperationKind::Reset:
    case OperationKind::Recover:
    case OperationKind::ReadMemory:
    case OperationKind::Diagnose:
    case OperationKind::Halt:
    case OperationKind::Go:
    case OperationKind::TargetStatus:
    case OperationKind::WriteMemory:
    case OperationKind::ReadRegisters:
    case OperationKind::MemoryZones:
    case OperationKind::WriteFlash:
    case OperationKind::Identify:
        break;
    }
    return tr("The console is paused and resumes automatically afterwards.");
}

QString FlashViewModel::resultText() const {
    const device::TargetProfile* target = targetProfile(m_lastTarget);
    const QString mcu = target != nullptr ? device::targetDisplayName(*target) : m_lastTarget;
    switch (m_opState) {
    case Succeeded:
        switch (m_lastKind) {
        case OperationKind::Flash:
            return tr("Firmware flashed and verified in %1 s.").arg(secondsText(m_duration));
        case OperationKind::LteBundle:
            return tr("LTE modem firmware and LTE firmware flashed in %1 s.")
                .arg(secondsText(m_duration));
        case OperationKind::Modem:
            return tr("Modem firmware updated in %1 s.").arg(secondsText(m_duration));
        case OperationKind::EraseCode:
            return tr("The application firmware of the %1 MCU was erased.").arg(mcu);
        case OperationKind::EraseAll:
            return tr("Everything on the %1 MCU was erased.").arg(mcu);
        case OperationKind::Recover:
            return tr("The %1 MCU was recovered.").arg(mcu);
        case OperationKind::Reset:
            return m_resetHalt ? tr("The %1 MCU was reset and halted.").arg(mcu)
                               : tr("The %1 MCU was reset.").arg(mcu);
        case OperationKind::ReadMemory:
        case OperationKind::Diagnose:
        case OperationKind::Halt:
        case OperationKind::Go:
        case OperationKind::TargetStatus:
        case OperationKind::WriteMemory:
        case OperationKind::ReadRegisters:
        case OperationKind::MemoryZones:
        case OperationKind::WriteFlash:
        case OperationKind::Identify:
            break;
        }
        return {};
    case Cancelled:
        if (m_lastKind == OperationKind::Modem) {
            return tr("The modem update was cancelled. Flash the modem firmware again before you "
                      "use LTE.");
        }
        return isFlashKind(m_lastKind)
                   ? tr("Flashing was cancelled. The device may not start until you flash it "
                        "again.")
                   : tr("The operation was cancelled.");
    case Failed:
        // ADR-0109: an abandoned download is an interrupted flash.
        if (isFlashKind(m_lastKind) && m_error && m_error->code == ErrorCode::TargetProgramFailed) {
            return tr("Flashing was interrupted. The firmware may be partly written: flash it "
                      "again.");
        }
        return {};
    case Idle:
    case Running:
    case Cancelling:
        break;
    }
    return {};
}

QString FlashViewModel::chipText() const {
    if (m_opState != Succeeded || !m_chip || m_chip->name.empty()) {
        return {};
    }
    return tr("Chip: %1").arg(toQString(m_chip->name));
}

ErrorInfo FlashViewModel::error() const { return m_error ? ErrorInfo(*m_error) : ErrorInfo(); }

int FlashViewModel::errorAction() const {
    if (m_error) {
        // Firmware: "Choose another file"; Dependency (nRF Util missing or too old, J-Link
        // library): "Open settings".
        const error_action::Value action = error_action::forCategory(categoryOf(m_error->code));
        if (action == error_action::ChooseFile || action == error_action::OpenSettings) {
            return action;
        }
    }
    return flashOperation() ? error_action::Retry : error_action::Dismiss;
}

QString FlashViewModel::maintenanceTitle() const {
    const device::TargetProfile* target = targetProfile(m_maintenanceTarget);
    return target != nullptr ? tr("Maintenance — %1 MCU").arg(device::targetDisplayName(*target))
                             : tr("Maintenance");
}

QVariantMap FlashViewModel::describeErase(bool recover) const {
    const device::TargetProfile* target = targetProfile(m_maintenanceTarget);
    const QString mcu =
        target != nullptr ? device::targetDisplayName(*target) : m_maintenanceTarget;
    // ERASEALL and recover erase the PIB on every MCU (also on the nRF91, where a UICR erase alone
    // keeps it); the CLI names it for both targets (ADR-0038).
    const QStringList items{tr("Firmware (flash)"), tr("UICR configuration"),
                            tr("Product Information Block (serial number, hardware version, …)")};
    QString body = tr("This erases:");
    if (recover) {
        body = target != nullptr && target->recoverImage != device::RecoverImage::None
                   ? tr("Recover removes the read-out protection by erasing the %1 MCU completely, "
                        "then programs a small image that keeps it debuggable. This erases:")
                         .arg(mcu)
                   : tr("Recover removes the read-out protection by erasing the %1 MCU "
                        "completely. This erases:")
                         .arg(mcu);
    }
    return QVariantMap{
        {QStringLiteral("title"), recover ? tr("Recover the %1 MCU?").arg(mcu)
                                          : tr("Erase everything on the %1 MCU?").arg(mcu)},
        {QStringLiteral("body"), body},
        {QStringLiteral("items"), items},
        {QStringLiteral("consequence"), tr("This cannot be undone.")},
        {QStringLiteral("acceptText"), recover ? tr("Recover") : tr("Erase everything")},
    };
}

void FlashViewModel::requestConfirmation(bool recover) {
    if (!canRunMaintenance() || (recover ? !recoverAvailable() : !eraseAllAvailable())) {
        return;
    }
    m_pending = PendingConfirmation{
        .recover = recover, .targetId = m_maintenanceTarget, .profileId = m_tab->profileId()};
    m_confirmation = describeErase(recover);
    confirmationRequested(); // signal
}

void FlashViewModel::confirm() {
    const std::optional<PendingConfirmation> pending = std::exchange(m_pending, std::nullopt);
    if (!pending) {
        return;
    }
    if (pending->targetId != m_maintenanceTarget || pending->profileId != m_tab->profileId()) {
        // The dialog named another MCU than the one the page shows now: never erase that one.
        qCWarning(lcGui).noquote() << "Flash page: confirmation for" << pending->targetId
                                   << "refused: the MCU changed to" << m_maintenanceTarget;
        return;
    }
    if (pending->recover) {
        runRecover(pending->targetId);
    } else {
        runEraseAll(pending->targetId);
    }
}

QVariantMap FlashViewModel::describeEraseAll() const { return describeErase(false); }

QVariantMap FlashViewModel::describeRecover() const { return describeErase(true); }

void FlashViewModel::retranslate() {
    kindChanged();        // signal
    maintenanceChanged(); // signal
    textsChanged();       // signal
    progressChanged();    // signal
    stateChanged();       // signal
}

} // namespace hwio::gui
