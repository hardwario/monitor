#include "mcp/gui_consent_broker.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/mcp/consent.h"
#include "mcp/client_name.h"
#include "settings_store.h"

#include <QDeadlineTimer>
#include <QLoggingCategory>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimer>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

const QString askValue = QStringLiteral("ask");
const QString allowValue = QStringLiteral("allow");

} // namespace

QString GuiConsentBroker::ruleKey(hwio::mcp::ConsentCategory category) {
    const std::string_view id = hwio::mcp::consentCategoryId(category);
    return QStringLiteral("mcp/consent/") +
           QLatin1StringView(id.data(), static_cast<qsizetype>(id.size()));
}

void GuiConsentBroker::registerKeys(SettingsStore& settings) {
    std::vector<SettingKey> keys;
    keys.reserve(categories.size());
    for (const hwio::mcp::ConsentCategory category : categories) {
        keys.push_back(SettingKey{.path = ruleKey(category),
                                  .type = QMetaType::QString,
                                  .defaultValue = askValue,
                                  .validator = validators::oneOf({askValue, allowValue})});
    }
    settings.registerKeys(keys);
}

GuiConsentBroker::GuiConsentBroker(SettingsStore& settings, Options options, QObject* parent)
    : QObject(parent), m_settings(&settings), m_options(options) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (const Prompt* prompt = current(); prompt != nullptr) {
            qCInfo(lcGui).noquote()
                << "MCP consent: no answer for" << toQString(prompt->request.tool);
            finishCurrent(
                std::unexpected(makeError(ErrorCode::McpConsentTimeout, prompt->request.tool)));
        }
    });
    connect(m_settings, &SettingsStore::valueChanged, this, [this](const QString& key) {
        if (key.startsWith(QStringLiteral("mcp/consent/"))) {
            rulesChanged(); // signal
        }
    });
}

GuiConsentBroker::~GuiConsentBroker() {
    // The server is stopped before the broker goes away (McpController); nothing may be left.
    // Pending `done` functors are dropped without a call (their calls were cancelled).
    m_queue.clear();
}

bool GuiConsentBroker::alwaysAllowed(hwio::mcp::ConsentCategory category) const {
    return m_settings->value(ruleKey(category)).toString() == allowValue;
}

Status GuiConsentBroker::setAlwaysAllowed(hwio::mcp::ConsentCategory category, bool allow) {
    return m_settings->setValue(ruleKey(category), allow ? allowValue : askValue);
}

bool GuiConsentBroker::covered(const hwio::mcp::ConsentRequest& request) const {
    hwio::mcp::ConsentCategories granted;
    for (const Grant& grant : m_grants) {
        if (grant.clientKey == request.clientKey) {
            granted = grant.categories;
        }
    }
    return std::ranges::all_of(categories, [&](hwio::mcp::ConsentCategory category) {
        return !request.categories.testFlag(category) || granted.testFlag(category) ||
               alwaysAllowed(category);
    });
}

std::function<void()>
GuiConsentBroker::request(hwio::mcp::ConsentRequest request,
                          std::function<void(hwio::mcp::ConsentDecision)> done) {
    request.client.name = clientDisplayUtf8(request.client.name);       // review M4
    request.client.version = clientDisplayUtf8(request.client.version); // review M4
    clientSeen(toQString(request.clientKey));                           // signal
    if (covered(request)) {
        done({});
        return {};
    }
    if (m_queue.size() > m_options.maxQueued) { // the shown one + maxQueued waiting
        qCInfo(lcGui).noquote() << "MCP consent: queue full, refused" << toQString(request.tool);
        done(std::unexpected(makeError(ErrorCode::McpConsentTimeout, request.tool)));
        return {};
    }
    const std::uint64_t id = m_nextId++;
    m_queue.push_back(Prompt{.id = id, .request = std::move(request), .done = std::move(done)});
    if (m_queue.size() == 1) {
        showNext();
    } else {
        promptChanged(); // signal: the queued count changed
    }
    return [self = QPointer<GuiConsentBroker>(this), id] {
        if (!self.isNull()) {
            self->withdraw(id);
        }
    };
}

void GuiConsentBroker::withdraw(std::uint64_t id) {
    const auto it = std::ranges::find(m_queue, id, &Prompt::id);
    if (it == m_queue.end()) {
        return;
    }
    const bool shown = it == m_queue.begin();
    m_queue.erase(it);
    if (shown) {
        m_timer.stop();
        withdrawn(); // signal
        showNext();
    } else {
        promptChanged(); // signal
    }
}

void GuiConsentBroker::showNext() {
    m_timer.stop();
    if (!m_queue.empty()) {
        m_deadline = QDeadlineTimer(m_options.timeout);
        m_timer.start(m_options.timeout);
    }
    promptChanged(); // signal
}

void GuiConsentBroker::finishCurrent(hwio::mcp::ConsentDecision decision) {
    if (m_queue.empty()) {
        return;
    }
    Prompt prompt = std::move(m_queue.front());
    m_queue.pop_front();
    showNext();
    prompt.done(std::move(decision)); // may queue another request
}

void GuiConsentBroker::answer(std::uint64_t promptId, Answer answer) {
    const Prompt* prompt = current();
    if (prompt == nullptr || prompt->id != promptId) {
        qCInfo(lcGui) << "MCP consent: an answer for prompt" << promptId
                      << "that is not shown is ignored";
        return;
    }
    switch (answer) {
    case Answer::Deny:
        finishCurrent(
            std::unexpected(makeError(ErrorCode::McpConsentDenied, prompt->request.tool)));
        return;
    case Answer::AllowOnce:
        finishCurrent({});
        return;
    case Answer::AllowForClient: {
        const hwio::mcp::ConsentRequest& request = prompt->request;
        const auto it = std::ranges::find(m_grants, request.clientKey, &Grant::clientKey);
        if (it == m_grants.end()) {
            m_grants.push_back(Grant{.clientKey = request.clientKey,
                                     .clientName = toQString(request.client.name),
                                     .clientVersion = toQString(request.client.version),
                                     .categories = request.categories});
        } else {
            it->categories |= request.categories;
        }
        grantsChanged(); // signal
        // The answered prompt and every waiting request that the grant now covers are allowed.
        std::vector<Prompt> allowed;
        for (auto waiting = m_queue.begin(); waiting != m_queue.end();) {
            if (covered(waiting->request)) {
                allowed.push_back(std::move(*waiting));
                waiting = m_queue.erase(waiting);
            } else {
                ++waiting;
            }
        }
        showNext();
        for (Prompt& next : allowed) {
            next.done({}); // may queue another request
        }
        return;
    }
    }
}

void GuiConsentBroker::clientGone(std::string_view clientKey) {
    const auto removed = std::erase_if(
        m_grants, [clientKey](const Grant& grant) { return grant.clientKey == clientKey; });
    if (removed > 0) {
        grantsChanged(); // signal
    }
}

void GuiConsentBroker::revoke(std::size_t index) {
    if (index < m_grants.size()) {
        m_grants.erase(m_grants.begin() + static_cast<std::ptrdiff_t>(index));
        grantsChanged(); // signal
    }
}

void GuiConsentBroker::forgetAll() {
    if (!m_grants.empty()) {
        m_grants.clear();
        grantsChanged(); // signal
    }
}

void GuiConsentBroker::denyAll() {
    m_timer.stop();
    std::deque<Prompt> prompts = std::exchange(m_queue, {});
    promptChanged(); // signal
    for (Prompt& prompt : prompts) {
        prompt.done(std::unexpected(makeError(ErrorCode::McpConsentDenied, prompt.request.tool)));
    }
}

const GuiConsentBroker::Prompt* GuiConsentBroker::current() const {
    return m_queue.empty() ? nullptr : &m_queue.front();
}

std::size_t GuiConsentBroker::queued() const { return m_queue.empty() ? 0 : m_queue.size() - 1; }

std::chrono::milliseconds GuiConsentBroker::remaining() const {
    return m_queue.empty() ? std::chrono::milliseconds(0)
                           : std::chrono::milliseconds(m_deadline.remainingTime());
}

} // namespace hwio::gui
