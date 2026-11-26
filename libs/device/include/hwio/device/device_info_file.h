#pragma once

// The device information file (plan/features/0063, ADR-0440): the answers of the profile's
// DeviceProfile::infoFileCommands (CHESTER: `info show`, `config show`) in one plain UTF-8 text
// file named by the device's serial number, written by the GUI ("Save device information…") and
// the CLI (`hwio chester app info save`, `hwio ble info save`) through the same functions.
//
// DeviceInfoCollector runs the commands one by one as silent shell queries (ShellQueryQueue,
// ADR-0175: not in the console) on a J-Link or Bluetooth console; never on a remote support
// session (plan 0044: no silent query to a customer's device). Bounded: at most maxCommands
// commands, ShellQuery's 2 000 lines and 256 KiB per answer (a longer answer is cut and its
// section ends with a marker line), commandTimeout per command. A command without an answer (no
// output before the timeout, or a query the session refused while it stays connected) becomes a
// "(no answer)" section; a console that leaves Connected meanwhile, or a session its owner
// releases from the queue (ShellQueryQueue::setSession), ends the collection with
// target.not_connected and nothing to write.
//
// The document keeps the answers as the device printed them (secrets included) until it is
// written: withoutSecrets() hides them with the console file's rules (ConsoleRedaction, ADR-0186,
// ADR-0426) unless the user asked for them (DeviceInfoFileOptions::withSecrets, D3).
// formatDeviceInfo() and deviceInfoFileName() are pure. writeDeviceInfoFile() writes through a
// QSaveFile, owner-only (restrictToOwner) before the first byte, UTF-8 with LF line ends.
//
// Thread safety: DeviceInfoCollector lives on the owner thread of the session (results by signal
// on that thread); the free functions are reentrant.

#include "hwio/core/result.h"
#include "hwio/device/console_session.h"
#include "hwio/device/session_events.h"
#include "hwio/device/shell_query.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::device {

struct DeviceInfoFileOptions {
    bool withSecrets = false; // D3: secrets are hidden unless the user asks for them
};

struct DeviceInfoSection {
    std::string command;
    bool answered = false;          // false: "(no answer)"
    bool truncated = false;         // the answer was cut at ShellQuery's bounds
    std::vector<std::string> lines; // the answer as printed (no line ends)
};

struct DeviceInfoDocument {
    std::string serial;       // the device's serial number ("" = it reported none)
    std::string connectionId; // J-Link serial number or Bluetooth address (the name's fallback)
    std::string deviceName;   // `info show` product name, else the profile's display name
    std::string firmware;     // "<firmware name> <version>" ("" = unknown)
    QDateTime time;           // when it was read (local time with its UTC offset)
    std::string monitorVersion;
    std::string profileId; // the redaction rules (ConsoleRedaction) of this profile and target
    std::string targetId;
    std::vector<DeviceInfoSection> sections; // in the profile's order

    // The commands without an answer, in order.
    [[nodiscard]] std::vector<std::string> unanswered() const;
};

// The document with every answer line's secret replaced as in the console file
// ("claim token: <redacted>", "lrw config appkey <redacted>").
[[nodiscard]] DeviceInfoDocument withoutSecrets(DeviceInfoDocument document);

// The file content: a header (device name, serial number, firmware, date, Monitor version), then
// per command "### <command>" and its answer, "(no answer)", or the answer and a marker line when
// it was cut. LF line ends, English only (ADR-0027: a record, the same from the GUI and the CLI).
[[nodiscard]] std::string formatDeviceInfo(const DeviceInfoDocument& document);

// "<serial>_<YYYY-MM-DD_HHMMSS>.txt" (D4); without a serial number
// "unknown-<connection id>_<YYYY-MM-DD_HHMMSS>.txt". The serial number and connection id are
// device or system data: bytes other than [A-Za-z0-9._-] become '-', at most maxNamePartBytes,
// never a leading '.'.
inline constexpr std::size_t maxNamePartBytes = 64;
[[nodiscard]] std::string deviceInfoFileName(const DeviceInfoDocument& document);

// Writes formatDeviceInfo() of the document (withoutSecrets() unless options.withSecrets) to
// `path`, replacing an existing file. Errors: io.write_failed (detail: path and reason).
[[nodiscard]] Status writeDeviceInfoFile(const std::filesystem::path& path,
                                         const DeviceInfoDocument& document,
                                         DeviceInfoFileOptions options);

struct DeviceInfoCollectorOptions {
    std::chrono::milliseconds commandTimeout{10000};
    // The document's time; empty = QDateTime::currentDateTime() (tests inject a fixed clock).
    std::function<QDateTime()> clock;
};

class DeviceInfoCollector : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t maxCommands = 16;
    static constexpr std::chrono::milliseconds commandTimeout{10000};

    // The session and the queue must outlive the collector, or the session is destroyed first
    // (the collection then ends with target.not_connected).
    DeviceInfoCollector(ConsoleSession& session, ShellQueryQueue& queries,
                        DeviceInfoCollectorOptions options = {}, QObject* parent = nullptr);
    ~DeviceInfoCollector() override;
    DeviceInfoCollector(const DeviceInfoCollector&) = delete;
    DeviceInfoCollector& operator=(const DeviceInfoCollector&) = delete;
    DeviceInfoCollector(DeviceInfoCollector&&) = delete;
    DeviceInfoCollector& operator=(DeviceInfoCollector&&) = delete;

    // Starts reading; finished() follows (also for an error found later). Errors (nothing
    // started): target.not_connected (the console is not connected, or `queries` does not run on
    // this session), usage.invalid_argument (a
    // remote support session, or a profile without infoFileCommands), internal.busy (running).
    [[nodiscard]] Status start();
    // Stops without a result (no finished()); a query already sent ends by its own rule.
    void cancel();

    [[nodiscard]] bool running() const noexcept { return m_running; }
    // Commands answered or given up so far, and all of them (progress "2 of 4").
    [[nodiscard]] std::size_t done() const noexcept { return m_document.sections.size(); }
    [[nodiscard]] std::size_t total() const noexcept { return m_commands.size(); }
    // Set when finished() is emitted; nullopt before and after cancel() / start().
    [[nodiscard]] const std::optional<Result<DeviceInfoDocument>>& result() const noexcept {
        return m_result;
    }

signals:
    void progressChanged();
    void finished();

private:
    void next();
    void onAnswer(const std::optional<ShellQueryResult>& answer);
    void onConnection(const ConnectionEvent& event);
    void fail(Error error);
    void complete();

    QPointer<ConsoleSession> m_session;
    ShellQueryQueue* m_queries; // not owned; outlives this
    DeviceInfoCollectorOptions m_options;
    std::vector<std::string> m_commands; // of this run, <= maxCommands
    DeviceInfoDocument m_document;
    std::optional<Result<DeviceInfoDocument>> m_result;
    std::uint64_t m_run = 0; // answers of an earlier (cancelled) run are ignored
    std::uint64_t m_attach = 0;
    bool m_running = false;
};

// CLI helper: start() and a local event loop until finished() (bounded by the commands' timeouts
// and the queue's idle wait). Errors: those of start() and of the collection, cancelled when
// `stop` is requested, timeout when the bound passed.
[[nodiscard]] Result<DeviceInfoDocument> collectDeviceInfo(ConsoleSession& session,
                                                           ShellQueryQueue& queries,
                                                           std::stop_token stop = {},
                                                           DeviceInfoCollectorOptions options = {});

} // namespace hwio::device
