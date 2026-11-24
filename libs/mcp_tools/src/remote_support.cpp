#include "remote_support.h"

#include "hwio/core/error.h"
#include "hwio/core/localized_text.h"
#include "hwio/core/result.h"
#include "hwio/device/command_capture.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_redaction.h"
#include "hwio/mcp/consent.h"
#include "hwio/mcp/limits.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/tool_call.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/tools_context.h"
#include "tool_support.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::mcp::tools::detail {

namespace {

using hwio::device::ConsoleLine;

// ADR-0434: the tools an AI client may use on a remote device, and those that stay human.
constexpr std::array<std::string_view, 9> allowedTools{
    "status",       "read_terminal", "read_log",  "wait_for_state", "wait_for_connection",
    "send_command", "flash",         "flash_lte", "reset"};
constexpr std::array<std::string_view, 3> humanTools{"connect", "disconnect", "reconnect"};

// MCP error texts are plain English (ADR-0103).
hwio::Error toolNotAllowedOnRemote(std::string_view tool) {
    hwio::Error error = makeError(ErrorCode::McpToolNotAllowed, std::string(tool));
    setUntranslatedText(error, ErrorText::Message,
                        std::format("The tool {} cannot be used on a customer's device.", tool));
    clearText(error, ErrorText::Cause);
    setUntranslatedText(error, ErrorText::Suggestion,
                        "Joining, leaving and reconnecting a support session is done by the "
                        "support person in HARDWARIO Monitor.");
    return error;
}

hwio::Error capabilityMissingOnRemote(const std::string& id) {
    hwio::Error error = makeError(ErrorCode::McpCapabilityMissing, id);
    setUntranslatedText(error, ErrorText::Message,
                        "This tool is not available on a customer's device in a support session.");
    clearText(error, ErrorText::Cause);
    setUntranslatedText(error, ErrorText::Suggestion,
                        "Use send_command, read_log, read_terminal, flash (catalog_id), flash_lte "
                        "or reset, or ask the support person.");
    return error;
}

hwio::Error remoteInvalid(const QString& message, std::string detail) {
    return invalidArgument(message, std::move(detail));
}

std::chrono::milliseconds timeoutArgument(const QJsonObject& args,
                                          std::chrono::milliseconds fallback,
                                          const hwio::mcp::ToolLimits& limits) {
    if (!args.contains(QStringLiteral("timeout"))) {
        return fallback;
    }
    const double value = args.value(QStringLiteral("timeout")).toDouble();
    const auto ms =
        std::chrono::milliseconds(static_cast<std::int64_t>(std::llround(value * 1000)));
    return std::clamp(ms, limits.sendCommandMinTimeout, limits.sendCommandMaxTimeout);
}

QString trimmed(const QString& text) { return text.trimmed(); }

// ---- the gate ---------------------------------------------------------------------------------

class RemoteGateCall final : public hwio::mcp::ToolCall {
public:
    RemoteGateCall(hwio::mcp::RequestContext context, QJsonObject arguments,
                   hwio::mcp::ToolEnv& env, ResolvedDevice device, hwio::mcp::ToolStart next)
        : m_env(&env), m_context(std::move(context)), m_arguments(std::move(arguments)),
          m_id(std::move(device.id)), m_ref(std::move(device.ref)), m_next(std::move(next)) {
        setDeviceId(m_id);
        setActivityArguments(visibleArguments(m_ref, m_arguments));
        IRemoteDevice* remote = m_ref.remote.data();
        if (remote == nullptr) {
            finish(std::unexpected(deviceNotOpen(m_id)));
            return;
        }
        // ADR-0436: Stop AI (or the grant ending otherwise) ends the client's running calls.
        connect(remote, &IRemoteDevice::aiRevoked, this, [this](const QString& clientKey) {
            if (clientKey.isEmpty() || clientKey.toStdString() == m_context.clientKey) {
                cancel();
            }
        });
        // The tab left the session: the grant ends with it (ADR-0436), a running call too.
        connect(remote, &QObject::destroyed, this, [this] {
            if (isFinished()) {
                return;
            }
            if (m_inner && !m_inner->isFinished()) {
                disconnect(m_inner.get(), nullptr, this, nullptr);
                m_inner->cancel();
            }
            finish(std::unexpected(deviceNotOpen(m_id)));
        });
        if (remote->aiAllowed(m_context.clientKey)) {
            run();
            return;
        }
        QJsonObject details;
        details.insert(QStringLiteral("device"), qs(m_id));
        details.insert(QStringLiteral("remote"), true);
        hwio::mcp::ConsentRequest request = hwio::mcp::makeConsentRequest(
            m_context, hwio::mcp::ConsentCategory::RemoteDevice, m_id, details);
        request.remoteDevice = true;
        requestConsent(env.consentBroker(), std::move(request),
                       [this](const hwio::mcp::ConsentDecision& decision) { decided(decision); });
    }
    ~RemoteGateCall() override = default;
    RemoteGateCall(const RemoteGateCall&) = delete;
    RemoteGateCall& operator=(const RemoteGateCall&) = delete;
    RemoteGateCall(RemoteGateCall&&) = delete;
    RemoteGateCall& operator=(RemoteGateCall&&) = delete;

protected:
    void onCancel() override {
        if (m_inner && !m_inner->isFinished()) {
            m_inner->cancel(); // its result (cancelled) is forwarded
        }
        if (!isFinished()) {
            ToolCall::onCancel();
        }
    }

private:
    void decided(const hwio::mcp::ConsentDecision& decision) {
        if (!decision) {
            finish(std::unexpected(decision.error()));
            return;
        }
        // ADR-0436: only a person's answer to this prompt allows it, never a rule or a flag.
        if (*decision != hwio::mcp::ConsentGrant::Prompted) {
            finish(std::unexpected(makeError(ErrorCode::McpConsentDenied, m_context.tool)));
            return;
        }
        if (m_ref.remote.isNull()) {
            finish(std::unexpected(deviceNotOpen(m_id)));
            return;
        }
        m_ref.remote->allowAi(m_context);
        run();
    }

    void run() {
        if (isFinished()) {
            return;
        }
        if (!m_ref.alive()) {
            finish(std::unexpected(deviceNotOpen(m_id)));
            return;
        }
        m_inner = m_next(m_context, m_arguments, *m_env);
        if (!m_inner) {
            finish(std::unexpected(makeError(ErrorCode::InternalUnexpected, "no call")));
            return;
        }
        hwio::mcp::ToolCall* inner = m_inner.get();
        if (inner->isFinished()) {
            forward(inner->result().value_or(
                std::unexpected(makeError(ErrorCode::InternalUnexpected))));
            return;
        }
        connect(inner, &hwio::mcp::ToolCall::finished, this,
                [this](const hwio::Result<hwio::mcp::ToolResult>& result) { forward(result); });
        connect(inner, &hwio::mcp::ToolCall::progress, this,
                [this](double done, std::optional<double> total, const QString& message) {
                    reportProgress(done, total, message);
                });
    }

    void forward(hwio::Result<hwio::mcp::ToolResult> result) {
        if (isFinished()) {
            return;
        }
        adoptConsent(*m_inner);
        if (result && !result->isError) {
            QJsonObject structured = result->structured;
            structured.insert(QStringLiteral("remote"), true); // ADR-0434
            result = hwio::mcp::toolResult(structured);
        }
        finish(std::move(result));
    }

    hwio::mcp::ToolEnv* m_env; // non-owning, the server's
    hwio::mcp::RequestContext m_context;
    QJsonObject m_arguments;
    std::string m_id;
    DeviceRef m_ref;
    hwio::mcp::ToolStart m_next;
    std::unique_ptr<hwio::mcp::ToolCall> m_inner;
};

// ---- send_command (ADR-0438) ----------------------------------------------------------------

class RemoteSendCommandCall final : public DeviceCall {
public:
    RemoteSendCommandCall(ToolsContext& tools, const hwio::mcp::RequestContext& context,
                          hwio::mcp::ToolEnv& env, ResolvedDevice device, std::string command,
                          std::chrono::milliseconds timeout)
        : DeviceCall(tools, context, env, std::move(device)), m_command(std::move(command)) {
        IRemoteDevice* remote = ref().remote.data();
        if (remote == nullptr || !alive()) {
            completeError(deviceNotOpen(id()));
            return;
        }
        if (hwio::Status checked = checkRemoteCommand(id(), *remote, m_command, limits());
            !checked) {
            completeError(std::move(checked).error());
            return;
        }
        if (hwio::Status admitted = this->tools().remoteCommands().begin(id(), limits());
            !admitted) {
            completeError(std::move(admitted).error());
            return;
        }
        m_slot = true;
        m_visible = visibleCommand(ref(), m_command);
        provider().marker(id(), markerStart(this->context(), QStringLiteral("send_command")));
        provider().setAiCommandHint(id(), true);
        m_hint = true;
        m_start = ref().console->log().lastSeq();
        connect(remote, &IRemoteDevice::lineRefused, this,
                [this](const QString& code) { refused(code); });
        m_echo.setSingleShot(true);
        m_echo.setInterval(limits().remoteEchoTimeout);
        connect(&m_echo, &QTimer::timeout, this, [this] { echoTimeout(); });
        const QPointer<IRemoteDevice> target(remote);
        const hwio::mcp::RequestContext who = this->context();
        const DeviceRef view = ref();
        auto capture = hwio::device::CommandCapture::start(
            *ref().console, m_command,
            hwio::device::CaptureOptions{.timeout = timeout,
                                         .silence = limits().remoteCommandSilence,
                                         .maxLines = limits().sendCommandMaxLines,
                                         .maxBytes = limits().sendCommandMaxBytes,
                                         .silent = false,
                                         .forceSilence = true},
            hwio::device::CaptureHooks{
                .send = [target, who](std::string line) -> hwio::Status {
                    if (target.isNull()) {
                        return std::unexpected(makeError(ErrorCode::McpDeviceNotOpen));
                    }
                    return target->sendLine(std::move(line), who);
                },
                .view = [view](const ConsoleLine& line) { return visibleLine(view, line); }});
        if (isFinished()) { // refused while sending
            if (capture) {
                (*capture)->cancel();
            }
            return;
        }
        if (!capture) {
            fail(std::move(capture).error());
            return;
        }
        m_capture = std::move(*capture);
        connect(m_capture.get(), &hwio::device::CommandCapture::finished, this, [this] { end(); });
        if (!m_capture->echoSeen()) {
            m_echo.start();
        }
    }

    ~RemoteSendCommandCall() override { release(); }
    RemoteSendCommandCall(const RemoteSendCommandCall&) = delete;
    RemoteSendCommandCall& operator=(const RemoteSendCommandCall&) = delete;
    RemoteSendCommandCall(RemoteSendCommandCall&&) = delete;
    RemoteSendCommandCall& operator=(RemoteSendCommandCall&&) = delete;

protected:
    void onCancel() override {
        stop();
        release();
        ToolCall::onCancel();
    }
    void deviceGone() override {
        stop();
        m_hint = false; // the device is gone; nothing to clear
        release();
    }

private:
    void echoTimeout() {
        if (isFinished() || (m_capture && m_capture->echoSeen())) {
            return;
        }
        // ADR-0438: the host dropped the line (not allowed yet, too many lines, busy).
        fail(makeError(ErrorCode::RemoteCommandNotDelivered, "no echo"));
    }

    void refused(const QString& code) {
        if (isFinished() || (m_capture && m_capture->echoSeen())) {
            return; // a refused line is never echoed: this one was not ours
        }
        const auto known = errorCodeFromId(code.toStdString());
        fail(makeError(known.value_or(ErrorCode::RemoteCommandRefused), "refused by the host"));
    }

    void end() {
        if (isFinished() || !m_capture || !m_capture->result()) {
            return;
        }
        if (!m_capture->echoSeen()) {
            // ADR-0438: a dropped command is an error, also when `timeout` ends it first.
            fail(makeError(ErrorCode::RemoteCommandNotDelivered, "no echo"));
            return;
        }
        const hwio::device::CaptureResult& captured = *m_capture->result();
        m_echo.stop();
        clearHint();
        if (!alive()) {
            release();
            completeError(deviceNotOpen(id()));
            return;
        }
        QJsonArray output;
        for (const ConsoleLine& line : captured.lines) { // already the hidden view
            QJsonObject item;
            item.insert(QStringLiteral("dir"), qs(hwio::device::directionId(line.dir)));
            item.insert(QStringLiteral("text"), qs(hwio::device::englishText(line)));
            output.append(item);
        }
        const std::uint64_t cursor = ref().console->log().lastSeq();
        QJsonObject result;
        result.insert(QStringLiteral("command"), m_visible);
        result.insert(QStringLiteral("output"), output);
        result.insert(QStringLiteral("end"), qs(hwio::device::captureEndId(captured.end)));
        result.insert(QStringLiteral("interleaved"), captured.interleaved);
        result.insert(QStringLiteral("log_cursor"), seqJson(m_start));
        result.insert(QStringLiteral("log_cursor_end"), seqJson(cursor));
        result.insert(QStringLiteral("terminal_cursor_end"), seqJson(cursor));
        result.insert(QStringLiteral("truncated"), captured.truncated);
        result.insert(QStringLiteral("paused_by_operation"), false);
        result.insert(QStringLiteral("remote"), true);
        release();
        complete(result);
    }

    void fail(hwio::Error error) {
        stop();
        release();
        completeError(std::move(error));
    }

    void stop() {
        m_echo.stop();
        if (m_capture) {
            m_capture->cancel();
        }
    }

    void clearHint() {
        if (m_hint) {
            m_hint = false;
            provider().setAiCommandHint(id(), false);
        }
    }

    void release() {
        clearHint();
        if (m_slot) {
            m_slot = false;
            tools().remoteCommands().end(id());
        }
    }

    std::string m_command; // trimmed, as sent
    QString m_visible;     // the hidden view of the command (result, never the raw line)
    std::unique_ptr<hwio::device::CommandCapture> m_capture;
    QTimer m_echo;
    std::uint64_t m_start = 0;
    bool m_hint = false;
    bool m_slot = false; // RemoteCommands::begin() succeeded
};

// ---- operations (D8, D12, D13) --------------------------------------------------------------

struct RemoteOperationPlan {
    std::string_view tool;
    RemoteOperationRequest request;
    QJsonObject details; // the consent prompt's
    QString action;      // the start marker's
    QJsonObject result;  // the success result's fields
};

const RemoteTargetOffer* findOffer(const RemoteInfo& info, std::string_view target,
                                   RemoteOperationKind kind) {
    const auto it = std::ranges::find_if(info.operations, [&](const RemoteTargetOffer& offer) {
        return (kind == RemoteOperationKind::LteUpdate || offer.target == target) &&
               std::ranges::find(offer.kinds, kind) != offer.kinds.end();
    });
    return it == info.operations.end() ? nullptr : &*it;
}

hwio::Error targetUnsupported(std::string detail) {
    return makeError(ErrorCode::RemoteTargetUnsupported, std::move(detail));
}

hwio::Status planFlash(RemoteOperationPlan& plan, const QJsonObject& args, const RemoteInfo& info,
                       const std::string& target) {
    if (args.contains(QStringLiteral("path"))) {
        return std::unexpected(remoteInvalid(
            QStringLiteral("A file cannot be flashed to a customer's device: use catalog_id, "
                           "or prepared_id when the device lists prepared firmware."),
            "path"));
    }
    if (args.value(QStringLiteral("allow_uicr")).toBool()) {
        return std::unexpected(remoteInvalid(
            QStringLiteral("allow_uicr is not available on a customer's device."), "allow_uicr"));
    }
    const RemoteTargetOffer* offer = findOffer(info, target, RemoteOperationKind::Flash);
    if (offer == nullptr) {
        return std::unexpected(targetUnsupported("flash " + target));
    }
    plan.request.kind = RemoteOperationKind::Flash;
    plan.request.target = target;
    plan.details.insert(QStringLiteral("target"), qs(target));
    plan.result.insert(QStringLiteral("target"), qs(target));
    plan.result.insert(QStringLiteral("operation"), QStringLiteral("flash"));
    if (args.contains(QStringLiteral("catalog_id"))) {
        if (!offer->catalog) {
            return std::unexpected(targetUnsupported("no catalog firmware on " + target));
        }
        const QString catalog = args.value(QStringLiteral("catalog_id")).toString().toLower();
        plan.request.firmwareId = catalog.toStdString();
        plan.details.insert(QStringLiteral("catalog_id"), catalog);
        plan.result.insert(QStringLiteral("catalog_id"), catalog);
        plan.action = QStringLiteral("flash %1 (catalog %2)").arg(qs(target), catalog);
    } else {
        const std::string prepared =
            args.value(QStringLiteral("prepared_id")).toString().toStdString();
        const auto it = std::ranges::find(offer->prepared, prepared, &RemotePreparedFirmware::id);
        if (it == offer->prepared.end()) {
            std::string known;
            for (const RemotePreparedFirmware& firmware : offer->prepared) {
                known += known.empty() ? firmware.id : ", " + firmware.id;
            }
            return std::unexpected(
                remoteInvalid(QStringLiteral("The customer's device lists no prepared firmware %1.")
                                  .arg(qs(prepared)),
                              "prepared: " + known));
        }
        plan.request.prepared = prepared;
        plan.details.insert(QStringLiteral("prepared_id"), qs(prepared));
        plan.details.insert(QStringLiteral("firmware"), qs(it->name + " " + it->version));
        plan.result.insert(QStringLiteral("prepared_id"), qs(prepared));
        plan.action = QStringLiteral("flash %1 (prepared %2)").arg(qs(target), qs(prepared));
    }
    return {};
}

hwio::Status planLteUpdate(RemoteOperationPlan& plan, const QJsonObject& args,
                           const RemoteInfo& info, const std::string& /*target*/) {
    if (args.contains(QStringLiteral("path"))) {
        return std::unexpected(remoteInvalid(
            QStringLiteral("No file can be given for a customer's device: the customer's "
                           "HARDWARIO Monitor uses its recommended LTE firmware."),
            "path"));
    }
    if (findOffer(info, {}, RemoteOperationKind::LteUpdate) == nullptr) {
        return std::unexpected(targetUnsupported("lteUpdate"));
    }
    plan.request.kind = RemoteOperationKind::LteUpdate;
    plan.details.insert(QStringLiteral("target"), QStringLiteral("lte"));
    plan.details.insert(QStringLiteral("firmware"), QStringLiteral("recommended"));
    plan.result.insert(QStringLiteral("target"), QStringLiteral("lte"));
    plan.result.insert(QStringLiteral("operation"), QStringLiteral("lteUpdate"));
    plan.action = QStringLiteral("flash_lte (recommended firmware)");
    return {};
}

hwio::Status planReset(RemoteOperationPlan& plan, const QJsonObject& args, const RemoteInfo& info,
                       const std::string& target) {
    if (args.value(QStringLiteral("halt")).toBool()) {
        return std::unexpected(remoteInvalid(
            QStringLiteral("A customer's device cannot be held at the reset vector."), "halt"));
    }
    if (findOffer(info, target, RemoteOperationKind::Reset) == nullptr) {
        return std::unexpected(targetUnsupported("reset " + target));
    }
    plan.request.kind = RemoteOperationKind::Reset;
    plan.request.target = target;
    plan.details.insert(QStringLiteral("target"), qs(target));
    plan.result.insert(QStringLiteral("halted"), false);
    plan.action = QStringLiteral("reset %1").arg(qs(target));
    return {};
}

hwio::Result<RemoteOperationPlan> planOperation(std::string_view tool, const QJsonObject& args,
                                                const RemoteInfo& info) {
    const std::string target = args.contains(QStringLiteral("target"))
                                   ? args.value(QStringLiteral("target")).toString().toStdString()
                                   : std::string("app");
    RemoteOperationPlan plan;
    plan.tool = tool;
    auto* build = &planReset;
    if (tool == "flash") {
        build = &planFlash;
    } else if (tool == "flash_lte") {
        build = &planLteUpdate;
    }
    if (auto planned = build(plan, args, info, target); !planned) {
        return std::unexpected(std::move(planned).error());
    }
    plan.details.insert(QStringLiteral("remote"), true);
    return plan;
}

class RemoteOperationCall final : public DeviceCall {
public:
    RemoteOperationCall(ToolsContext& tools, const hwio::mcp::RequestContext& context,
                        hwio::mcp::ToolEnv& env, ResolvedDevice device, RemoteOperationPlan plan)
        : DeviceCall(tools, context, env, std::move(device)), m_plan(std::move(plan)) {
        QJsonObject details = m_plan.details;
        details.insert(QStringLiteral("device"), qs(id()));
        // ADR-0436: every operation on a customer's device asks (Flash category), every time.
        hwio::mcp::ConsentRequest request = hwio::mcp::makeConsentRequest(
            this->context(), hwio::mcp::ConsentCategory::Flash, id(), details);
        request.remoteDevice = true;
        requestConsent(consentBroker(), std::move(request),
                       [this](const hwio::mcp::ConsentDecision& decision) {
                           if (!decision) {
                               completeError(decision.error());
                               return;
                           }
                           if (*decision != hwio::mcp::ConsentGrant::Prompted) {
                               completeError(
                                   makeError(ErrorCode::McpConsentDenied, this->context().tool));
                               return;
                           }
                           begin();
                       });
    }
    ~RemoteOperationCall() override {
        detach();
        endBusy();
    }
    RemoteOperationCall(const RemoteOperationCall&) = delete;
    RemoteOperationCall& operator=(const RemoteOperationCall&) = delete;
    RemoteOperationCall(RemoteOperationCall&&) = delete;
    RemoteOperationCall& operator=(RemoteOperationCall&&) = delete;

protected:
    void onCancel() override {
        if (!m_operation) {
            ToolCall::onCancel();
            return;
        }
        // D12 (ADR-0438): stop waiting; the customer's Monitor finishes the operation.
        detach();
        hwio::Error error = makeError(ErrorCode::Cancelled, "stopped waiting");
        setUntranslatedText(error, ErrorText::Message,
                            "The operation continues on the customer's device; its result "
                            "appears in the remote tab.");
        failWith(std::move(error));
    }
    void deviceGone() override {
        detach();
        endBusy();
    }

private:
    void begin() {
        if (isFinished()) {
            return;
        }
        IRemoteDevice* remote = ref().remote.data();
        if (!alive() || remote == nullptr) {
            completeError(deviceNotOpen(id()));
            return;
        }
        const QPointer<RemoteOperationCall> self(this);
        if (hwio::Status busy = provider().beginBusy(id(), busyText(m_plan.tool),
                                                     [self] {
                                                         if (!self.isNull()) {
                                                             self->cancel();
                                                         }
                                                     });
            !busy) {
            completeError(std::move(busy).error());
            return;
        }
        m_busy = true;
        m_begun = std::chrono::steady_clock::now();
        provider().marker(id(), markerStart(context(), m_plan.action));
        m_marker = true;
        reportProgress(0, std::nullopt, QStringLiteral("waiting for the customer"));
        auto started = remote->requestOperation(
            m_plan.request, context(),
            RemoteOperationCallbacks{.progress =
                                         [self](const RemoteOperationProgress& progress) {
                                             if (!self.isNull()) {
                                                 self->onProgress(progress);
                                             }
                                         },
                                     .done =
                                         [self](hwio::Status status) {
                                             if (!self.isNull()) {
                                                 self->onDone(std::move(status));
                                             }
                                         }});
        if (isFinished()) { // done() before requestOperation() returned
            return;
        }
        if (!started) {
            failWith(std::move(started).error());
            return;
        }
        m_operation = *started;
    }

    void onProgress(const RemoteOperationProgress& progress) {
        if (isFinished()) {
            return;
        }
        const double steps = std::max(1, progress.steps);
        const double fraction = progress.total > 0
                                    ? static_cast<double>(std::min(progress.done, progress.total)) /
                                          static_cast<double>(progress.total)
                                    : 0.0;
        const double done = (static_cast<double>(std::max(1, progress.step)) - 1.0) + fraction;
        switch (progress.stage) {
        case RemoteOperationProgress::Stage::WaitingForCustomer:
            reportProgress(0, steps, QStringLiteral("waiting for the customer"));
            return;
        case RemoteOperationProgress::Stage::WaitingForSupport:
            // D13 (ADR-0438): the support person answers in the remote tab.
            reportProgress(done, steps, QStringLiteral("waiting for support's answer"));
            return;
        case RemoteOperationProgress::Stage::Running:
            reportProgress(done, steps,
                           progress.phase.empty() ? QStringLiteral("running") : qs(progress.phase));
            return;
        }
    }

    void onDone(hwio::Status status) {
        m_operation.reset();
        if (isFinished()) {
            return;
        }
        if (!status) {
            failWith(std::move(status).error());
            return;
        }
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_begun);
        if (alive()) {
            provider().marker(id(), markerDone(m_plan.tool, duration));
        }
        QJsonObject result = m_plan.result;
        if (m_plan.tool != "reset") {
            result.insert(QStringLiteral("status"), QStringLiteral("ok"));
            result.insert(QStringLiteral("duration"), seconds(duration));
        }
        if (alive()) {
            const std::uint64_t cursor = ref().console->log().lastSeq();
            result.insert(QStringLiteral("log_cursor_end"), seqJson(cursor));
            result.insert(QStringLiteral("terminal_cursor_end"), seqJson(cursor));
        }
        result.insert(QStringLiteral("remote"), true);
        endBusy();
        complete(result);
    }

    void failWith(hwio::Error error) {
        if (isFinished()) {
            return;
        }
        if (m_marker && alive()) {
            provider().marker(id(), error.code == ErrorCode::Cancelled
                                        ? markerCancelled(m_plan.tool)
                                        : markerFailed(m_plan.tool, error));
        }
        endBusy();
        completeError(std::move(error));
    }

    void detach() {
        if (m_operation) {
            const RemoteOperationId operation = *m_operation;
            m_operation.reset();
            if (!ref().remote.isNull()) {
                ref().remote->detachOperation(operation);
            }
        }
    }

    void endBusy() {
        if (m_busy) {
            m_busy = false;
            provider().endBusy(id());
        }
    }

    RemoteOperationPlan m_plan;
    std::optional<RemoteOperationId> m_operation;
    std::chrono::steady_clock::time_point m_begun;
    bool m_busy = false;
    bool m_marker = false;
};

} // namespace

// ---- public parts -----------------------------------------------------------------------------

RemotePolicy remotePolicy(std::string_view tool) noexcept {
    if (std::ranges::find(allowedTools, tool) != allowedTools.end()) {
        return RemotePolicy::Allowed;
    }
    if (std::ranges::find(humanTools, tool) != humanTools.end()) {
        return RemotePolicy::NotAllowed;
    }
    return RemotePolicy::CapabilityMissing; // every other tool, also ones added later
}

hwio::Error remoteNotAllowedYet(const std::string& id) {
    hwio::Error error = makeError(ErrorCode::McpConsentDenied, id);
    setUntranslatedText(error, ErrorText::Message,
                        "Support has not allowed this AI client to work with the customer's "
                        "device yet.");
    setUntranslatedText(error, ErrorText::Suggestion,
                        "Call a tool on the device (e.g. read_terminal); the support person is "
                        "then asked in HARDWARIO Monitor.");
    return error;
}

hwio::Status RemoteCommands::begin(const std::string& id, const hwio::mcp::ToolLimits& limits) {
    if (m_states[id].busy) {
        return std::unexpected(makeError(ErrorCode::InternalBusy, "send_command"));
    }
    if (hwio::Status taken = take(id, limits); !taken) {
        return taken;
    }
    m_states[id].busy = true;
    return {};
}

void RemoteCommands::end(const std::string& id) {
    if (const auto it = m_states.find(id); it != m_states.end()) {
        it->second.busy = false;
    }
}

hwio::Status RemoteCommands::take(const std::string& id, const hwio::mcp::ToolLimits& limits) {
    State& state = m_states[id];
    const auto now = std::chrono::steady_clock::now();
    while (!state.sent.empty() && now - state.sent.front() >= limits.remoteCommandWindow) {
        state.sent.pop_front();
    }
    if (state.sent.size() >= limits.remoteCommandsPerWindow) {
        hwio::Error error = makeError(
            ErrorCode::McpTooManyRequests,
            std::format("at most {} commands per {} s on a customer's device",
                        limits.remoteCommandsPerWindow,
                        std::chrono::duration_cast<std::chrono::seconds>(limits.remoteCommandWindow)
                            .count()));
        return std::unexpected(std::move(error));
    }
    state.sent.push_back(now);
    return {};
}

void RemoteCommands::keepOnly(const std::set<std::string>& alive) {
    std::erase_if(m_states, [&alive](const auto& entry) {
        return !alive.contains(entry.first) && !entry.second.busy;
    });
}

hwio::Status checkRemoteCommand(const std::string& id, IRemoteDevice& remote, std::string_view line,
                                const hwio::mcp::ToolLimits& limits) {
    if (line.size() > limits.remoteLineMaxBytes) {
        return std::unexpected(remoteInvalid(
            QStringLiteral("The command is longer than %1 bytes (the limit of a support session).")
                .arg(limits.remoteLineMaxBytes),
            "command"));
    }
    if (std::ranges::any_of(line, [](char c) {
            const auto byte = static_cast<unsigned char>(c);
            return byte < 0x20 || byte == 0x7f;
        })) {
        return std::unexpected(
            remoteInvalid(QStringLiteral("The command contains a control character."), "command"));
    }
    if (remote.redaction().setsSecret(line)) {
        // ADR-0437: an AI client does not write a customer's keys; never sent.
        hwio::Error error = makeError(ErrorCode::RemoteCommandRefused, "sets a secret");
        setUntranslatedText(error, ErrorText::Message,
                            "This command sets a secret value of the customer's device; an AI "
                            "client cannot send it.");
        setUntranslatedText(error, ErrorText::Suggestion,
                            "Ask the support person; the customer enters secrets locally.");
        return std::unexpected(std::move(error));
    }
    const RemoteInfo info = remote.info();
    if (info.join == RemoteJoin::Ended) {
        return std::unexpected(makeError(ErrorCode::RemoteHostEnded, id));
    }
    if (info.join != RemoteJoin::Allowed) {
        return std::unexpected(makeError(ErrorCode::RemoteWaitingForCustomer, id));
    }
    if (!info.commands) {
        hwio::Error error = capabilityMissingOnRemote(id);
        setUntranslatedText(error, ErrorText::Message,
                            "The customer shares no console in this session (operations only).");
        return std::unexpected(std::move(error));
    }
    return {};
}

QJsonObject visibleArguments(const DeviceRef& ref, const QJsonObject& arguments) {
    QJsonObject visible;
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        if (!it.value().isString()) {
            visible.insert(it.key(), it.value());
        } else if (it.key() == QStringLiteral("command")) {
            visible.insert(it.key(), visibleCommand(ref, it.value().toString().toStdString()));
        } else {
            visible.insert(it.key(), visibleText(ref, it.value().toString()));
        }
    }
    return visible;
}

hwio::mcp::ToolStart remoteGate(ToolsContext& context, std::string_view tool,
                                hwio::mcp::ToolStart start) {
    hwio::mcp::ToolStart remoteStart;
    if (tool == "send_command") {
        remoteStart = remoteSendCommandStart(context);
    } else if (tool == "flash" || tool == "flash_lte" || tool == "reset") {
        remoteStart = remoteOperationStart(context, tool);
    }
    return [&context, tool, start = std::move(start), remoteStart = std::move(remoteStart)](
               const hwio::mcp::RequestContext& rc, const QJsonObject& args,
               hwio::mcp::ToolEnv& env) -> std::unique_ptr<hwio::mcp::ToolCall> {
        const bool deviceArgument = args.contains(QStringLiteral("device"));
        if (tool == "list_devices" || (tool == "status" && !deviceArgument)) {
            return start(rc, args, env); // remote entries handled there (nameless before)
        }
        if (tool == "connect") {
            if (args.value(QStringLiteral("device")).toString().startsWith(u"remote:")) {
                return failed(toolNotAllowedOnRemote(tool));
            }
            return start(rc, args, env);
        }
        auto device = resolveDevice(context.provider(), args);
        if (!device || !device->ref.isRemote) {
            return start(rc, args, env);
        }
        switch (remotePolicy(tool)) {
        case RemotePolicy::NotAllowed:
            return failed(toolNotAllowedOnRemote(tool));
        case RemotePolicy::CapabilityMissing:
            return failed(capabilityMissingOnRemote(device->id));
        case RemotePolicy::Allowed:
            break;
        }
        return std::make_unique<RemoteGateCall>(rc, args, env, std::move(*device),
                                                remoteStart ? remoteStart : start);
    };
}

hwio::mcp::ToolStart remoteSendCommandStart(ToolsContext& context) {
    return [&context](const hwio::mcp::RequestContext& rc, const QJsonObject& args,
                      hwio::mcp::ToolEnv& env) -> std::unique_ptr<hwio::mcp::ToolCall> {
        auto device = resolveDevice(context.provider(), args);
        if (!device) {
            return failed(std::move(device).error());
        }
        const auto& limits = env.limits().tools;
        // ADR-0438: the echo is matched against the trimmed line; that is what is sent.
        const std::string command =
            trimmed(args.value(QStringLiteral("command")).toString()).toStdString();
        if (command.empty()) {
            return failed(remoteInvalid(QStringLiteral("The command is empty."), "command"));
        }
        return std::make_unique<RemoteSendCommandCall>(
            context, rc, env, std::move(*device), command,
            timeoutArgument(args, limits.remoteCommandDefaultTimeout, limits));
    };
}

hwio::mcp::ToolStart remoteOperationStart(ToolsContext& context, std::string_view tool) {
    return [&context, tool](const hwio::mcp::RequestContext& rc, const QJsonObject& args,
                            hwio::mcp::ToolEnv& env) -> std::unique_ptr<hwio::mcp::ToolCall> {
        auto device = resolveDevice(context.provider(), args);
        if (!device) {
            return failed(std::move(device).error());
        }
        if (device->ref.remote.isNull()) {
            return failed(deviceNotOpen(device->id));
        }
        const RemoteInfo info = device->ref.remote->info();
        if (info.join == RemoteJoin::Ended) {
            return failed(makeError(ErrorCode::RemoteHostEnded, device->id));
        }
        if (info.join != RemoteJoin::Allowed) {
            return failed(makeError(ErrorCode::RemoteWaitingForCustomer, device->id));
        }
        auto plan = planOperation(tool, args, info);
        if (!plan) {
            return failed(std::move(plan).error());
        }
        return std::make_unique<RemoteOperationCall>(context, rc, env, std::move(*device),
                                                     std::move(*plan));
    };
}

} // namespace hwio::mcp::tools::detail
