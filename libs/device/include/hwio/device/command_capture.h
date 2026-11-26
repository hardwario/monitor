#pragma once

// CommandCapture - one shell command and its output, ended by the ADR-0104 rule (ADR-0175, plan
// 0021 phase 2). Shared by the MCP tool send_command and ShellQuery.
//
// start() sends the line and collects the shell-channel lines that follow its echo (output before
// the echo belongs to an earlier command; a shell that echoes the typed line itself has that copy
// dropped). The capture ends
//   - at the shell prompt (ConsoleSession::promptSeen) when the session has one (shellPromptOf)
//     and CaptureOptions::forceSilence is not set,
//   - otherwise after `silence` without new output once output has started,
//   - and always at `timeout`.
// Echoes of other commands after the echo (dir In) are kept and set `interleaved` (ADR-0105).
// Output is bounded by `maxLines` / `maxBytes` (text bytes): further lines are dropped and
// `truncated` is set.
//
// Observe mode (default, MCP send_command): the lines are read from ConsoleSession::linesReceived
// (they stay in the console log); the echo is recognised by its text. A line held by a running
// silent capture starts its timeout only when the hold ends (review P8).
// Silent mode (ShellQuery): the capture is installed with ConsoleSession::setShellCapture, so the
// command's echo and output never reach the log; other users' lines are held until the capture
// ends. The echo is recognised by origin (ConsoleLine::origin, review P1); echoes of other lines
// are never taken. A connection state other than Connected aborts it (aborted(), no result).
//
// Plan 0062 (ADR-0437, ADR-0438): start() with CaptureHooks observes like observe mode, but sends
// the command through the caller's `send` (an AI command on a remote tab goes through the remote
// tab, not ConsoleSession::sendLine) and looks at every line only through `view` (the hidden view
// of a remote tab: the capture, its echo match and its result never see the raw line).
//
// Thread safety: owner thread of the session. The session must outlive the capture or the capture
// is cancelled first (it holds the session only as QPointer and stops when it goes away).

#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_session.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::device {

enum class CaptureEnd : std::uint8_t { Prompt, Silence, Timeout };

// "prompt", "silence", "timeout" (the MCP send_command `end` values).
[[nodiscard]] std::string_view captureEndId(CaptureEnd end) noexcept;

// The shell prompt the session reports with promptSeen: BLE BleProfile::shellPrompt, else the
// console target's TargetProfile::shellPrompt ("" = none: the silence rule).
[[nodiscard]] std::string_view shellPromptOf(const ConsoleSession& session);

struct CaptureOptions {
    std::chrono::milliseconds timeout{2000};
    std::chrono::milliseconds silence{300}; // ADR-0104
    std::size_t maxLines = 2000;
    std::size_t maxBytes = std::size_t{256} * 1024;
    bool silent = false;
    // Plan 0062 (ADR-0438): end by the silence rule even when the session's profile has a shell
    // prompt (a remote session never reports one).
    bool forceSilence = false;
};

// Plan 0062: see the header comment. Both are required.
struct CaptureHooks {
    // Sends the command (called once, after the capture listens); its error fails start().
    std::function<Status(std::string)> send;
    // The line as the capture may see it; the echo is matched against the view of the command
    // as a Direction::In shell line.
    std::function<ConsoleLine(const ConsoleLine&)> view;
};

struct CaptureResult {
    std::vector<ConsoleLine> lines; // output (dir Out) and interleaved echoes (dir In)
    CaptureEnd end = CaptureEnd::Timeout;
    bool truncated = false;
    bool interleaved = false;
};

class CommandCapture : public QObject {
    Q_OBJECT

    struct Passkey {
        explicit Passkey() = default;
    };

public:
    // Sends `command`. Errors: those of ConsoleSession::sendLine; internal.busy in silent mode
    // when another silent capture runs on the session.
    [[nodiscard]] static Result<std::unique_ptr<CommandCapture>>
    start(ConsoleSession& session, std::string command, CaptureOptions options);
    // Observe mode with the caller's sender and line view (plan 0062). Errors: those of
    // `hooks.send`; internal.unexpected for silent options or missing hooks.
    [[nodiscard]] static Result<std::unique_ptr<CommandCapture>>
    start(ConsoleSession& session, std::string command, CaptureOptions options, CaptureHooks hooks);

    CommandCapture(Passkey key, ConsoleSession& session, std::string command,
                   CaptureOptions options);
    ~CommandCapture() override; // cancels a running capture
    CommandCapture(const CommandCapture&) = delete;
    CommandCapture& operator=(const CommandCapture&) = delete;
    CommandCapture(CommandCapture&&) = delete;
    CommandCapture& operator=(CommandCapture&&) = delete;

    [[nodiscard]] bool isFinished() const noexcept { return m_result.has_value(); }
    [[nodiscard]] const std::optional<CaptureResult>& result() const noexcept { return m_result; }
    // The command's echo arrived (plan 0062: a remote command without an echo was not delivered).
    [[nodiscard]] bool echoSeen() const noexcept { return m_echoSeen; }
    // Stops collecting without a result (no finished()); removes a silent capture.
    void cancel();

signals:
    // Once, with result() set.
    void finished();
    // Silent mode: the console left Connected; no result, no finished().
    void aborted();

private:
    bool offer(const ConsoleLine& line);           // true = the line belongs to the capture
    void observe(const QList<ConsoleLine>& lines); // observe mode: linesReceived
    void append(const ConsoleLine& line);
    void end(CaptureEnd reason);
    void stop();
    void startDeadline();
    [[nodiscard]] static Result<std::unique_ptr<CommandCapture>>
    startObserving(ConsoleSession& session, std::string command, CaptureOptions options,
                   CaptureHooks hooks);

    QPointer<ConsoleSession> m_session;
    std::string m_command;
    CaptureOptions m_options;
    std::function<ConsoleLine(const ConsoleLine&)> m_view; // plan 0062; empty = the line itself
    bool m_promptMode = false;
    std::uint64_t m_startSeq = 0;
    std::uint64_t m_origin = 0; // silent mode: the token of our echo
    QTimer m_deadline;
    QTimer m_silence;
    CaptureResult m_collected;
    std::size_t m_bytes = 0;
    bool m_echoSeen = false;
    bool m_outputStarted = false;
    bool m_captureInstalled = false;
    bool m_stopped = false;
    std::optional<CaptureResult> m_result;
};

} // namespace hwio::device
