#include "hwio/device/device_info_file.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/localized_text.h"
#include "hwio/core/log.h"
#include "hwio/core/owner_only.h"
#include "hwio/core/result.h"
#include "hwio/core/version.h"
#include "hwio/device/command_capture.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_redaction.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_info.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/device/shell_query.h"

#include <QByteArray>
#include <QDateTime>
#include <QEventLoop>
#include <QIODevice>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QtCore/qtmetamacros.h>
#include <QtTranslation>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::device {

namespace {

constexpr std::string_view unknownText = "unknown";

// ShellQuery's bounds (the marker line names them).
static_assert(ShellQuery::defaultMaxLines == 2000);
static_assert(CaptureOptions{}.maxBytes == std::size_t{256} * 1024);

// [A-Za-z0-9._-] kept, every other byte '-', at most maxNamePartBytes, no leading '.'.
std::string namePart(std::string_view text) {
    std::string part;
    for (const char c : text.substr(0, maxNamePartBytes)) {
        const bool kept = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        part += kept ? c : '-';
    }
    if (part.starts_with('.')) {
        part.front() = '-';
    }
    return part;
}

// "+01:00" / "-05:30" / "+00:00".
std::string utcOffset(const QDateTime& time) {
    const int seconds = time.offsetFromUtc();
    const int minutes = std::abs(seconds) / 60;
    return std::format("{}{:02}:{:02}", seconds < 0 ? '-' : '+', minutes / 60, minutes % 60);
}

std::string orUnknown(const std::string& value) {
    return value.empty() ? std::string(unknownText) : value;
}

Error disconnectedError() {
    Error error = makeError(ErrorCode::TargetNotConnected, "device information");
    setText(error, ErrorText::Message, "hwio::device",
            QT_TRANSLATE_NOOP("hwio::device",
                              "The device disconnected while its information was read."));
    setText(error, ErrorText::Suggestion, "hwio::device",
            QT_TRANSLATE_NOOP("hwio::device", "Connect the device again and try once more."));
    return error;
}

} // namespace

std::vector<std::string> DeviceInfoDocument::unanswered() const {
    std::vector<std::string> commands;
    for (const DeviceInfoSection& section : sections) {
        if (!section.answered) {
            commands.push_back(section.command);
        }
    }
    return commands;
}

DeviceInfoDocument withoutSecrets(DeviceInfoDocument document) {
    const ConsoleRedaction redaction(document.profileId, document.targetId);
    for (DeviceInfoSection& section : document.sections) {
        for (std::string& text : section.lines) {
            ConsoleLine line;
            line.channel = Channel::Shell;
            line.dir = Direction::Out;
            line.text = text;
            if (std::optional<std::string> hidden = redaction.redacted(line)) {
                text = std::move(*hidden);
            }
        }
    }
    return document;
}

std::string formatDeviceInfo(const DeviceInfoDocument& document) {
    std::string text = "HARDWARIO Monitor - device information\n";
    text += "Device: " + orUnknown(document.deviceName) + '\n';
    text += "Serial number: " + orUnknown(document.serial) + '\n';
    text += "Firmware: " + orUnknown(document.firmware) + '\n';
    text += "Date: " +
            (document.time.isValid()
                 ? toStdString(document.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))) +
                       ' ' + utcOffset(document.time)
                 : std::string(unknownText)) +
            '\n';
    text += "HARDWARIO Monitor: " + orUnknown(document.monitorVersion) + '\n';
    for (const DeviceInfoSection& section : document.sections) {
        text += "\n### " + section.command + '\n';
        if (!section.answered) {
            text += "(no answer)\n";
            continue;
        }
        for (const std::string& line : section.lines) {
            text += line + '\n';
        }
        if (section.truncated) {
            text += "(the answer was cut here: at most 2000 lines or 256 KiB per command)\n";
        }
    }
    return text;
}

std::string deviceInfoFileName(const DeviceInfoDocument& document) {
    const std::string name =
        !document.serial.empty()
            ? namePart(document.serial)
            : "unknown-" +
                  namePart(document.connectionId.empty() ? unknownText : document.connectionId);
    const QDateTime time = document.time.isValid() ? document.time : QDateTime::currentDateTime();
    return name + '_' + toStdString(time.toString(QStringLiteral("yyyy-MM-dd_HHmmss"))) + ".txt";
}

Status writeDeviceInfoFile(const std::filesystem::path& path, const DeviceInfoDocument& document,
                           DeviceInfoFileOptions options) {
    const QString name = toQString(toUtf8(path));
    const auto failed = [&path](const QString& reason) {
        return std::unexpected(
            makeError(ErrorCode::IoWriteFailed, toUtf8(path) + ": " + toStdString(reason)));
    };
    QSaveFile out(name);
    // Binary: LF line ends on every system (ADR-0440).
    if (!out.open(QIODevice::WriteOnly)) {
        return failed(out.errorString());
    }
    restrictToOwner(out); // ADR-0426: before the first byte (the temporary file of QSaveFile)
    const std::string content =
        formatDeviceInfo(options.withSecrets ? document : withoutSecrets(document));
    const QByteArray bytes = QByteArray::fromStdString(content);
    if (out.write(bytes) != bytes.size()) {
        const QString reason = out.errorString();
        out.cancelWriting();
        return failed(reason);
    }
    if (!out.commit()) {
        return failed(out.errorString());
    }
    qCInfo(log::lcDevice).noquote()
        << "Device information written:" << name
        << (options.withSecrets ? "(with secrets)" : "(secrets hidden)");
    return {};
}

// ---- DeviceInfoCollector ------------------------------------------------------------------------

DeviceInfoCollector::DeviceInfoCollector(ConsoleSession& session, ShellQueryQueue& queries,
                                         DeviceInfoCollectorOptions options, QObject* parent)
    : QObject(parent), m_session(&session), m_queries(&queries), m_options(std::move(options)) {
    connect(&session, &ConsoleSession::connectionChanged, this, &DeviceInfoCollector::onConnection);
    connect(&session, &QObject::destroyed, this, [this] {
        if (m_running) {
            fail(disconnectedError());
        }
    });
}

DeviceInfoCollector::~DeviceInfoCollector() = default;

Status DeviceInfoCollector::start() {
    if (m_running) {
        return std::unexpected(makeError(ErrorCode::InternalBusy, "device information"));
    }
    if (m_session.isNull() || m_session->connection().state != ConnectionState::Connected ||
        m_queries->session() != m_session.data()) {
        return std::unexpected(makeError(ErrorCode::TargetNotConnected, "device information"));
    }
    // Plan 0044 / ADR-0440 (D6): never a silent query to a customer's device.
    if (m_session->identity().transport == Transport::Remote) {
        Error error = makeError(ErrorCode::UsageInvalidArgument, "device information: remote");
        setText(error, ErrorText::Message, "hwio::device",
                QT_TRANSLATE_NOOP("hwio::device", "The device information of a remote support "
                                                  "session's device cannot be saved."));
        setText(error, ErrorText::Cause, "hwio::device",
                QT_TRANSLATE_NOOP("hwio::device", "HARDWARIO Monitor never sends commands of its "
                                                  "own to a customer's device."));
        setText(error, ErrorText::Suggestion, "hwio::device",
                QT_TRANSLATE_NOOP("hwio::device", "Ask the customer to save it."));
        return std::unexpected(std::move(error));
    }
    const DeviceProfile& profile = m_session->profile();
    if (profile.infoFileCommands.empty()) {
        Error error = makeError(ErrorCode::UsageInvalidArgument,
                                "device information: " + std::string(profile.id) + " has none");
        setText(
            error, ErrorText::Message, "hwio::device",
            QT_TRANSLATE_NOOP("hwio::device", "This device has no device information to save."));
        return std::unexpected(std::move(error));
    }
    m_commands.clear();
    for (const std::string_view command :
         profile.infoFileCommands.first(std::min(profile.infoFileCommands.size(), maxCommands))) {
        m_commands.emplace_back(command);
    }
    const SessionIdentity identity = m_session->identity();
    m_document = DeviceInfoDocument{
        .serial = {},
        .connectionId = identity.id,
        .deviceName = std::string(profile.displayName),
        .firmware = {},
        .time = m_options.clock ? m_options.clock() : QDateTime::currentDateTime(),
        .monitorVersion = std::string(buildInfo().version),
        .profileId = std::string(profile.id),
        .targetId = std::string(m_session->consoleTargetId()),
        .sections = {},
    };
    m_result.reset();
    m_attach = m_session->connection().attach;
    ++m_run;
    m_running = true;
    qCInfo(log::lcDevice) << "Reading the device information:" << m_commands.size() << "commands";
    emit progressChanged();
    next();
    return {};
}

void DeviceInfoCollector::cancel() {
    if (!m_running) {
        return;
    }
    ++m_run;
    m_running = false;
    m_document = {};
    m_result.reset();
    qCInfo(log::lcDevice) << "Reading the device information cancelled";
    emit progressChanged();
}

void DeviceInfoCollector::next() {
    if (!m_running) {
        return;
    }
    if (done() >= total()) {
        complete();
        return;
    }
    const std::uint64_t run = m_run;
    const QPointer<DeviceInfoCollector> self(this);
    m_queries->run(
        m_commands.at(done()),
        [self, run](const std::optional<ShellQueryResult>& answer) {
            // A cancelled run, a destroyed collector: not ours any more.
            if (!self.isNull() && self->m_running && self->m_run == run) {
                self->onAnswer(answer);
            }
        },
        m_options.commandTimeout);
}

void DeviceInfoCollector::onAnswer(const std::optional<ShellQueryResult>& answer) {
    // A refused or aborted query of a console that left Connected (or came back as another attach),
    // or of a session its owner released from the queue (Disconnect), is a disconnect: nothing is
    // written.
    if (m_session.isNull() || m_queries->session() != m_session.data() ||
        m_session->connection().state != ConnectionState::Connected ||
        m_session->connection().attach != m_attach) {
        fail(disconnectedError());
        return;
    }
    DeviceInfoSection section{
        .command = m_commands.at(done()), .answered = false, .truncated = false, .lines = {}};
    // No output before the timeout, or a query that did not run: "(no answer)". An answer that
    // ended at the prompt with no output is an empty answer.
    if (answer && (!answer->lines.empty() || answer->end != CaptureEnd::Timeout)) {
        section.answered = true;
        section.truncated = answer->truncated;
        section.lines = answer->lines;
        for (std::string& line : section.lines) {
            if (line.ends_with('\r')) {
                line.pop_back();
            }
        }
    }
    qCDebug(log::lcDevice).noquote()
        << "Device information:" << toQString(section.command)
        << (section.answered ? "answered" : "no answer") << section.lines.size() << "lines";
    m_document.sections.push_back(std::move(section));
    emit progressChanged();
    next();
}

void DeviceInfoCollector::onConnection(const ConnectionEvent& event) {
    if (m_running && (event.state != ConnectionState::Connected || event.attach != m_attach)) {
        fail(disconnectedError());
    }
}

void DeviceInfoCollector::fail(Error error) {
    ++m_run;
    m_running = false;
    m_document = {};
    qCInfo(log::lcDevice).noquote()
        << "Reading the device information failed:" << toQString(idOf(error.code));
    m_result = std::unexpected(std::move(error));
    emit progressChanged();
    emit finished();
}

void DeviceInfoCollector::complete() {
    // The header from the first answers that have the fields (CHESTER: `info show`).
    bool productName = false;
    for (const DeviceInfoSection& section : m_document.sections) {
        if (!section.answered) {
            continue;
        }
        const DeviceInfo info = parseInfoShow(section.lines);
        if (m_document.serial.empty()) {
            m_document.serial = std::string(info.deviceSerial());
        }
        if (m_document.firmware.empty() && info.hasFirmware()) {
            m_document.firmware = std::string(info.fwName()) + ' ' + std::string(info.fwVersion());
        }
        if (const auto product = info.value("product-name");
            !productName && product && !product->empty()) {
            m_document.deviceName = std::string(*product);
            productName = true;
        }
    }
    ++m_run;
    m_running = false;
    qCInfo(log::lcDevice) << "Device information read:" << m_document.sections.size() << "commands,"
                          << m_document.unanswered().size() << "without an answer";
    m_result = std::move(m_document);
    m_document = {};
    emit progressChanged();
    emit finished();
}

Result<DeviceInfoDocument> collectDeviceInfo(ConsoleSession& session, ShellQueryQueue& queries,
                                             std::stop_token stop,
                                             DeviceInfoCollectorOptions options) {
    const std::chrono::milliseconds perCommand =
        options.commandTimeout + ShellQueryQueue::maxIdleWait;
    DeviceInfoCollector collector(session, queries, std::move(options));
    if (Status started = collector.start(); !started) {
        return std::unexpected(std::move(started).error());
    }
    if (!collector.result()) {
        QEventLoop loop;
        QObject::connect(&collector, &DeviceInfoCollector::finished, &loop, &QEventLoop::quit);
        QTimer guard; // every command's timeout and idle wait, and a margin
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        guard.start(perCommand * static_cast<int>(collector.total()) + std::chrono::seconds(5));
        QTimer poll; // Ctrl+C
        QObject::connect(&poll, &QTimer::timeout, &loop, [&loop, &stop] {
            if (stop.stop_requested()) {
                loop.quit();
            }
        });
        poll.start(std::chrono::milliseconds(50));
        loop.exec();
    }
    if (const auto& result = collector.result()) {
        return *result;
    }
    collector.cancel();
    if (stop.stop_requested()) {
        return std::unexpected(makeError(ErrorCode::Cancelled, "device information"));
    }
    return std::unexpected(makeError(ErrorCode::Timeout, "device information"));
}

} // namespace hwio::device
