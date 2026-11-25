#include "workers/batch_runner.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/localized_text.h"
#include "hwio/core/result.h"
#include "hwio/device/command_history.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QList>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QtContainerFwd>
#include <QtTranslation>

#include <chrono>
#include <expected>
#include <optional>
#include <utility>

namespace hwio::gui {

namespace {

Error batchError(ErrorCode code, const LocalizedText& message, const LocalizedText& cause,
                 const LocalizedText& suggestion, const QString& detail) {
    Error error = makeError(code, detail.toStdString());
    setText(error, ErrorText::Message, message);
    setText(error, ErrorText::Cause, cause);
    setText(error, ErrorText::Suggestion, suggestion);
    return error;
}

} // namespace

BatchRunner::BatchRunner(QObject* parent) : QObject(parent) {
    m_timer.setInterval(defaultInterval);
    connect(&m_timer, &QTimer::timeout, this, &BatchRunner::tick);
}

void BatchRunner::setInterval(std::chrono::milliseconds interval) { m_timer.setInterval(interval); }

Result<QStringList> BatchRunner::load(const QString& path) {
    const auto unreadable = [&path](const QString& reason) {
        return batchError(
            ErrorCode::IoReadFailed,
            localizedText(
                "hwio::gui::BatchRunner",
                QT_TRANSLATE_NOOP("hwio::gui::BatchRunner", "The batch file could not be read.")),
            localizedText(
                "hwio::gui::BatchRunner",
                QT_TRANSLATE_NOOP("hwio::gui::BatchRunner",
                                  "the file is missing or you do not have permission to read it.")),
            localizedText("hwio::gui::BatchRunner",
                          QT_TRANSLATE_NOOP("hwio::gui::BatchRunner", "Choose another file.")),
            path + QStringLiteral(": ") + reason);
    };
    const QFileInfo info(path);
    if (!info.isFile()) {
        return std::unexpected(unreadable(QStringLiteral("not a file")));
    }
    if (info.size() > maxFileBytes) {
        return std::unexpected(batchError(
            ErrorCode::UsageInvalidArgument,
            localizedText("hwio::gui::BatchRunner",
                          QT_TRANSLATE_NOOP("hwio::gui::BatchRunner",
                                            "The batch file is too large (max. %1 MiB)."),
                          {maxFileBytes / (1024LL * 1024)}),
            {},
            localizedText(
                "hwio::gui::BatchRunner",
                QT_TRANSLATE_NOOP("hwio::gui::BatchRunner", "Split it into smaller files.")),
            QStringLiteral("%1: %2 bytes").arg(path).arg(info.size())));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(unreadable(file.errorString()));
    }
    // Bounded read (the file may have grown since the size check).
    const QByteArray bytes = file.read(maxFileBytes + 1);
    if (bytes.size() > maxFileBytes) {
        return std::unexpected(batchError(
            ErrorCode::UsageInvalidArgument,
            localizedText("hwio::gui::BatchRunner",
                          QT_TRANSLATE_NOOP("hwio::gui::BatchRunner",
                                            "The batch file is too large (max. %1 MiB)."),
                          {maxFileBytes / (1024LL * 1024)}),
            {},
            localizedText(
                "hwio::gui::BatchRunner",
                QT_TRANSLATE_NOOP("hwio::gui::BatchRunner", "Split it into smaller files.")),
            path));
    }
    QStringList commands;
    for (const QByteArray& raw : bytes.split('\n')) {
        QString line = QString::fromUtf8(raw);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (commands.size() >= maxCommands) {
            return std::unexpected(batchError(
                ErrorCode::UsageInvalidArgument,
                localizedText("hwio::gui::BatchRunner",
                              QT_TRANSLATE_NOOP("hwio::gui::BatchRunner",
                                                "The batch file is too large (max. %1 commands)."),
                              {LocalizedText{.translated = QLocale().toString(maxCommands),
                                             .english = QString::number(maxCommands)}}),
                {},
                localizedText(
                    "hwio::gui::BatchRunner",
                    QT_TRANSLATE_NOOP("hwio::gui::BatchRunner", "Split it into smaller files.")),
                path));
        }
        if (std::cmp_greater(line.toUtf8().size(), device::CommandHistory::maxEntryBytes)) {
            return std::unexpected(batchError(
                ErrorCode::UsageInvalidArgument,
                localizedText(
                    "hwio::gui::BatchRunner",
                    QT_TRANSLATE_NOOP("hwio::gui::BatchRunner",
                                      "The batch file contains a command longer than %1 bytes."),
                    {device::CommandHistory::maxEntryBytes}),
                {},
                localizedText(
                    "hwio::gui::BatchRunner",
                    QT_TRANSLATE_NOOP("hwio::gui::BatchRunner", "Split it into smaller files.")),
                QStringLiteral("%1: command %2").arg(path).arg(commands.size() + 1)));
        }
        commands.append(line);
    }
    if (commands.isEmpty()) {
        return std::unexpected(batchError(
            ErrorCode::UsageInvalidArgument,
            localizedText("hwio::gui::BatchRunner",
                          QT_TRANSLATE_NOOP("hwio::gui::BatchRunner",
                                            "The batch file contains no commands.")),
            localizedText("hwio::gui::BatchRunner",
                          QT_TRANSLATE_NOOP("hwio::gui::BatchRunner",
                                            "every line is empty or a comment (#).")),
            localizedText("hwio::gui::BatchRunner",
                          QT_TRANSLATE_NOOP("hwio::gui::BatchRunner", "Choose another file.")),
            path));
    }
    return commands;
}

void BatchRunner::start(QStringList commands, SendFunction send) {
    if (running()) {
        stop();
    }
    m_commands = std::move(commands);
    m_send = std::move(send);
    m_next = 0;
    progressChanged(); // signal
    if (m_commands.isEmpty() || !m_send) {
        end(std::nullopt);
        return;
    }
    m_timer.start();
    tick();
}

void BatchRunner::stop() {
    if (!running()) {
        return;
    }
    end(std::nullopt);
}

void BatchRunner::tick() {
    if (m_next >= m_commands.size()) {
        end(std::nullopt);
        return;
    }
    const Status sent = m_send(toStdString(m_commands.at(m_next)));
    if (!sent) {
        if (sent.error().code == ErrorCode::InternalQueueFull) {
            return; // the same command again on the next tick
        }
        end(sent.error());
        return;
    }
    ++m_next;
    progressChanged(); // signal
    if (m_next >= m_commands.size()) {
        end(std::nullopt);
    }
}

void BatchRunner::end(const std::optional<Error>& error) {
    m_timer.stop();
    m_send = {};
    finished(error); // signal
}

} // namespace hwio::gui
