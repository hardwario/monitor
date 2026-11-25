#pragma once

// RemoteJoinViewModel - the joiner side of a remote support session in a remote device tab
// (plan/features/0044 § Joiner, ADR-0325, ADR-0336): the banner state (waiting for the customer,
// joined, ended, idle) and the remote operations of the Flash page (catalog firmware ID only,
// erase, recover, reset; each confirmed by the customer, results as reported by the host). Also the
// validation of the Join dialog's input (static helpers used by AppViewModel). ADR-0369: a result
// names the operation and the local time and is also a status line in the tab's console; a success
// clears when support leaves the Flash page or starts another operation, an error stays until
// dismissResult() or another operation.
//
// Plan 0062 (ADR-0433..0438): the tab's GuiRemoteDevice (remoteDevice(), one per session, created
// and destroyed with the session; the grants of AI clients are revoked before it is destroyed), the
// AI client's operations through the same RemoteOperationClient (requestAiOperation(): one source
// of operationPending()), the AI busy state of an AI operation ("AI: Flashing…" in
// operationStatus, Stop waiting, D12) and the banner's "AI client active" with Stop AI.
//
// Ownership: child of its DeviceTabViewModel (Qt parent). GUI thread only.

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/remote/tunnel.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace hwio::remote {
class RemoteOperationClient;
} // namespace hwio::remote

namespace hwio::gui {

class DeviceTabViewModel;
class GuiRemoteDevice;

class RemoteJoinViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DeviceTabViewModel")

    Q_PROPERTY(bool active READ active NOTIFY changed) // a remote tab
    // "connecting" | "waiting" | "joined" | "ended" | "idle" ("ended" also after support left the
    // session with the device bar's Leave session, ADR-0429; then bannerText says so)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString bannerText READ bannerText NOTIFY changed)
    // The host is HARDWARIO Monitor with operations (the Flash page offers them).
    Q_PROPERTY(bool operationsAvailable READ operationsAvailable NOTIFY changed)
    Q_PROPERTY(QString operationsUnavailableText READ operationsUnavailableText NOTIFY changed)
    // Plan 0059 (ADR-0414, D-5): the targets support can choose, [{id, name, catalog}]: those of
    // the host's hello, named in support's language from the profile (CHESTER: "Application core",
    // "LTE modem core"; another known profile: its target's display name; else the id); an older
    // host (no `targets`): app (catalog) and lte, as in plan 0044.
    Q_PROPERTY(QVariantList targets READ targets NOTIFY changed)
    // ADR-0363: the host offers the guided LTE update (its hello's opKinds).
    Q_PROPERTY(bool lteUpdateAvailable READ lteUpdateAvailable NOTIFY changed)
    Q_PROPERTY(bool operationPending READ operationPending NOTIFY operationChanged)
    Q_PROPERTY(QString operationStatus READ operationStatus NOTIFY operationChanged)
    Q_PROPERTY(bool operationFailed READ operationFailed NOTIFY operationChanged)
    // 0..1, -1 = indeterminate.
    Q_PROPERTY(double operationProgress READ operationProgress NOTIFY operationChanged)
    // ADR-0368: the customer's Monitor asks support (the LTE update's Continue, Continue without a
    // copy); "" when no question is open. answerQuestion() answers it.
    Q_PROPERTY(QString operationQuestion READ operationQuestion NOTIFY operationChanged)
    // ADR-0369: an error result that support dismisses with × (dismissResult()).
    Q_PROPERTY(bool resultDismissable READ resultDismissable NOTIFY operationChanged)
    // Plan 0062 (D12): an AI client waits for the running operation (the Flash card's Stop
    // waiting, stopWaiting()).
    Q_PROPERTY(bool aiWaiting READ aiWaiting NOTIFY operationChanged)
    // Plan 0062 (ADR-0436): "AI client active: Claude Code" while support allowed an AI client for
    // this session ("" otherwise); the banner's Stop AI is stopAi().
    Q_PROPERTY(QString aiClientText READ aiClientText NOTIFY aiChanged)

public:
    explicit RemoteJoinViewModel(DeviceTabViewModel* tab);
    ~RemoteJoinViewModel() override;
    RemoteJoinViewModel(const RemoteJoinViewModel&) = delete;
    RemoteJoinViewModel& operator=(const RemoteJoinViewModel&) = delete;
    RemoteJoinViewModel(RemoteJoinViewModel&&) = delete;
    RemoteJoinViewModel& operator=(RemoteJoinViewModel&&) = delete;

    [[nodiscard]] bool active() const;
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString bannerText() const;
    [[nodiscard]] bool operationsAvailable() const;
    [[nodiscard]] QString operationsUnavailableText() const;
    [[nodiscard]] bool lteUpdateAvailable() const;
    [[nodiscard]] QVariantList targets() const;
    [[nodiscard]] bool operationPending() const;
    // While an AI client waits for the operation: its busy text first ("AI: Flashing… Waiting for
    // the customer's confirmation…").
    [[nodiscard]] QString operationStatus() const;
    [[nodiscard]] bool operationFailed() const { return m_failed; }
    [[nodiscard]] double operationProgress() const { return m_progress; }
    [[nodiscard]] QString operationQuestion() const;
    Q_INVOKABLE void answerQuestion(bool yes);
    [[nodiscard]] bool resultDismissable() const;
    // ADR-0369: clears an error result (×).
    Q_INVOKABLE void dismissResult();

    // `kind`: "flash" (with `firmwareId`), "erase", "recover", "reset", "lteUpdate" (ADR-0363,
    // `target` ignored); `target` "app" | "lte".
    Q_INVOKABLE void requestOperation(const QString& kind, const QString& target,
                                      const QString& firmwareId = QString());
    // A valid catalog firmware ID (32 hex digits) for the Flash page's field.
    Q_INVOKABLE [[nodiscard]] static bool isFirmwareId(const QString& text);

    // ADR-0429: support left the session (DeviceTabViewModel::disconnectDevice of a remote tab):
    // state() "ended" with its own banner text until the next session (Join again) or until the
    // tab is configured for another session (setLeft(false)).
    void setLeft(bool left);
    [[nodiscard]] bool left() const noexcept { return m_left; }

    // The Join dialog: {valid, code ("482 913"), foreignRelay ("" or the link's server), error}.
    [[nodiscard]] static QVariantMap checkJoinInput(const QString& text, const QUrl& configured);

    // ---- plan 0062: AI clients (MCP) ----
    // The tab's remote device while it has a session (nullptr otherwise).
    [[nodiscard]] GuiRemoteDevice* remoteDevice() const noexcept { return m_device.get(); }
    // An AI client's request through the tab's operation client (the same op-request a person's
    // is; `questions` for the LTE update, answered by the person here, D13). `callbacks.done` is
    // called once with the result unless detachAiOperation() came first. Errors:
    // RemoteOperationClient::request's (internal.busy while any operation runs).
    [[nodiscard]] hwio::Result<hwio::mcp::tools::RemoteOperationId>
    requestAiOperation(const hwio::mcp::tools::RemoteOperationRequest& request,
                       hwio::mcp::tools::RemoteOperationCallbacks callbacks);
    // D12: no further callback; the tab keeps following the operation to its result.
    void detachAiOperation(hwio::mcp::tools::RemoteOperationId id);
    // The provider's beginBusy / endBusy of an AI operation (ADR-0105): the busy text and the
    // cancel callback (stopWaiting()). Errors: internal.busy while an operation runs or another AI
    // operation is busy.
    [[nodiscard]] hwio::Status beginAiBusy(QString text, std::function<void()> cancel);
    void endAiBusy();
    [[nodiscard]] bool aiWaiting() const { return static_cast<bool>(m_aiCancel); }
    // Stop waiting (D12): the AI call ends `cancelled`; the operation continues.
    Q_INVOKABLE void stopWaiting();
    [[nodiscard]] QString aiClientText() const;
    // Stop AI (ADR-0436): revokes every AI client's grant for this session.
    Q_INVOKABLE void stopAi();

signals:
    void changed();
    void operationChanged();
    void aiChanged();

private:
    void attach();
    void onFinished(bool ok, const Error& error);
    void clearResult();
    // Plan 0062: the AI operation's progress callback, if one is attached.
    void aiProgress(const hwio::mcp::tools::RemoteOperationProgress& progress);
    void releaseDevice();

    // Plan 0062: the AI client's pending operation (one at a time, as the client).
    struct AiOperation {
        hwio::mcp::tools::RemoteOperationId id = 0;
        hwio::mcp::tools::RemoteOperationCallbacks callbacks;
    };

    DeviceTabViewModel* m_tab; // the parent
    // Plan 0062: the AI clients' device of the session; releaseDevice() revokes its grants and
    // deletes it before m_client goes (attach(), destructor).
    std::unique_ptr<GuiRemoteDevice> m_device;
    std::unique_ptr<remote::RemoteOperationClient> m_client;
    std::optional<AiOperation> m_aiOperation;
    hwio::mcp::tools::RemoteOperationId m_nextAiOperation = 1;
    QString m_aiText;                 // the AI busy text ("" = none)
    std::function<void()> m_aiCancel; // set while an AI client waits (Stop waiting)
    std::optional<remote::tunnel::OpProgress> m_lastProgress; // for the AI's question stage
    bool m_idle = false;
    bool m_left = false; // ADR-0429: support left; cleared when a session is attached again
    QString m_status;
    bool m_failed = false;
    double m_progress = -1.0;
    // ADR-0369: the operation of the pending request (its result names it).
    remote::tunnel::OpKind m_kind = remote::tunnel::OpKind::Reset;
    remote::tunnel::OpTarget m_target = remote::tunnel::OpTarget::App;
};

} // namespace hwio::gui
