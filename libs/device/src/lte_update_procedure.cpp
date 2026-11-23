#include "hwio/device/lte_update_procedure.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/device/application_backup.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/pib.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_image.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QtLogging>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::device {

namespace {

QString label(const TargetProfile* target) {
    return target != nullptr ? toQString(target->id).toUpper() : QString();
}

constexpr std::size_t pibBytes = 128;

} // namespace

LteUpdateProcedure::LteUpdateProcedure(DeviceSession& session, Options options, QObject* parent)
    : QObject(parent), m_session(&session), m_options(std::move(options)) {
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &LteUpdateProcedure::poll);
    connect(&session, &DeviceSession::operationProgress, this, &LteUpdateProcedure::onProgress);
    connect(&session, &DeviceSession::operationFinished, this, &LteUpdateProcedure::onFinished);
    connect(&session, &QObject::destroyed, this, [this] {
        if (m_step != Step::Idle && m_step != Step::Done && m_step != Step::Failed &&
            m_step != Step::Cancelled) {
            m_operation.reset();
            fail(makeError(ErrorCode::TargetNotConnected, "the session was closed"));
        }
    });
}

LteUpdateProcedure::~LteUpdateProcedure() {
    m_pollTimer.stop();
    // A read or an identify is cancelled; a flash or erase runs to its end in the session.
    if (m_operation && !m_session.isNull() &&
        (m_step == Step::Backup || m_step == Step::ConnectApp || m_step == Step::ConnectLte ||
         m_step == Step::ConnectAppAgain)) {
        cancelRunning();
    }
    m_hold.release();
}

const TargetProfile* LteUpdateProcedure::appTarget() const {
    return m_session.isNull() ? nullptr
                              : findTarget(m_session->profile(), m_session->consoleTargetId());
}

const TargetProfile* LteUpdateProcedure::lteTarget() const {
    if (m_session.isNull()) {
        return nullptr;
    }
    for (const TargetProfile& target : m_session->profile().targets) {
        if (target.id != m_session->consoleTargetId()) {
            return &target;
        }
    }
    return nullptr;
}

std::string_view LteUpdateProcedure::appTargetId() const noexcept {
    const TargetProfile* target = appTarget();
    return target != nullptr ? target->id : std::string_view();
}

std::string_view LteUpdateProcedure::lteTargetId() const noexcept {
    const TargetProfile* target = lteTarget();
    return target != nullptr ? target->id : std::string_view();
}

std::string_view LteUpdateProcedure::stepId(Step step) noexcept {
    switch (step) {
    case Step::Idle:
        return "idle";
    case Step::ConnectApp:
        return "connect-app";
    case Step::Confirm:
        return "confirm";
    case Step::Backup:
        return "backup";
    case Step::BackupFailed:
        return "backup-failed";
    case Step::Erase:
        return "erase";
    case Step::ConnectLte:
        return "connect-lte";
    case Step::UpdateLte:
        return "update-lte";
    case Step::ConnectAppAgain:
        return "connect-app-again";
    case Step::ChooseApplication:
        return "choose-application";
    case Step::Restore:
        return "restore";
    case Step::Done:
        return "done";
    case Step::Failed:
        return "failed";
    case Step::Cancelled:
        return "cancelled";
    }
    return "idle";
}

void LteUpdateProcedure::cancelRunning() {
    // A read or an identify: the session ends it soon; its result is then ignored.
    if (m_operation && !m_session.isNull()) {
        if (Status cancelled = m_session->cancel(*m_operation); !cancelled) {
            qCInfo(log::lcFlash).noquote()
                << "LTE update: cancel refused:" << toQString(cancelled.error().detail);
        }
    }
}

LteUpdateProcedure::Step LteUpdateProcedure::connectorStepBefore(Step step) noexcept {
    switch (step) {
    case Step::Erase:
        return Step::ConnectApp;
    case Step::UpdateLte:
        return Step::ConnectLte;
    default:
        return Step::ConnectAppAgain;
    }
}

// ---- control ----------------------------------------------------------------------------------

Status LteUpdateProcedure::start(Source source) {
    if (m_step != Step::Idle) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "the LTE update was started already"));
    }
    if (m_session.isNull() || appTarget() == nullptr || lteTarget() == nullptr) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "the device profile has no second target"));
    }
    const bool empty = (source.kind == Source::Kind::Image && !source.image) ||
                       (source.kind == Source::Kind::Bundle && !source.bundle) ||
                       (source.kind == Source::Kind::Modem && source.modemZip.empty());
    if (empty) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "no LTE firmware for the update"));
    }
    auto hold = m_session->hold();
    if (!hold) {
        return std::unexpected(std::move(hold).error());
    }
    m_hold = std::move(*hold);
    m_source = std::move(source);
    qCInfo(log::lcFlash).noquote() << "LTE update started with" << toQString(m_source.name);
    startConnectorStep(Step::ConnectApp);
    return {};
}

bool LteUpdateProcedure::canCancel() const noexcept {
    switch (m_step) {
    case Step::ConnectApp:
    case Step::Confirm:
    case Step::Backup:
    case Step::BackupFailed:
    case Step::ConnectLte:
    case Step::ConnectAppAgain:
    case Step::ChooseApplication:
        return true;
    case Step::Failed:
        return canRetry(); // a retryable failure is still in the procedure
    case Step::Idle:
    case Step::Erase:
    case Step::UpdateLte:
    case Step::Restore:
    case Step::Done:
    case Step::Cancelled:
        break;
    }
    return false;
}

bool LteUpdateProcedure::canRetry() const noexcept {
    return m_step == Step::Failed && !m_session.isNull() &&
           (m_failedStep == Step::Erase || m_failedStep == Step::UpdateLte ||
            m_failedStep == Step::Restore);
}

bool LteUpdateProcedure::cancel() {
    if (!canCancel()) {
        return false;
    }
    m_pollTimer.stop();
    qCInfo(log::lcFlash).noquote() << "LTE update cancelled in" << toQString(stepId(m_step))
                                   << (m_erased ? "(the application MCU is erased)" : "");
    if (m_operation && !m_session.isNull()) {
        // A read or an identify: the session ends it soon; its result is ignored.
        cancelRunning();
    }
    m_operation.reset();
    m_error.reset();
    setStep(Step::Cancelled);
    endHold();
    finished(); // signal
    return true;
}

void LteUpdateProcedure::retry() {
    if (!canRetry()) {
        return;
    }
    m_error.reset();
    switch (m_failedStep) {
    case Step::Erase:
        startConnectorStep(Step::ConnectApp);
        break;
    case Step::UpdateLte:
        startConnectorStep(Step::ConnectLte);
        break;
    default:
        startConnectorStep(Step::ConnectAppAgain);
        break;
    }
}

void LteUpdateProcedure::confirm() {
    if (m_step != Step::Confirm) {
        return;
    }
    qCInfo(log::lcFlash) << "LTE update: confirmed";
    m_confirmed = true;
    startBackup();
}

void LteUpdateProcedure::continueWithoutBackup() {
    if (m_step != Step::BackupFailed) {
        return;
    }
    qCInfo(log::lcFlash) << "LTE update: continuing without a copy of the application";
    m_backupSkipped = true;
    m_error.reset();
    startErase();
}

Status LteUpdateProcedure::restoreApplication(std::shared_ptr<const firmware::FirmwareImage> image,
                                              FlashOptions options) {
    if (m_step != Step::ChooseApplication) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                         "the LTE update is not waiting for the application"));
    }
    if (image == nullptr && m_restoreImage == nullptr) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "there is no copy of the application"));
    }
    if (image != nullptr && image->empty()) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "the application image is empty"));
    }
    if (image != nullptr) {
        m_restoreImage = std::move(image);
        m_restoreOptions = options;
    } else {
        m_restoreOptions = FlashOptions{};
    }
    startRestore();
    return {};
}

// ---- steps ------------------------------------------------------------------------------------

void LteUpdateProcedure::setStep(Step step) {
    if (step != m_step) {
        qCInfo(log::lcFlash).noquote() << "LTE update step:" << toQString(stepId(step));
    }
    m_step = step;
    changed(); // signal
}

void LteUpdateProcedure::fail(Error error) {
    m_pollTimer.stop();
    qCWarning(log::lcFlash).noquote() << "LTE update failed in" << toQString(stepId(m_step)) << ":"
                                      << toQString(idOf(error.code)) << toQString(error.detail);
    m_failedStep = m_step;
    m_error = std::move(error);
    setStep(Step::Failed);
    if (!canRetry()) {
        endHold();
    }
    finished(); // signal
}

void LteUpdateProcedure::endHold() {
    // Releasing the hold reconnects the console on the application MCU (D4).
    m_hold.release();
}

bool LteUpdateProcedure::startOperation(const Result<OperationId>& id) {
    if (!id) {
        fail(id.error());
        return false;
    }
    m_operation = *id;
    m_progress = OperationProgress{};
    progressChanged(); // signal
    return true;
}

void LteUpdateProcedure::startConnectorStep(Step step) {
    m_wrongConnector = false;
    setStep(step);
    poll(); // the first check at once
}

void LteUpdateProcedure::poll() {
    if (m_operation || m_session.isNull()) {
        return; // one identify in flight
    }
    std::string_view target;
    switch (m_step) {
    case Step::ConnectApp:
    case Step::ConnectAppAgain:
        target = appTargetId();
        break;
    case Step::ConnectLte:
        target = lteTargetId();
        break;
    default:
        return;
    }
    auto id = m_session->identifyTarget(target);
    if (!id) {
        // Another operation of the session (internal.busy) or a full queue: try again later.
        qCInfo(log::lcFlash).noquote()
            << "LTE update: identify not started:" << toQString(id.error().detail);
        m_pollTimer.start(m_options.pollInterval);
        return;
    }
    m_operation = *id;
}

void LteUpdateProcedure::onConnectorResult(const OperationResult& result) {
    const bool app = m_step == Step::ConnectApp || m_step == Step::ConnectAppAgain;
    const std::string_view wanted = app ? appTargetId() : lteTargetId();
    bool wrong = false;
    if (result.status && result.familyMatch) {
        // A protected MCU answers only through its CTRL-AP: still the right connector (the backup
        // step then reports target.protected).
        if (result.familyMatch->foundTargetId == wanted) {
            afterConnector();
            return;
        }
        wrong = !result.familyMatch->foundTargetId.empty();
    }
    if (wrong != m_wrongConnector) {
        m_wrongConnector = wrong;
        changed(); // signal
    }
    m_pollTimer.start(m_options.pollInterval);
}

void LteUpdateProcedure::afterConnector() {
    m_wrongConnector = false;
    switch (m_step) {
    case Step::ConnectApp:
        if (!m_confirmed) {
            setStep(Step::Confirm); // ADR-0366: nothing changes before the user's Continue
        } else if (hasBackup() || m_backupSkipped) {
            startErase(); // a retried erase keeps the copy it made
        } else {
            startBackup();
        }
        break;
    case Step::ConnectLte:
        startLteUpdate();
        break;
    case Step::ConnectAppAgain:
        setStep(Step::ChooseApplication);
        if (m_options.autoRestore && m_restoreImage != nullptr) {
            if (Status restored = restoreApplication(); !restored) {
                qCWarning(log::lcFlash).noquote()
                    << "LTE update: restore refused:" << toQString(restored.error().detail);
            }
        }
        break;
    default:
        break;
    }
}

void LteUpdateProcedure::startBackup() {
    const TargetProfile* target = appTarget();
    auto range = ApplicationBackup::rangeOf(*target);
    if (!range) {
        m_error = std::move(range).error();
        setStep(Step::BackupFailed);
        return;
    }
    m_backupBase = static_cast<std::uint32_t>(range->start);
    m_backupSize = static_cast<std::size_t>(range->size());
    m_backupData.clear();
    m_backupData.reserve(m_backupSize);
    m_pibRead = false;
    m_backupCheck = false;
    m_serial.clear();
    setStep(Step::Backup);
    // The PIB first (the serial number of the file name).
    const auto pib = static_cast<std::uint32_t>(target->uicr.start + target->pibOffset);
    auto id = m_session->readMemory(target->id, pib, pibBytes);
    if (!id) {
        m_error = id.error();
        setStep(Step::BackupFailed);
        return;
    }
    m_operation = *id;
}

void LteUpdateProcedure::readNextChunk() {
    if (m_backupData.size() >= m_backupSize) {
        finishBackup();
        return;
    }
    const std::size_t size = std::min(maxReadMemoryBytes, m_backupSize - m_backupData.size());
    const auto address = static_cast<std::uint32_t>(m_backupBase + m_backupData.size());
    auto id = m_session->readMemory(appTargetId(), address, size);
    if (!id) {
        m_error = id.error();
        setStep(Step::BackupFailed);
        return;
    }
    m_operation = *id;
}

void LteUpdateProcedure::onBackupResult(const OperationResult& result) {
    if (m_backupCheck) {
        // The answer of the connector check after a failed read.
        m_backupCheck = false;
        const bool stillApp = result.status && result.familyMatch &&
                              result.familyMatch->foundTargetId == appTargetId();
        if (!stillApp) {
            m_error.reset();
            startConnectorStep(Step::ConnectApp); // the cable moved: wait for APP again
            return;
        }
        setStep(Step::BackupFailed); // m_error: the read error
        return;
    }
    if (!result.status) {
        m_error = result.status.error();
        m_backupData.clear();
        m_backupData.shrink_to_fit();
        // A read fails also when the cable moved after the connector check (another MCU answers
        // at the address): check which MCU answers before saying that no copy can be made.
        if (auto check = m_session->identifyTarget(appTargetId()); check) {
            m_backupCheck = true;
            m_operation = *check;
            return;
        }
        setStep(Step::BackupFailed);
        return;
    }
    if (!m_pibRead) {
        m_pibRead = true;
        if (result.data.size() == pibBytes) {
            if (auto pib =
                    decodePib(std::span<const std::byte, pibBytes>(result.data.data(), pibBytes))) {
                m_serial = pib->serialNumber;
            }
        }
        readNextChunk();
        return;
    }
    m_backupData.insert(m_backupData.end(), result.data.begin(), result.data.end());
    m_progress.kind = OperationKind::ReadMemory;
    m_progress.done = m_backupData.size();
    m_progress.total = m_backupSize;
    progressChanged(); // signal
    readNextChunk();
}

void LteUpdateProcedure::finishBackup() {
    std::vector<std::byte> data = ApplicationBackup::trimmed(std::move(m_backupData));
    m_backupData = {};
    if (data.empty()) {
        // Nothing in the application flash: there is nothing to keep (and nothing to restore).
        qCInfo(log::lcFlash) << "LTE update: the application flash is empty; no copy needed";
        m_backupSkipped = true;
        startErase();
        return;
    }
    const auto now = m_options.now ? m_options.now() : std::chrono::system_clock::now();
    const std::filesystem::path directory = m_options.backupDirectory.empty()
                                                ? ApplicationBackup::defaultDirectory()
                                                : m_options.backupDirectory;
    auto saved = ApplicationBackup::save(directory, m_serial, now, m_backupBase, data);
    if (!saved) {
        m_error = std::move(saved).error();
        setStep(Step::BackupFailed);
        return;
    }
    std::vector<firmware::Segment> segments;
    segments.push_back(firmware::Segment{.address = m_backupBase, .data = std::move(data)});
    auto image = firmware::FirmwareImage::fromSegments(std::move(segments));
    if (!image) {
        m_error = std::move(image).error();
        setStep(Step::BackupFailed);
        return;
    }
    m_backupFile = std::move(*saved);
    m_restoreImage = std::make_shared<const firmware::FirmwareImage>(std::move(*image));
    startErase();
}

void LteUpdateProcedure::startErase() {
    setStep(Step::Erase);
    m_erasedBefore = m_erased;
    if (startOperation(m_session->erase(appTargetId(), EraseScope::Code))) {
        m_erased = true; // from now on the application may be gone
    }
}

void LteUpdateProcedure::startLteUpdate() {
    setStep(Step::UpdateLte);
    const std::string_view target = lteTargetId();
    const FlashOptions options{.halt = false, .allowUicr = m_source.options.allowUicr};
    switch (m_source.kind) {
    case Source::Kind::Image:
        (void)startOperation(m_session->flash(target, m_source.image, options));
        break;
    case Source::Kind::Bundle:
        (void)startOperation(m_session->flashLteBundle(target, m_source.bundle, options));
        break;
    case Source::Kind::Modem:
        (void)startOperation(m_session->updateModem(target, m_source.modemZip));
        break;
    }
}

void LteUpdateProcedure::startRestore() {
    setStep(Step::Restore);
    (void)startOperation(m_session->flash(appTargetId(), m_restoreImage, m_restoreOptions));
}

// ---- session events ---------------------------------------------------------------------------

void LteUpdateProcedure::onProgress(OperationId id, const OperationProgress& progress) {
    if (!m_operation || id != *m_operation || m_step == Step::Backup) {
        return;
    }
    if (m_step == Step::Erase || m_step == Step::UpdateLte || m_step == Step::Restore) {
        m_progress = progress;
        progressChanged(); // signal
    }
}

void LteUpdateProcedure::onFinished(OperationId id, const OperationResult& result) {
    if (!m_operation || id != *m_operation) {
        return;
    }
    m_operation.reset();
    // The cable moved after the connector check (the operation found the other MCU before it
    // wrote anything): back to waiting for the right connector.
    if (!result.status && result.status.error().code == ErrorCode::TargetWrongFamily &&
        (m_step == Step::Erase || m_step == Step::UpdateLte || m_step == Step::Restore)) {
        qCInfo(log::lcFlash) << "LTE update: the J-Link is on the other connector again";
        if (m_step == Step::Erase) {
            m_erased = m_erasedBefore;
        }
        startConnectorStep(connectorStepBefore(m_step));
        return;
    }
    switch (m_step) {
    case Step::ConnectApp:
    case Step::ConnectLte:
    case Step::ConnectAppAgain:
        onConnectorResult(result);
        return;
    case Step::Backup:
        onBackupResult(result);
        return;
    case Step::Erase:
        if (!result.status) {
            fail(result.status.error());
            return;
        }
        startConnectorStep(Step::ConnectLte);
        return;
    case Step::UpdateLte:
        if (!result.status) {
            fail(result.status.error());
            return;
        }
        startConnectorStep(Step::ConnectAppAgain);
        return;
    case Step::Restore:
        if (!result.status) {
            fail(result.status.error());
            return;
        }
        m_erased = false;
        setStep(Step::Done);
        endHold();
        finished(); // signal
        return;
    case Step::Idle:
    case Step::Confirm:
    case Step::BackupFailed:
    case Step::ChooseApplication:
    case Step::Done:
    case Step::Failed:
    case Step::Cancelled:
        return;
    }
}

// ---- texts ------------------------------------------------------------------------------------

QString LteUpdateProcedure::connectorLabel() const {
    switch (m_step) {
    case Step::ConnectApp:
    case Step::ConnectAppAgain:
        return label(appTarget());
    case Step::ConnectLte:
        return label(lteTarget());
    default:
        return {};
    }
}

QString LteUpdateProcedure::promptText() const {
    switch (m_step) {
    case Step::Idle:
        return {};
    case Step::ConnectApp:
    case Step::ConnectAppAgain:
    case Step::ConnectLte: {
        const bool app = m_step != Step::ConnectLte;
        const QString wanted = label(app ? appTarget() : lteTarget());
        const QString other = label(app ? lteTarget() : appTarget());
        return m_wrongConnector
                   ? tr("This is the %1 connector. Move the cable to the %2 connector.")
                         .arg(other, wanted)
                   : tr("Connect the J-Link cable to the %1 connector.").arg(wanted);
    }
    case Step::Confirm:
        return tr("The application on this CHESTER will be erased. Then you move the J-Link cable "
                  "to the %1 connector, and at the end back to %2; the application is written "
                  "back.")
            .arg(label(lteTarget()), label(appTarget()));
    case Step::Backup:
        return tr("Saving a copy of the application…");
    case Step::BackupFailed:
        return tr("A copy of the application could not be saved. Without it you will have to "
                  "flash an application yourself at the end.");
    case Step::Erase:
        return tr("Erasing the application…");
    case Step::UpdateLte:
        return tr("Updating the LTE MCU…");
    case Step::ChooseApplication:
        return m_restoreImage != nullptr ? tr("Write back the application that was in the device.")
                                         : tr("Choose the application firmware to flash.");
    case Step::Restore:
        return tr("Writing the application…");
    case Step::Done:
        return tr("The LTE firmware was updated and the application is running again.");
    case Step::Failed:
        return m_error ? toQString(m_error->message) : QString();
    case Step::Cancelled:
        return m_erased ? tr("The application MCU is erased. The device will not run until you "
                             "flash an application.")
                        : tr("The LTE firmware update was cancelled.");
    }
    return {};
}

} // namespace hwio::device
