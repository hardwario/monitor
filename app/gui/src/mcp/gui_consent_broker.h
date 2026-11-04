#pragma once

// GuiConsentBroker - the IConsentBroker of the GUI's MCP server (plan/features/0014, ADR-0101,
// ADR-0108, ADR-0161, ADR-0241).
//
// A request is allowed at once when each of its categories is either set to "Always allow"
// (setting mcp/consent/<id> = allow) or granted to the request's client ("Allow … for <client>
// until it disconnects", keyed by RequestContext::clientKey, ADR-0241). Otherwise it is queued and
// shown as a prompt (ConsentWindow.qml through ConsentViewModel): one at a time, at most
// `maxQueued` waiting behind it (more -> mcp.consent_timeout at once). The countdown (`timeout`,
// 120 s) starts when the prompt is shown; no answer -> mcp.consent_timeout, Deny ->
// mcp.consent_denied (detail: the tool). The returned cancel functor withdraws the request (the
// client cancelled, the server stopped): `done` is then never called and `withdrawn` is emitted
// when it was the shown prompt.
//
// The client name and version of a request are sanitised when it arrives (review M4).
//
// Grants are forgotten by clientGone() (legacy session end, modern client idle 30 min), revoke(),
// and forgetAll() (token regenerated, server stopped / port changed, quit; McpController).
//
// `done` callbacks may start new requests synchronously; the queue is updated before any of them
// runs.
//
// Ownership: McpController (std::unique_ptr, destroyed after the server). GUI thread only
// (ADR-0097).

#include "hwio/mcp/consent.h"

#include <QDeadlineTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::gui {

class SettingsStore;

class GuiConsentBroker : public QObject, public hwio::mcp::IConsentBroker {
    Q_OBJECT

public:
    struct Options {
        std::chrono::milliseconds timeout{std::chrono::seconds(120)}; // ADR-0101
        std::size_t maxQueued = 8;                                    // behind the shown one
    };

    enum class Answer : std::uint8_t { Deny, AllowOnce, AllowForClient };

    struct Prompt {
        std::uint64_t id = 0;
        hwio::mcp::ConsentRequest request;
        std::function<void(hwio::mcp::ConsentDecision)> done;
    };

    struct Grant {
        std::string clientKey;
        QString clientName;    // sanitised (client_name.h)
        QString clientVersion; // sanitised, may be empty (ADR-0241)
        hwio::mcp::ConsentCategories categories;
    };

    // The categories in settings order, with their key ids ("flash", "memory-write", ...).
    static constexpr std::array categories{
        hwio::mcp::ConsentCategory::Flash, hwio::mcp::ConsentCategory::Erase,
        hwio::mcp::ConsentCategory::MemoryWrite, hwio::mcp::ConsentCategory::Uicr,
        hwio::mcp::ConsentCategory::Connect};

    // The mcp/consent/<id> keys (ask|allow, default ask).
    static void registerKeys(SettingsStore& settings);
    [[nodiscard]] static QString ruleKey(hwio::mcp::ConsentCategory category);

    GuiConsentBroker(SettingsStore& settings, Options options, QObject* parent = nullptr);
    ~GuiConsentBroker() override;
    GuiConsentBroker(const GuiConsentBroker&) = delete;
    GuiConsentBroker& operator=(const GuiConsentBroker&) = delete;
    GuiConsentBroker(GuiConsentBroker&&) = delete;
    GuiConsentBroker& operator=(GuiConsentBroker&&) = delete;

    // hwio::mcp::IConsentBroker
    [[nodiscard]] std::function<void()>
    request(hwio::mcp::ConsentRequest request,
            std::function<void(hwio::mcp::ConsentDecision)> done) override;
    void clientGone(std::string_view clientKey) override;

    // Rules (Settings): true = Always allow.
    [[nodiscard]] bool alwaysAllowed(hwio::mcp::ConsentCategory category) const;
    [[nodiscard]] hwio::Status setAlwaysAllowed(hwio::mcp::ConsentCategory category, bool allow);

    [[nodiscard]] const std::vector<Grant>& grants() const noexcept { return m_grants; }
    void revoke(std::size_t index);
    void forgetAll();

    // The shown prompt (nullptr when none) and the number waiting behind it.
    [[nodiscard]] const Prompt* current() const;
    [[nodiscard]] std::size_t queued() const;
    // Time left of the shown prompt (0 when none).
    [[nodiscard]] std::chrono::milliseconds remaining() const;
    // Answers the prompt `promptId` (review H1): ignored unless it is the shown prompt, so a second
    // click or a repeated key never answers the next prompt.
    void answer(std::uint64_t promptId, Answer answer);
    // Denies every waiting request with mcp.consent_denied (quit).
    void denyAll();

signals:
    void promptChanged(); // another prompt is shown, or none
    void grantsChanged();
    void rulesChanged();
    // The shown prompt was withdrawn by the client or the server.
    void withdrawn();
    // A client asked (for the active-client count).
    void clientSeen(const QString& clientKey);

private:
    [[nodiscard]] bool covered(const hwio::mcp::ConsentRequest& request) const;
    void withdraw(std::uint64_t id);
    void showNext();
    // Removes the shown prompt and calls its `done` with `decision`, then shows the next one.
    void finishCurrent(hwio::mcp::ConsentDecision decision);

    SettingsStore* m_settings; // non-owning, outlives the broker
    Options m_options;
    std::deque<Prompt> m_queue; // front = shown
    std::vector<Grant> m_grants;
    std::uint64_t m_nextId = 1;
    QTimer m_timer; // the shown prompt's timeout
    QDeadlineTimer m_deadline;
};

} // namespace hwio::gui
