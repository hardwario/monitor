// Plan 0014 A1/A2: the activity list (ring of 1000, roles, copy, clear), the client snippets (port,
// token, masked, CLI path, valid JSON), the consent broker (rules, grants per client and category,
// revoke, clientGone, forgetAll, queue overflow, timeout, withdrawal of the shown and of a queued
// prompt, several categories in one prompt, denyAll) and the prompt's view model texts.

#include "gui_test_support.h"
#include "hwio/core/error.h"
#include "hwio/mcp/activity.h"
#include "hwio/mcp/consent.h"
#include "mcp/activity_model.h"
#include "mcp/client_name.h"
#include "mcp/client_snippets.h"
#include "mcp/consent_view_model.h"
#include "mcp/gui_consent_broker.h"
#include "settings_store.h"

#include <QChar>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hwio::gui::ActivityModel;
using hwio::gui::ConsentViewModel;
using hwio::gui::GuiConsentBroker;
using hwio::gui::SettingsStore;
using hwio::gui::test::spin;
using hwio::gui::test::waitFor;
using hwio::mcp::ConsentCategories;
using hwio::mcp::ConsentCategory;
using hwio::mcp::ConsentDecision;
using hwio::mcp::ConsentRequest;
using namespace std::chrono_literals;
namespace snippets = hwio::gui::mcp_snippets;

hwio::mcp::ActivityEntry entry(int number, std::string result = "ok") {
    hwio::mcp::ActivityEntry e;
    e.time = QDateTime::currentDateTimeUtc();
    e.client = "Claude Code";
    e.clientKey = "Claude Code";
    e.tool = "send_command";
    e.deviceId = "jlink:801000001";
    e.arguments = R"({"command":"n)" + std::to_string(number) + R"("})";
    e.result = std::move(result);
    e.duration = std::chrono::milliseconds(1234);
    return e;
}

TEST(McpActivity, RingOfThousandNewestFirst) {
    ActivityModel model;
    QSignalSpy recorded(&model, &ActivityModel::entryRecorded);
    for (int i = 0; i < 1005; ++i) {
        model.record(entry(i));
    }
    EXPECT_EQ(model.rowCount(), 1000);
    EXPECT_EQ(recorded.count(), 1005);
    EXPECT_EQ(recorded.front().front().toString(), QStringLiteral("Claude Code"));
    EXPECT_EQ(model.entry(0).arguments, "{\"command\":\"n1004\"}");
    EXPECT_EQ(model.entry(999).arguments, "{\"command\":\"n5\"}");
}

TEST(McpActivity, RolesTextAndClear) {
    ActivityModel model;
    model.record(entry(1));
    model.record(entry(2, "mcp.consent_denied"));
    const QModelIndex newest = model.index(0);
    EXPECT_EQ(model.data(newest, ActivityModel::ToolRole).toString(),
              QStringLiteral("send_command"));
    EXPECT_EQ(model.data(newest, ActivityModel::DeviceRole).toString(),
              QStringLiteral("jlink:801000001"));
    EXPECT_EQ(model.data(newest, ActivityModel::ResultRole).toString(),
              QStringLiteral("mcp.consent_denied"));
    EXPECT_TRUE(model.data(newest, ActivityModel::IsErrorRole).toBool());
    EXPECT_FALSE(model.data(model.index(1), ActivityModel::IsErrorRole).toBool());
    EXPECT_EQ(model.data(newest, ActivityModel::DurationRole).toString(), QStringLiteral("1.2 s"));
    EXPECT_EQ(model.data(newest, ActivityModel::TimeRole).toString().size(), 8); // HH:mm:ss
    EXPECT_TRUE(model.roleNames().values().contains("arguments"));

    const QStringList lines = model.text().split(QLatin1Char('\n'));
    ASSERT_EQ(lines.size(), 2);
    EXPECT_TRUE(lines.at(0).contains(QStringLiteral("mcp.consent_denied")));
    EXPECT_TRUE(lines.at(1).contains(QStringLiteral("{\"command\":\"n1\"}")));

    QSignalSpy count(&model, &ActivityModel::countChanged);
    model.clear();
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_EQ(count.count(), 1);
    EXPECT_TRUE(model.text().isEmpty());
}

TEST(McpSnippets, PortTokenMaskedAndValidJson) {
    const QString token(43, QLatin1Char('A'));
    const snippets::CliInvocation cli{.program = QStringLiteral("/usr/bin/hwio"), .arguments = {}};
    const auto made = snippets::make(47811, token, cli);
    ASSERT_EQ(made.size(), 3U);

    EXPECT_EQ(made[0].kind, snippets::Kind::ClaudeCodeHttp);
    EXPECT_TRUE(made[0].text.contains(QStringLiteral("http://127.0.0.1:47811/mcp")));
    EXPECT_TRUE(made[0].text.contains(QStringLiteral("Bearer ") + token));
    EXPECT_FALSE(made[0].shown.contains(token));
    EXPECT_TRUE(made[0].shown.contains(snippets::masked(token)));
    EXPECT_EQ(snippets::masked(token).size(), 43);

    QJsonParseError error{};
    const QJsonObject mcpJson = QJsonDocument::fromJson(made[1].text.toUtf8(), &error).object();
    ASSERT_EQ(error.error, QJsonParseError::NoError);
    const QJsonObject server = mcpJson.value(QStringLiteral("mcpServers"))
                                   .toObject()
                                   .value(QStringLiteral("hwio"))
                                   .toObject();
    EXPECT_EQ(server.value(QStringLiteral("url")).toString(),
              QStringLiteral("http://127.0.0.1:47811/mcp"));
    EXPECT_EQ(server.value(QStringLiteral("headers"))
                  .toObject()
                  .value(QStringLiteral("Authorization"))
                  .toString(),
              QStringLiteral("Bearer ${HWIO_MCP_TOKEN}"));
    EXPECT_FALSE(made[1].text.contains(token));

    const QJsonObject desktop = QJsonDocument::fromJson(made[2].text.toUtf8(), &error).object();
    ASSERT_EQ(error.error, QJsonParseError::NoError);
    const QJsonObject entry = desktop.value(QStringLiteral("mcpServers"))
                                  .toObject()
                                  .value(QStringLiteral("hwio"))
                                  .toObject();
    EXPECT_EQ(entry.value(QStringLiteral("command")).toString(), QStringLiteral("/usr/bin/hwio"));
    EXPECT_EQ(entry.value(QStringLiteral("args")).toArray(), QJsonArray({QStringLiteral("mcp")}));

    // AppImage: the AppImage runs the CLI with the "hwio" argument.
    const auto appImage =
        snippets::make(47800, {},
                       snippets::CliInvocation{.program = QStringLiteral("/opt/Monitor.AppImage"),
                                               .arguments = {QStringLiteral("hwio")}});
    EXPECT_TRUE(appImage[0].text.contains(QStringLiteral("<token>")));
    EXPECT_TRUE(appImage[2].text.contains(QStringLiteral("\"hwio\"")));
    EXPECT_FALSE(snippets::installedCli().program.isEmpty());
}

// ---- consent ------------------------------------------------------------------------------------

class McpConsent : public ::testing::Test {
protected:
    McpConsent() : m_settings(std::nullopt) { GuiConsentBroker::registerKeys(m_settings); }

    GuiConsentBroker& broker(std::chrono::milliseconds timeout = 120s) {
        if (!m_broker) {
            m_broker.emplace(m_settings,
                             GuiConsentBroker::Options{.timeout = timeout, .maxQueued = 8});
        }
        return *m_broker;
    }

    struct Outcome {
        std::optional<ConsentDecision> decision;
        std::function<void()> cancel;
    };

    // A request of `client` for `categories`; the decision lands in the returned outcome.
    std::shared_ptr<Outcome> ask(const std::string& client, ConsentCategories categories,
                                 const std::string& tool = "flash",
                                 const std::string& device = "jlink:801000001") {
        auto outcome = std::make_shared<Outcome>();
        ConsentRequest request;
        request.client = {.name = client, .version = "1"};
        request.clientKey = client;
        request.tool = tool;
        request.deviceId = device;
        request.categories = categories;
        outcome->cancel = broker().request(std::move(request), [outcome](ConsentDecision decision) {
            outcome->decision = std::move(decision);
        });
        return outcome;
    }

    // Answers the shown prompt.
    void answer(GuiConsentBroker::Answer value) {
        const auto* prompt = broker().current();
        ASSERT_NE(prompt, nullptr);
        broker().answer(prompt->id, value);
    }

    static bool allowed(const Outcome& outcome) {
        return outcome.decision.has_value() && outcome.decision->has_value();
    }
    static std::string detail(const Outcome& outcome) {
        return outcome.decision.has_value() && !outcome.decision->has_value()
                   ? outcome.decision->error().detail
                   : std::string();
    }
    static std::string code(const Outcome& outcome) {
        return outcome.decision && !*outcome.decision
                   ? std::string(hwio::idOf(outcome.decision->error().code))
                   : std::string();
    }

    SettingsStore& settings() { return m_settings; }

private:
    SettingsStore m_settings;
    std::optional<GuiConsentBroker> m_broker;
};

TEST_F(McpConsent, AlwaysAllowRuleSkipsThePrompt) {
    EXPECT_FALSE(broker().alwaysAllowed(ConsentCategory::Flash));
    ASSERT_TRUE(broker().setAlwaysAllowed(ConsentCategory::Flash, true));
    EXPECT_EQ(settings().value(QStringLiteral("mcp/consent/flash")).toString(),
              QStringLiteral("allow"));
    const auto flash = ask("Claude Code", ConsentCategory::Flash);
    ASSERT_TRUE(flash->decision.has_value());
    EXPECT_TRUE(allowed(*flash));
    EXPECT_EQ(broker().current(), nullptr);
    // Flash + UICR: UICR still asks.
    const auto uicr = ask("Claude Code", ConsentCategory::Flash | ConsentCategory::Uicr);
    EXPECT_FALSE(uicr->decision.has_value());
    EXPECT_NE(broker().current(), nullptr);
}

TEST_F(McpConsent, AllowOnceAndDeny) {
    const auto first = ask("Claude Code", ConsentCategory::Erase, "erase");
    const auto second = ask("Claude Code", ConsentCategory::Erase, "erase");
    EXPECT_EQ(broker().queued(), 1U);
    answer(GuiConsentBroker::Answer::AllowOnce);
    ASSERT_TRUE(first->decision.has_value());
    EXPECT_TRUE(allowed(*first));
    EXPECT_FALSE(second->decision.has_value()); // asked again: allowed once only
    answer(GuiConsentBroker::Answer::Deny);
    EXPECT_EQ(code(*second), "mcp.consent_denied");
    EXPECT_EQ(detail(*second), "erase");
    EXPECT_EQ(broker().current(), nullptr);
}

TEST_F(McpConsent, GrantsPerClientAndCategory) {
    QSignalSpy grantsChanged(&broker(), &GuiConsentBroker::grantsChanged);
    const auto a1 = ask("Claude Code", ConsentCategory::Flash);
    const auto b1 = ask("Other", ConsentCategory::Flash);
    const auto a2 = ask("Claude Code", ConsentCategory::Flash);
    answer(GuiConsentBroker::Answer::AllowForClient);
    EXPECT_TRUE(a1->decision && a1->decision.value().has_value());
    EXPECT_TRUE(a2->decision && a2->decision.value().has_value()); // queued, now covered
    EXPECT_FALSE(b1->decision.has_value());                        // another client: still asked
    ASSERT_EQ(broker().grants().size(), 1U);
    EXPECT_EQ(broker().grants().front().clientName, QStringLiteral("Claude Code"));
    EXPECT_EQ(grantsChanged.count(), 1);
    answer(GuiConsentBroker::Answer::Deny);
    EXPECT_EQ(code(*b1), "mcp.consent_denied");

    // Another category of the same client asks.
    const auto erase = ask("Claude Code", ConsentCategory::Erase, "erase");
    EXPECT_FALSE(erase->decision.has_value());
    answer(GuiConsentBroker::Answer::Deny);
    // The grant holds without a prompt...
    EXPECT_TRUE(ask("Claude Code", ConsentCategory::Flash)->decision.has_value());
    // ...until the client is gone.
    broker().clientGone("Claude Code");
    EXPECT_TRUE(broker().grants().empty());
    EXPECT_FALSE(ask("Claude Code", ConsentCategory::Flash)->decision.has_value());
    answer(GuiConsentBroker::Answer::AllowForClient);
    ASSERT_EQ(broker().grants().size(), 1U);
    broker().revoke(0);
    EXPECT_TRUE(broker().grants().empty());
    (void)ask("Claude Code", ConsentCategory::Flash);
    answer(GuiConsentBroker::Answer::AllowForClient);
    broker().forgetAll(); // token regenerated, server stopped
    EXPECT_TRUE(broker().grants().empty());
}

TEST_F(McpConsent, QueueOverflowTimesOutAtOnce) {
    std::vector<std::shared_ptr<Outcome>> outcomes;
    outcomes.reserve(9);
    for (int i = 0; i < 9; ++i) { // one shown + eight queued
        outcomes.push_back(ask("Claude Code", ConsentCategory::Flash));
    }
    EXPECT_EQ(broker().queued(), 8U);
    for (const auto& outcome : outcomes) {
        EXPECT_FALSE(outcome->decision.has_value());
    }
    const auto tenth = ask("Claude Code", ConsentCategory::Flash);
    EXPECT_EQ(code(*tenth), "mcp.consent_timeout");
    broker().denyAll();
    for (const auto& outcome : outcomes) {
        EXPECT_EQ(code(*outcome), "mcp.consent_denied");
    }
    EXPECT_EQ(broker().current(), nullptr);
}

TEST_F(McpConsent, TimeoutStartsWhenShown) {
    (void)broker(300ms);
    const auto first = ask("Claude Code", ConsentCategory::Flash);
    const auto second = ask("Claude Code", ConsentCategory::Flash);
    EXPECT_GT(broker().remaining().count(), 0);
    ASSERT_TRUE(waitFor([&] { return first->decision.has_value(); }, 5s));
    EXPECT_EQ(code(*first), "mcp.consent_timeout");
    EXPECT_FALSE(second->decision.has_value()); // its countdown starts now
    ASSERT_TRUE(waitFor([&] { return second->decision.has_value(); }, 5s));
    EXPECT_EQ(code(*second), "mcp.consent_timeout");
}

TEST_F(McpConsent, WithdrawalOfTheShownAndOfAQueuedPrompt) {
    QSignalSpy withdrawn(&broker(), &GuiConsentBroker::withdrawn);
    const auto first = ask("Claude Code", ConsentCategory::Flash);
    const auto second = ask("Claude Code", ConsentCategory::Erase, "erase");
    const auto third = ask("Claude Code", ConsentCategory::MemoryWrite, "write_memory");
    second->cancel(); // queued
    EXPECT_EQ(broker().queued(), 1U);
    EXPECT_EQ(withdrawn.count(), 0);
    first->cancel(); // shown
    EXPECT_EQ(withdrawn.count(), 1);
    ASSERT_NE(broker().current(), nullptr);
    EXPECT_EQ(broker().current()->request.tool, "write_memory");
    answer(GuiConsentBroker::Answer::AllowOnce);
    EXPECT_FALSE(first->decision.has_value()); // never called after a withdrawal
    EXPECT_FALSE(second->decision.has_value());
    EXPECT_TRUE(third->decision && third->decision.value().has_value());
}

// ---- the prompt's texts -------------------------------------------------------------------------

TEST_F(McpConsent, PromptTextsPerCategory) {
    int shownTab = -2;
    ConsentViewModel vm(
        broker(),
        [](std::string_view id) {
            return id == "jlink:801000001"
                       ? ConsentViewModel::DeviceDescription{.text =
                                                                 QStringLiteral("CHESTER (tab 1)"),
                                                             .tabIndex = 0}
                       : ConsentViewModel::DeviceDescription{
                             .text = QStringLiteral("J-Link 801000002"), .tabIndex = -1};
        },
        [&shownTab](int tab) { shownTab = tab; });
    EXPECT_FALSE(vm.active());

    // Erase everything: erase + uicr in one prompt (ADR-0161), "Allow these for".
    ConsentRequest erase;
    erase.client = {.name = "Claude Code", .version = "1"};
    erase.clientKey = "Claude Code";
    erase.tool = "erase";
    erase.deviceId = "jlink:801000001";
    erase.categories = ConsentCategory::Erase | ConsentCategory::Uicr;
    erase.details = QJsonObject{{QStringLiteral("target"), QStringLiteral("app")},
                                {QStringLiteral("scope"), QStringLiteral("all")}};
    (void)broker().request(erase, [](const ConsentDecision&) {});
    EXPECT_TRUE(vm.active());
    EXPECT_EQ(vm.title(), QStringLiteral("Claude Code wants to erase the device"));
    EXPECT_EQ(vm.rememberLabel(), QStringLiteral("Allow these for Claude Code"));
    EXPECT_TRUE(vm.canShowDevice());
    const QVariantList rows = vm.details();
    QStringList texts;
    bool warning = false;
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        texts.append(map.value(QStringLiteral("value")).toString());
        warning = warning || map.value(QStringLiteral("warning")).toBool();
    }
    EXPECT_TRUE(texts.contains(QStringLiteral("CHESTER (tab 1)")));
    EXPECT_TRUE(texts.contains(QStringLiteral("all")));
    EXPECT_TRUE(texts.contains(QStringLiteral("The PIB (serial number, keys) is lost.")));
    EXPECT_TRUE(warning);
    EXPECT_TRUE(vm.countdownText().startsWith(QStringLiteral("Denied automatically in 2:00")));
    vm.showDevice();
    EXPECT_EQ(shownTab, 0);
    vm.deny(vm.promptId());

    // Connect a probe without a tab (ADR-0242 wording).
    ConsentRequest connect = erase;
    connect.tool = "connect";
    connect.deviceId = "jlink:801000002";
    connect.categories = ConsentCategory::Connect;
    connect.details = {};
    (void)broker().request(connect, [](const ConsentDecision&) {});
    EXPECT_EQ(vm.rememberLabel(), QStringLiteral("Allow connecting devices for Claude Code"));
    EXPECT_TRUE(vm.details().contains(QVariant(QVariantMap{
        {QStringLiteral("label"), QStringLiteral("Device")},
        {QStringLiteral("value"), QStringLiteral("Open a new tab for J-Link 801000002")},
        {QStringLiteral("warning"), false}})));
    EXPECT_FALSE(vm.canShowDevice());
    ASSERT_TRUE(waitFor([&] { return vm.armed(); }));
    vm.allowOnce(vm.promptId());

    // Flash with UICR data and the file size.
    ConsentRequest flash = erase;
    flash.tool = "flash";
    flash.categories = ConsentCategory::Flash | ConsentCategory::Uicr;
    flash.details = QJsonObject{{QStringLiteral("file"), QStringLiteral("/tmp/merged.hex")},
                                {QStringLiteral("size"), 412 * 1024},
                                {QStringLiteral("uicr"), true}};
    (void)broker().request(flash, [](const ConsentDecision&) {});
    EXPECT_EQ(vm.title(), QStringLiteral("Claude Code wants to flash firmware"));
    bool uicrWarning = false;
    for (const QVariant& row : vm.details()) {
        uicrWarning = uicrWarning || row.toMap()
                                         .value(QStringLiteral("value"))
                                         .toString()
                                         .startsWith(QStringLiteral("Contains UICR data"));
    }
    EXPECT_TRUE(uicrWarning);
    ASSERT_TRUE(waitFor([&] { return vm.armed(); }));
    vm.allowForClient(vm.promptId());
    EXPECT_FALSE(vm.active());
    spin(10ms);
}

// ---- review H1: answers carry the prompt id ----------------------------------------------------

TEST_F(McpConsent, ADoubleAnswerNeverAnswersTheNextPrompt) {
    const auto first = ask("Claude Code", ConsentCategory::Flash);
    const auto second = ask("Claude Code", ConsentCategory::Erase, "erase");
    const std::uint64_t firstId = broker().current()->id;
    broker().answer(firstId, GuiConsentBroker::Answer::AllowOnce);
    broker().answer(firstId, GuiConsentBroker::Answer::AllowOnce); // the second click
    EXPECT_TRUE(allowed(*first));
    EXPECT_FALSE(second->decision.has_value());
    ASSERT_NE(broker().current(), nullptr);
    EXPECT_EQ(broker().current()->request.tool, "erase");
}

TEST_F(McpConsent, AnAnswerForAReplacedPromptIsIgnored) {
    ConsentViewModel vm(broker(), {}, {});
    const auto first = ask("Claude Code", ConsentCategory::Flash);
    const qint64 firstId = vm.promptId();
    ASSERT_TRUE(waitFor([&] { return vm.armed(); }));
    const auto second = ask("Claude Code", ConsentCategory::Erase, "erase");
    first->cancel(); // withdrawn while the user answers: the second prompt replaces it
    EXPECT_NE(vm.promptId(), firstId);
    EXPECT_FALSE(vm.armed()); // the Allow buttons wait again
    vm.allowOnce(firstId);
    vm.allowOnce(vm.promptId()); // too early: not armed yet
    EXPECT_FALSE(second->decision.has_value());
    ASSERT_TRUE(waitFor([&] { return vm.armed(); }));
    EXPECT_GE(ConsentViewModel::armDelay.count(), 700);
    vm.allowOnce(vm.promptId());
    EXPECT_TRUE(allowed(*second));
    EXPECT_FALSE(first->decision.has_value());
}

TEST_F(McpConsent, WithdrawalAndANewPromptRace) {
    ConsentViewModel vm(broker(), {}, {});
    const auto first = ask("Claude Code", ConsentCategory::Flash);
    const qint64 firstId = vm.promptId();
    ASSERT_TRUE(waitFor([&] { return vm.armed(); }));
    first->cancel();
    EXPECT_FALSE(vm.active());
    const auto next = ask("Other", ConsentCategory::Flash);
    vm.deny(firstId); // a queued key press for the withdrawn prompt
    EXPECT_FALSE(next->decision.has_value());
    vm.deny(vm.promptId());
    EXPECT_EQ(code(*next), "mcp.consent_denied");
}

// ---- review M4: client names --------------------------------------------------------------------

TEST(McpClientName, SanitisedAndCapped) {
    EXPECT_EQ(hwio::gui::clientDisplayText("Claude Code"), QStringLiteral("Claude Code"));
    const QString bidi = hwio::gui::clientDisplayText("abc‮def⁦g‎h\nnext\x1b[31m");
    EXPECT_EQ(bidi, QStringLiteral("abcdefgh next"));
    const QString longName = hwio::gui::clientDisplayText(std::string(1000, 'n'));
    EXPECT_EQ(longName.size(), hwio::gui::maxClientNameChars);
    EXPECT_TRUE(longName.endsWith(QChar(0x2026)));
}

TEST_F(McpConsent, BrokerAndActivitySanitiseTheClient) {
    const auto request = ask("‮evil\nname", ConsentCategory::Flash);
    ASSERT_NE(broker().current(), nullptr);
    EXPECT_EQ(broker().current()->request.client.name, "evil name");
    ActivityModel model;
    auto e = entry(1);
    e.client = std::string(1000, 'c');
    model.record(std::move(e));
    EXPECT_EQ(model.data(model.index(0), ActivityModel::ClientRole).toString().size(),
              hwio::gui::maxClientNameChars);
    request->cancel();
}

} // namespace
