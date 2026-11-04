#pragma once

// LteUpdateProcedure - the guided LTE firmware update (plan/features/0033-rc2-feedback.md, C and
// CLI; ADR-0234). A headless state machine on the owner thread of a J-Link DeviceSession; the GUI
// wizard (LteUpdateWizard) and `hwio chester lte flash --guided` both drive it and only show what
// it says (texts translated here, ADR-0027).
//
//   ConnectApp        the probe must reach the console target (application MCU): identifyTarget
//                     every pollInterval, one in flight, until it matches (or cancel)
//   Backup            the PIB serial number, then the code flash in readMemory chunks; trimmed and
//                     written by ApplicationBackup (D3). A failure (target.protected, a read or
//                     write error) stops in BackupFailed: continueWithoutBackup() or cancel()
//   Erase             erase(app, Code): UICR and PIB stay
//   ConnectLte        as ConnectApp for the LTE target
//   UpdateLte         flash / flashLteBundle / updateModem of the source on the LTE target
//   ConnectAppAgain   as ConnectApp
//   ChooseApplication restoreApplication(): the copy (default) or another image; skipped with
//                     Options::autoRestore when there is a copy
//   Restore           flash(app) - programs, verifies and resets (the application runs)
//   Done | Failed | Cancelled
// The session is held (SessionHold, D4) from start() until Done or Cancelled (and while this
// object lives): RTT and auto-reconnect are paused; releasing the hold reconnects the console.
// An operation that finds the other MCU (target.wrong_family: the cable moved after the check) goes
// back to its connector step by itself; any other failure of Erase, UpdateLte or Restore can be
// retried (back to its connector step). cancel() is
// refused while Erase, UpdateLte or Restore runs (a flash is never interrupted here).
//
// The two targets: the session's console target (CHESTER app) and the profile's first other
// target (CHESTER lte); the connector labels are their ids in upper case ("APP", "LTE").
//
// Ownership: a QObject on the session's owner thread; it keeps the session as a QPointer (a closed
// session ends the procedure as Failed). Bounds: one session operation at a time; the backup is at
// most ApplicationBackup::maxBytes in memory.

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/lte_input.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hwio::device {

struct TargetProfile;

class LteUpdateProcedure : public QObject {
    Q_OBJECT

public:
    enum class Step : std::uint8_t {
        Idle,
        ConnectApp,
        Backup,
        BackupFailed,
        Erase,
        ConnectLte,
        UpdateLte,
        ConnectAppAgain,
        ChooseApplication,
        Restore,
        Done,
        Failed,
        Cancelled,
    };

    // What the LTE MCU gets (the ADR-0040 classification of the LTE input).
    struct Source {
        enum class Kind : std::uint8_t { Image, Bundle, Modem };
        Kind kind = Kind::Bundle;
        std::shared_ptr<const firmware::FirmwareImage> image; // Image
        std::shared_ptr<const firmware::LteBundle> bundle;    // Bundle
        std::filesystem::path modemZip;                       // Modem
        std::string name;                                     // file name or the designated name
        FlashOptions options; // Image, Bundle: allowUicr (ADR-0036); `halt` is ignored
    };

    struct Options {
        std::chrono::milliseconds pollInterval{1000};
        std::filesystem::path backupDirectory; // empty = ApplicationBackup::defaultDirectory()
        bool autoRestore = false;              // CLI: write the copy back without waiting
        // Test seam: the time in the backup's file name (system_clock::now by default).
        std::function<std::chrono::system_clock::time_point()> now;
    };

    LteUpdateProcedure(DeviceSession& session, Options options, QObject* parent = nullptr);
    ~LteUpdateProcedure() override; // cancels a running read or identify; releases the hold
    LteUpdateProcedure(const LteUpdateProcedure&) = delete;
    LteUpdateProcedure& operator=(const LteUpdateProcedure&) = delete;
    LteUpdateProcedure(LteUpdateProcedure&&) = delete;
    LteUpdateProcedure& operator=(LteUpdateProcedure&&) = delete;

    // Holds the session and waits for the application MCU. Errors: usage.invalid_argument
    // (already started, an empty source, a profile without a second target), those of hold().
    [[nodiscard]] Status start(Source source);
    // BackupFailed: go on without a copy (ChooseApplication then needs an image).
    void continueWithoutBackup();
    // ChooseApplication: `image` nullptr = the copy. Errors: usage.invalid_argument (not waiting
    // for it, nullptr without a copy, an empty image).
    [[nodiscard]] Status
    restoreApplication(std::shared_ptr<const firmware::FirmwareImage> image = nullptr,
                       FlashOptions options = {});
    // Refused (false) while Erase, UpdateLte or Restore runs and after the end.
    bool cancel();
    // Failed after Erase, UpdateLte or Restore: back to that step's connector step.
    void retry();

    [[nodiscard]] Step step() const noexcept { return m_step; }
    // Failed: the step that failed (Idle otherwise).
    [[nodiscard]] Step failedStep() const noexcept {
        return m_step == Step::Failed ? m_failedStep : Step::Idle;
    }
    [[nodiscard]] bool canCancel() const noexcept;
    [[nodiscard]] bool canRetry() const noexcept;
    // The application MCU was erased by this procedure and nothing was written back yet.
    [[nodiscard]] bool applicationErased() const noexcept { return m_erased; }
    [[nodiscard]] bool hasBackup() const noexcept { return !m_backupFile.empty(); }
    [[nodiscard]] const std::filesystem::path& backupFile() const noexcept { return m_backupFile; }
    // The connector step found the other MCU (the cable is on the wrong connector).
    [[nodiscard]] bool wrongConnector() const noexcept { return m_wrongConnector; }
    // The text of the current step (translated): the connector prompt, "Saving a copy of the
    // application…", the result, ...
    [[nodiscard]] QString promptText() const;
    // The connector label of a connector step ("APP", "LTE"); "" otherwise.
    [[nodiscard]] QString connectorLabel() const;
    // BackupFailed and Failed: what went wrong.
    [[nodiscard]] const std::optional<Error>& error() const noexcept { return m_error; }
    // The last progress of the running operation (phases, step 1/2 of a bundle, bytes); the
    // backup reports done / total bytes with kind ReadMemory.
    [[nodiscard]] const OperationProgress& progress() const noexcept { return m_progress; }
    [[nodiscard]] const Source& source() const noexcept { return m_source; }
    [[nodiscard]] std::string_view appTargetId() const noexcept;
    [[nodiscard]] std::string_view lteTargetId() const noexcept;

    [[nodiscard]] static std::string_view stepId(Step step) noexcept; // "connect-app", ... (logs)

signals:
    void changed();         // step, prompt, error
    void progressChanged(); // progress()
    void finished();        // Done, Failed or Cancelled was reached

private:
    void setStep(Step step);
    void fail(Error error);
    void startConnectorStep(Step step);
    void poll();
    void startBackup();
    void readNextChunk();
    void finishBackup();
    void startErase();
    void startLteUpdate();
    void startRestore();
    void afterConnector();
    void endHold();
    void cancelRunning();
    // The connector step that precedes an operation step.
    [[nodiscard]] static Step connectorStepBefore(Step step) noexcept;
    [[nodiscard]] bool startOperation(const Result<OperationId>& id);
    void onProgress(OperationId id, const OperationProgress& progress);
    void onFinished(OperationId id, const OperationResult& result);
    void onConnectorResult(const OperationResult& result);
    void onBackupResult(const OperationResult& result);
    [[nodiscard]] const TargetProfile* appTarget() const;
    [[nodiscard]] const TargetProfile* lteTarget() const;

    QPointer<DeviceSession> m_session;
    Options m_options;
    SessionHold m_hold;
    Source m_source;
    Step m_step = Step::Idle;
    Step m_failedStep = Step::Idle;
    std::optional<OperationId> m_operation; // the one in flight
    bool m_wrongConnector = false;
    bool m_erased = false;
    bool m_erasedBefore = false; // m_erased when the current erase started
    bool m_backupSkipped = false;
    std::optional<Error> m_error;
    OperationProgress m_progress;
    QTimer m_pollTimer;
    // Backup: the PIB serial number, the range and the bytes read so far (<= maxBytes).
    std::string m_serial;
    std::uint32_t m_backupBase = 0;
    std::size_t m_backupSize = 0;
    bool m_pibRead = false;
    bool m_backupCheck = false; // an identify after a failed backup read is in flight
    std::vector<std::byte> m_backupData;
    std::filesystem::path m_backupFile;
    std::shared_ptr<const firmware::FirmwareImage> m_restoreImage;
    FlashOptions m_restoreOptions;
};

} // namespace hwio::device
