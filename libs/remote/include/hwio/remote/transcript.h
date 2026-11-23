#pragma once

// Transcript - the console history in the web Terminal's format (~/terminal/src/components/
// console/console.tsx, plan 0044 § Wire protocol): a list of commands, each with the device output
// lines that followed it,
//   {id, timestamp, command, index, response: {outputs: [string], result: 0}}
// id "<timestamp>-<index>", timestamp ISO 8601 UTC with milliseconds ("2026-11-10T12:34:56.789Z"),
// index the position since the start (the web host: prev.length). Output before the first command
// goes to a synthetic entry {id: "system-<timestamp>", command: "(device output)", index: 0}.
//
// Host: the sync snapshot sent to an allowed participant. Joiner: parsing a received sync. Both
// bounded (plan 0044 § Limits): at most maxEntries entries and maxBytes UTF-8 bytes of text; the
// oldest entries go first, and a single entry over the byte bound loses its oldest output lines.
// Not thread-safe (one owner).

#include "hwio/core/result.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

namespace hwio::remote {

inline constexpr std::string_view deviceOutputCommand = "(device output)";

struct TranscriptLimits {
    std::size_t maxEntries = 500;
    std::size_t maxBytes = 256ULL * 1024;
};

struct TranscriptEntry {
    QString id;
    QString timestamp;
    QString command;
    std::int64_t index = 0;
    QStringList outputs;
    std::size_t bytes = 0; // UTF-8 bytes of command + outputs
};

class Transcript {
public:
    explicit Transcript(TranscriptLimits limits = {});

    // web host handleWriteLocal(line)
    void addCommand(const QString& line, std::chrono::system_clock::time_point time);
    // web host handleReadLocal(line)
    void addOutput(const QString& line, std::chrono::system_clock::time_point time);

    [[nodiscard]] const std::deque<TranscriptEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] std::size_t bytes() const noexcept { return m_bytes; }
    // The sync `commands` array.
    [[nodiscard]] QJsonArray toJson() const;
    // A received sync `commands` array (the joiner): invalid entries are skipped, the limits apply
    // (oldest first). Errors: remote.protocol_error (not an array).
    [[nodiscard]] static Result<Transcript> fromJson(const QJsonValue& commands,
                                                     TranscriptLimits limits = {});
    // True when fromJson() dropped entries or lines because of the limits.
    [[nodiscard]] bool truncated() const noexcept { return m_truncated; }

    [[nodiscard]] static QString isoTimestamp(std::chrono::system_clock::time_point time);

private:
    void push(TranscriptEntry entry);
    void enforce();

    TranscriptLimits m_limits;
    std::deque<TranscriptEntry> m_entries;
    std::size_t m_bytes = 0;
    std::int64_t m_nextIndex = 0;
    bool m_truncated = false;
};

} // namespace hwio::remote
