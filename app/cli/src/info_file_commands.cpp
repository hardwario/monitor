#include "info_file_commands.h"

#include "cli_app.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/localized_text.h"
#include "hwio/core/result.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_info_file.h"
#include "hwio/device/shell_query.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace hwio::cli {

namespace {

namespace fs = std::filesystem;

fs::path toPath(const std::string& text) { return {toQString(text).toStdU16String()}; }

// An existing file or symlink at `path` (never replaced, as `uicr read` without --force).
Status refuseExisting(const fs::path& path) {
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(path, ec);
    if (!fs::exists(status)) {
        return {};
    }
    if (fs::is_symlink(status)) {
        return std::unexpected(makeError(ErrorCode::CliFileIsSymlink, toUtf8(path)));
    }
    Error error = makeError(ErrorCode::IoFileExists, toUtf8(path));
    setUntranslatedText(error, ErrorText::Suggestion,
                        "Give another file name, or a folder to get a new name with the time.");
    return std::unexpected(std::move(error));
}

bool isDirectory(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec); // follows a symlink to a folder
}

} // namespace

Status checkInfoSavePath(const InfoSaveOptions& options) {
    if (options.path.empty()) {
        return {};
    }
    const fs::path path = toPath(options.path);
    return isDirectory(path) ? Status{} : refuseExisting(path);
}

Result<CommandResult> saveDeviceInfo(const Context& context, device::ConsoleSession& session,
                                     device::ShellQueryQueue& queries,
                                     const InfoSaveOptions& options) {
    auto document = device::collectDeviceInfo(session, queries, context.stop);
    if (!document) {
        return std::unexpected(std::move(document).error());
    }
    const fs::path given = toPath(options.path);
    fs::path file;
    if (options.path.empty()) {
        file = toPath(device::deviceInfoFileName(*document));
    } else if (isDirectory(given)) {
        file = given / toPath(device::deviceInfoFileName(*document));
    } else {
        file = given;
    }
    if (const Status free = refuseExisting(file); !free) {
        return std::unexpected(free.error());
    }
    if (const Status written = device::writeDeviceInfoFile(
            file, *document, device::DeviceInfoFileOptions{.withSecrets = options.withSecrets});
        !written) {
        return std::unexpected(written.error());
    }
    std::error_code ec;
    const fs::path absolute = fs::absolute(file, ec);
    const std::string shown = toUtf8(ec ? file : absolute);

    QJsonArray commands;
    for (const device::DeviceInfoSection& section : document->sections) {
        QJsonObject row;
        row.insert(QStringLiteral("command"), toQString(section.command));
        row.insert(QStringLiteral("answered"), section.answered);
        row.insert(QStringLiteral("lines"), static_cast<qint64>(section.lines.size()));
        commands.append(row);
    }
    QJsonObject data;
    data.insert(QStringLiteral("path"), toQString(shown));
    data.insert(QStringLiteral("serial"), document->serial.empty()
                                              ? QJsonValue(QJsonValue::Null)
                                              : QJsonValue(toQString(document->serial)));
    data.insert(QStringLiteral("commands"), commands);

    std::string human = "Device information saved to " + shown;
    const std::vector<std::string> unanswered = document->unanswered();
    if (!unanswered.empty()) {
        human += "\nNo answer from the device to:";
        for (const std::string& command : unanswered) {
            human += " `" + command + "`";
        }
    }
    if (document->serial.empty()) {
        human +=
            "\nThe device reported no serial number; the file name uses " +
            (document->connectionId.empty() ? std::string("\"unknown\"") : document->connectionId) +
            " instead.";
    }
    return CommandResult{.data = data, .humanText = std::move(human), .streamed = false};
}

} // namespace hwio::cli
