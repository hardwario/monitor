#include "hwio/remote/transcript.h"

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/core/text_sanitizer.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <QtCore/qnamespace.h>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>

namespace hwio::remote {

namespace {

std::size_t utf8Bytes(const QString& text) {
    return static_cast<std::size_t>(text.toUtf8().size());
}

} // namespace

Transcript::Transcript(TranscriptLimits limits) : m_limits(limits) {}

QString Transcript::isoTimestamp(std::chrono::system_clock::time_point time) {
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(Qt::ISODateWithMs);
}

void Transcript::addCommand(const QString& line, std::chrono::system_clock::time_point time) {
    const QString timestamp = isoTimestamp(time);
    const std::int64_t index = m_nextIndex;
    push(TranscriptEntry{.id = QStringLiteral("%1-%2").arg(timestamp).arg(index),
                         .timestamp = timestamp,
                         .command = line,
                         .index = index,
                         .outputs = {},
                         .bytes = utf8Bytes(line)});
}

void Transcript::addOutput(const QString& line, std::chrono::system_clock::time_point time) {
    if (m_entries.empty()) {
        const QString timestamp = isoTimestamp(time);
        const QString command = QString::fromLatin1(deviceOutputCommand);
        push(TranscriptEntry{.id = QStringLiteral("system-%1").arg(timestamp),
                             .timestamp = timestamp,
                             .command = command,
                             .index = 0,
                             .outputs = {},
                             .bytes = utf8Bytes(command)});
    }
    TranscriptEntry& last = m_entries.back();
    const std::size_t bytes = utf8Bytes(line);
    last.outputs.append(line);
    last.bytes += bytes;
    m_bytes += bytes;
    enforce();
}

void Transcript::push(TranscriptEntry entry) {
    m_bytes += entry.bytes;
    m_entries.push_back(std::move(entry));
    // The web host's index is prev.length (the synthetic entry is index 0 and counts).
    m_nextIndex = m_entries.back().index + 1;
    enforce();
}

void Transcript::enforce() {
    while (m_entries.size() > m_limits.maxEntries ||
           (m_bytes > m_limits.maxBytes && m_entries.size() > 1)) {
        m_bytes -= m_entries.front().bytes;
        m_entries.pop_front();
        m_truncated = true;
    }
    // One entry over the byte bound: its oldest output lines go.
    while (m_bytes > m_limits.maxBytes && !m_entries.empty() &&
           !m_entries.back().outputs.isEmpty()) {
        TranscriptEntry& only = m_entries.back();
        const std::size_t bytes = utf8Bytes(only.outputs.front());
        only.outputs.removeFirst();
        only.bytes -= bytes;
        m_bytes -= bytes;
        m_truncated = true;
    }
}

QJsonArray Transcript::toJson() const {
    QJsonArray commands;
    for (const TranscriptEntry& entry : m_entries) {
        QJsonObject response;
        response.insert(QStringLiteral("outputs"), QJsonArray::fromStringList(entry.outputs));
        response.insert(QStringLiteral("result"), 0);
        QJsonObject command;
        command.insert(QStringLiteral("id"), entry.id);
        command.insert(QStringLiteral("timestamp"), entry.timestamp);
        command.insert(QStringLiteral("command"), entry.command);
        command.insert(QStringLiteral("index"), static_cast<double>(entry.index));
        command.insert(QStringLiteral("response"), response);
        commands.append(command);
    }
    return commands;
}

Result<Transcript> Transcript::fromJson(const QJsonValue& commands, TranscriptLimits limits) {
    if (!commands.isArray()) {
        return std::unexpected(
            makeError(ErrorCode::RemoteProtocolError, "sync commands is not an array"));
    }
    Transcript transcript(limits);
    const QJsonArray array = commands.toArray();
    // Only the newest maxEntries can survive: skip the rest without parsing.
    const qsizetype first = array.size() > static_cast<qsizetype>(limits.maxEntries)
                                ? array.size() - static_cast<qsizetype>(limits.maxEntries)
                                : 0;
    transcript.m_truncated = first > 0;
    for (qsizetype i = first; i < array.size(); ++i) {
        const QJsonObject object = array.at(i).toObject();
        const QJsonValue command = object.value(QStringLiteral("command"));
        if (!command.isString()) {
            continue;
        }
        TranscriptEntry entry;
        entry.command = sanitizeDeviceText(command.toString());
        entry.timestamp = object.value(QStringLiteral("timestamp")).toString().left(40);
        entry.id = object.value(QStringLiteral("id")).toString().left(80);
        entry.index = static_cast<std::int64_t>(object.value(QStringLiteral("index")).toDouble(0));
        entry.bytes = utf8Bytes(entry.command);
        const QJsonArray outputs = object.value(QStringLiteral("response"))
                                       .toObject()
                                       .value(QStringLiteral("outputs"))
                                       .toArray();
        for (const auto& output : outputs) {
            if (output.isString()) {
                const QString line = sanitizeDeviceText(output.toString());
                entry.bytes += utf8Bytes(line);
                entry.outputs.append(line);
            }
        }
        transcript.m_bytes += entry.bytes;
        transcript.m_entries.push_back(std::move(entry));
        transcript.enforce();
    }
    return transcript;
}

} // namespace hwio::remote
