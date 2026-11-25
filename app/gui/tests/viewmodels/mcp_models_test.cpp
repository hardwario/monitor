// Plan 0014 A1/A2: the activity list (ring of 1000, roles, copy, clear), the client snippets (port,
// token, masked, CLI path, valid JSON), the consent broker (rules, grants per client and category,
// revoke, clientGone, forgetAll, queue overflow, timeout, withdrawal of the shown and of a queued
// prompt, several categories in one prompt, denyAll) and the prompt's view model texts (plan 0054:
// rows, labels, order, the firmware name with the full catalog ID, its bounds) and the Permissions
// rows (plan 0055: only the changed row is reported, never a reset; a language switch).

#include "gui_test_support.h"
#include "hwio/core/error.h"
#include "hwio/mcp/activity.h"
#include "hwio/mcp/consent.h"
#include "mcp/activity_model.h"
#include "mcp/client_name.h"
#include "mcp/client_snippets.h"
#include "mcp/consent_rules_model.h"
#include "mcp/consent_view_model.h"
#include "mcp/gui_consent_broker.h"
#include "settings_store.h"

#include <QByteArray>
#include <QChar>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLatin1Char>
#include <QList>
#include <QModelIndex>
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
using hwio::gui::ConsentRulesModel;
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
    // Plan 0050 (ADR-0377): how an allowed request was allowed.
    static std::optional<hwio::mcp::ConsentGrant> grant(const Outcome& outcome) {
        if (!outcome.decision.has_value() || !outcome.decision->has_value()) {
            return std::nullopt;
        }
        return outcome.decision->value();
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

// Plan 0062 (ADR-0436): a customer's device is asked every time: no rule, no client grant covers
// it, and "Allow for this client" on it is not remembered.
TEST_F(McpConsent, ACustomersDeviceIsNeverCoveredOrRemembered) {
    ASSERT_TRUE(broker().setAlwaysAllowed(ConsentCategory::Flash, true));
    const auto remoteRequest = [this](ConsentCategories categories) {
        auto outcome = std::make_shared<Outcome>();
        ConsentRequest request;
        request.client = {.name = "Claude Code", .version = "1"};
        request.clientKey = "Claude Code";
        request.tool = "flash";
        request.deviceId = "remote:1";
        request.categories = categories;
        request.remoteDevice = true;
        outcome->cancel = broker().request(
            std::move(request), [outcome](ConsentDecision d) { outcome->decision = std::move(d); });
        return outcome;
    };
    const auto flash = remoteRequest(ConsentCategory::Flash);
    EXPECT_FALSE(flash->decision.has_value()); // the rule does not cover it
    answer(GuiConsentBroker::Answer::AllowForClient);
    ASSERT_TRUE(allowed(*flash));
    EXPECT_EQ(grant(*flash), hwio::mcp::ConsentGrant::Prompted);
    EXPECT_TRUE(broker().grants().empty()); // not remembered
    const auto device = remoteRequest(ConsentCategory::RemoteDevice);
    EXPECT_FALSE(device->decision.has_value());
    answer(GuiConsentBroker::Answer::Deny);
    EXPECT_EQ(code(*device), "mcp.consent_denied");
}

// Plan 0055 (ADR-0390): a test describer; `prefix` makes a language switch visible.
ConsentRulesModel::Describer rulesDescriber(const QString& prefix) {
    return [&prefix](ConsentCategory category) {
        return ConsentRulesModel::Texts{
            .title = prefix + QString::number(static_cast<int>(category)),
            .hint =
                category == ConsentCategory::MemoryWrite ? QStringLiteral("warning") : QString()};
    };
}

TEST_F(McpConsent, RulesModelReportsOnlyTheChangedRow) {
    const QString prefix = QStringLiteral("en ");
    ASSERT_TRUE(broker().setAlwaysAllowed(ConsentCategory::Connect, true));
    ConsentRulesModel model(broker(), rulesDescriber(prefix));
    ASSERT_EQ(model.rowCount(), 5);
    EXPECT_EQ(model.data(model.index(0), ConsentRulesModel::RuleIdRole).toString(),
              QStringLiteral("flash"));
    EXPECT_EQ(model.data(model.index(2), ConsentRulesModel::RuleIdRole).toString(),
              QStringLiteral("memory-write"));
    EXPECT_EQ(model.data(model.index(2), ConsentRulesModel::HintRole).toString(),
              QStringLiteral("warning"));
    EXPECT_TRUE(model.data(model.index(0), ConsentRulesModel::HintRole).toString().isEmpty());
    EXPECT_FALSE(model.data(model.index(1), ConsentRulesModel::AllowRole).toBool());
    EXPECT_TRUE(model.data(model.index(4), ConsentRulesModel::AllowRole).toBool()); // stored
    const QList<QByteArray> names = model.roleNames().values();
    for (const char* role : {"ruleId", "title", "hint", "allow"}) {
        EXPECT_TRUE(names.contains(role)) << role;
    }

    QSignalSpy changed(&model, &ConsentRulesModel::dataChanged);
    QSignalSpy reset(&model, &ConsentRulesModel::modelAboutToBeReset);
    QSignalSpy inserted(&model, &ConsentRulesModel::rowsInserted);
    QSignalSpy removed(&model, &ConsentRulesModel::rowsRemoved);
    QSignalSpy layout(&model, &ConsentRulesModel::layoutChanged);

    ASSERT_TRUE(broker().setAlwaysAllowed(ConsentCategory::Erase, true));
    ASSERT_EQ(changed.count(), 1);
    EXPECT_EQ(changed.front().at(0).value<QModelIndex>().row(), 1);
    EXPECT_EQ(changed.front().at(1).value<QModelIndex>().row(), 1);
    EXPECT_EQ(changed.front().at(2).value<QList<int>>(), QList<int>{ConsentRulesModel::AllowRole});
    EXPECT_TRUE(model.data(model.index(1), ConsentRulesModel::AllowRole).toBool());

    // The same value again (the store still reports the write): nothing changed, nothing emitted.
    ASSERT_TRUE(broker().setAlwaysAllowed(ConsentCategory::Erase, true));
    EXPECT_EQ(changed.count(), 1);
    ASSERT_TRUE(broker().setAlwaysAllowed(ConsentCategory::Erase, false));
    EXPECT_EQ(changed.count(), 2);
    EXPECT_FALSE(model.data(model.index(1), ConsentRulesModel::AllowRole).toBool());

    EXPECT_EQ(reset.count(), 0);
    EXPECT_EQ(inserted.count(), 0);
    EXPECT_EQ(removed.count(), 0);
    EXPECT_EQ(layout.count(), 0);
}

TEST_F(McpConsent, RulesModelRetranslatesTheTexts) {
    QString prefix = QStringLiteral("en ");
    ConsentRulesModel model(broker(), rulesDescriber(prefix));
    EXPECT_EQ(model.data(model.index(0), ConsentRulesModel::TitleRole).toString(),
              QStringLiteral("en ") + QString::number(static_cast<int>(ConsentCategory::Flash)));
    QSignalSpy changed(&model, &ConsentRulesModel::dataChanged);
    QSignalSpy reset(&model, &ConsentRulesModel::modelAboutToBeReset);
    prefix = QStringLiteral("cs ");
    model.retranslate();
    ASSERT_EQ(changed.count(), 1);
    EXPECT_EQ(changed.front().at(0).value<QModelIndex>().row(), 0);
    EXPECT_EQ(changed.front().at(1).value<QModelIndex>().row(), 4);
    const auto roles = changed.front().at(2).value<QList<int>>();
    EXPECT_TRUE(roles.contains(ConsentRulesModel::TitleRole));
    EXPECT_TRUE(roles.contains(ConsentRulesModel::HintRole));
    EXPECT_FALSE(roles.contains(ConsentRulesModel::AllowRole));
    EXPECT_EQ(model.data(model.index(0), ConsentRulesModel::TitleRole).toString(),
              QStringLiteral("cs ") + QString::number(static_cast<int>(ConsentCategory::Flash)));
    EXPECT_EQ(reset.count(), 0);
}

TEST_F(McpConsent, AllowOnceAndDeny) {
    const auto first = ask("Claude Code", ConsentCategory::Erase, "erase");
    const auto second = ask("Claude Code", ConsentCategory::Erase, "erase");
    EXPECT_EQ(broker().queued(), 1U);
    answer(GuiConsentBroker::Answer::AllowOnce);
    ASSERT_TRUE(first->decision.has_value());
    EXPECT_TRUE(allowed(*first));
    EXPECT_EQ(grant(*first), hwio::mcp::ConsentGrant::Prompted);
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
    EXPECT_EQ(grant(*a1), hwio::mcp::ConsentGrant::Prompted);
    EXPECT_EQ(grant(*a2), hwio::mcp::ConsentGrant::Remembered);
    EXPECT_FALSE(b1->decision.has_value()); // another client: still asked
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
    const auto again = ask("Claude Code", ConsentCategory::Flash);
    EXPECT_TRUE(again->decision.has_value());
    EXPECT_EQ(grant(*again), hwio::mcp::ConsentGrant::Remembered);
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
    EXPECT_EQ(vm.rememberLabel(), QStringLiteral("Always allow these for Claude Code"));
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
    EXPECT_EQ(vm.rememberLabel(),
              QStringLiteral("Always allow connecting devices for Claude Code"));
    EXPECT_TRUE(vm.details().contains(QVariant(QVariantMap{
        {QStringLiteral("label"), QStringLiteral("Device")},
        {QStringLiteral("value"), QStringLiteral("Open a new tab for J-Link 801000002")},
        {QStringLiteral("warning"), false},
        {QStringLiteral("note"), QString()},
        {QStringLiteral("action"), false}})));
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

// ---- plan 0054 (ADR-0389): rows, labels and order -------------------------------------------

// The labels of the shown prompt's rows ("" for a warning), in order.
QStringList labels(const ConsentViewModel& vm) {
    QStringList result;
    for (const QVariant& row : vm.details()) {
        result.append(row.toMap().value(QStringLiteral("label")).toString());
    }
    return result;
}

// The row with `label` (empty map when none).
QVariantMap rowOf(const ConsentViewModel& vm, const QString& label) {
    for (const QVariant& row : vm.details()) {
        if (row.toMap().value(QStringLiteral("label")).toString() == label) {
            return row.toMap();
        }
    }
    return {};
}

class McpConsentRows : public McpConsent {
protected:
    static constexpr const char* catalogId = "59489e4e91d741da930ab15b1ab9e31f";

    // Shows `request` (the previous prompt is denied first).
    void show(ConsentViewModel& vm, ConsentRequest request) {
        if (vm.active()) {
            vm.deny(vm.promptId());
        }
        (void)broker().request(std::move(request), [](const ConsentDecision&) {});
        ASSERT_TRUE(vm.active());
    }

    static ConsentRequest request(std::string tool, ConsentCategories categories,
                                  QJsonObject details) {
        ConsentRequest result;
        result.client = {.name = "Claude Code", .version = "1"};
        result.clientKey = "Claude Code";
        result.tool = std::move(tool);
        result.deviceId = "jlink:801000001";
        result.categories = categories;
        result.details = std::move(details);
        return result;
    }

    static QJsonObject catalogDetails(const QString& name, const QString& version) {
        QJsonObject details{{QStringLiteral("catalog_id"), QString::fromLatin1(catalogId)},
                            {QStringLiteral("size"), 660963},
                            {QStringLiteral("target"), QStringLiteral("app")}};
        if (!name.isNull()) {
            details.insert(QStringLiteral("firmware_name"), name);
            details.insert(QStringLiteral("firmware_version"), version);
        }
        return details;
    }

    static ConsentViewModel::DeviceDescription describe(std::string_view /*id*/) {
        return {.text = QStringLiteral("CHESTER · J-Link 822005109 (tab 1)"), .tabIndex = 0};
    }
};

// Catalog firmware with the page read: "<name> <version>  (<size>)" with the full ID as the
// note; then Device (with the link), MCU, Client; the permanent button says "Always".
TEST_F(McpConsentRows, CatalogFirmwareByName) {
    ConsentViewModel vm(broker(), &McpConsentRows::describe, {});
    show(vm,
         request("flash", ConsentCategory::Flash,
                 catalogDetails(QStringLiteral("hio-chester-clime"), QStringLiteral("v3.2.0"))));
    EXPECT_EQ(labels(vm), (QStringList{QStringLiteral("Firmware"), QStringLiteral("Device"),
                                       QStringLiteral("MCU"), QStringLiteral("Client")}));
    const QVariantMap firmware = rowOf(vm, QStringLiteral("Firmware"));
    EXPECT_TRUE(firmware.value(QStringLiteral("value"))
                    .toString()
                    .startsWith(QStringLiteral("hio-chester-clime v3.2.0  (")));
    EXPECT_EQ(firmware.value(QStringLiteral("note")).toString(),
              QStringLiteral("Catalog ID %1").arg(QString::fromLatin1(catalogId)));
    EXPECT_FALSE(firmware.value(QStringLiteral("action")).toBool());
    const QVariantMap device = rowOf(vm, QStringLiteral("Device"));
    EXPECT_EQ(device.value(QStringLiteral("value")).toString(),
              QStringLiteral("CHESTER · J-Link 822005109 (tab 1)"));
    EXPECT_TRUE(device.value(QStringLiteral("action")).toBool());
    EXPECT_EQ(rowOf(vm, QStringLiteral("MCU")).value(QStringLiteral("value")).toString(),
              QStringLiteral("Application MCU"));
    EXPECT_EQ(rowOf(vm, QStringLiteral("Client")).value(QStringLiteral("value")).toString(),
              QStringLiteral("Claude Code, version 1"));
    EXPECT_EQ(vm.rememberLabel(), QStringLiteral("Always allow flashing for Claude Code"));
    vm.deny(vm.promptId());
}

// The page was not read (cached file, offline): the ID row as before, "<id>  (<size>)", no note.
TEST_F(McpConsentRows, CatalogFirmwareWithoutTheName) {
    ConsentViewModel vm(broker(), &McpConsentRows::describe, {});
    show(vm, request("flash", ConsentCategory::Flash, catalogDetails(QString(), QString())));
    EXPECT_EQ(labels(vm), (QStringList{QStringLiteral("Catalog ID"), QStringLiteral("Device"),
                                       QStringLiteral("MCU"), QStringLiteral("Client")}));
    const QVariantMap id = rowOf(vm, QStringLiteral("Catalog ID"));
    EXPECT_TRUE(id.value(QStringLiteral("value"))
                    .toString()
                    .startsWith(QStringLiteral("%1  (").arg(QString::fromLatin1(catalogId))));
    EXPECT_TRUE(id.value(QStringLiteral("note")).toString().isEmpty());
    // An empty name (or only whitespace and control characters) counts as no name.
    show(vm, request("flash", ConsentCategory::Flash,
                     catalogDetails(QStringLiteral(" \n\t‮"), QStringLiteral("v1"))));
    EXPECT_EQ(labels(vm).value(0), QStringLiteral("Catalog ID"));
    vm.deny(vm.promptId());
}

// Security property (ADR-0389): a name can never replace or hide the full ID; it is one line of
// plain text, bounded to maxCatalogFieldBytes and cut with "…".
TEST_F(McpConsentRows, AFirmwareNameNeverHidesTheId) {
    ConsentViewModel vm(broker(), &McpConsentRows::describe, {});
    const QString id = QString::fromLatin1(catalogId);
    // A forged second line and a bidi override are flattened / dropped.
    show(vm,
         request("flash", ConsentCategory::Flash,
                 catalogDetails(QStringLiteral("evil\nCatalog ID 00000000000000000000000000000000"
                                               "‮"),
                                QStringLiteral("v1\r\n"))));
    QVariantMap firmware = rowOf(vm, QStringLiteral("Firmware"));
    const QString value = firmware.value(QStringLiteral("value")).toString();
    EXPECT_TRUE(
        value.startsWith(QStringLiteral("evil Catalog ID 00000000000000000000000000000000 v1  (")))
        << value.toStdString();
    EXPECT_FALSE(value.contains(QLatin1Char('\n')));
    EXPECT_FALSE(value.contains(QChar(u'‮')));
    EXPECT_EQ(firmware.value(QStringLiteral("note")).toString(),
              QStringLiteral("Catalog ID %1").arg(id));
    // Longer than the limit: cut with "…"; the ID stays complete.
    show(vm,
         request("flash", ConsentCategory::Flash,
                 catalogDetails(QString(300, QLatin1Char('a')), QString(300, QLatin1Char('1')))));
    firmware = rowOf(vm, QStringLiteral("Firmware"));
    const QString cut = firmware.value(QStringLiteral("value")).toString();
    const QString expectedName = QString(128, QLatin1Char('a')) + QChar(u'…');
    const QString expectedVersion = QString(128, QLatin1Char('1')) + QChar(u'…');
    EXPECT_TRUE(cut.startsWith(QStringLiteral("%1 %2  (").arg(expectedName, expectedVersion)))
        << cut.toStdString();
    EXPECT_EQ(firmware.value(QStringLiteral("note")).toString(),
              QStringLiteral("Catalog ID %1").arg(id));
    // A catalog ID without a name, and a file: the full ID / path in the value.
    show(vm, request("flash", ConsentCategory::Flash, catalogDetails(QString(), QString())));
    EXPECT_TRUE(rowOf(vm, QStringLiteral("Catalog ID"))
                    .value(QStringLiteral("value"))
                    .toString()
                    .contains(id));
    vm.deny(vm.promptId());
}

// A file: "File <full path>  (<size>)" first; LTE: the MCU title "LTE MCU"; an unknown target
// shows its id; erase and connect keep their rows with the client last.
TEST_F(McpConsentRows, FileLteEraseAndConnect) {
    ConsentViewModel vm(broker(), &McpConsentRows::describe, {});
    const QString path =
        QStringLiteral("/home/me/a very long directory name/build/zephyr/merged.hex");
    show(vm, request("flash", ConsentCategory::Flash,
                     QJsonObject{{QStringLiteral("file"), path},
                                 {QStringLiteral("size"), 1000},
                                 {QStringLiteral("target"), QStringLiteral("app")}}));
    EXPECT_EQ(labels(vm), (QStringList{QStringLiteral("File"), QStringLiteral("Device"),
                                       QStringLiteral("MCU"), QStringLiteral("Client")}));
    EXPECT_TRUE(rowOf(vm, QStringLiteral("File"))
                    .value(QStringLiteral("value"))
                    .toString()
                    .startsWith(path + QStringLiteral("  (")));

    show(vm,
         request("flash_lte", ConsentCategory::Flash,
                 QJsonObject{{QStringLiteral("file"), QStringLiteral("/tmp/mfw_nrf91x1_2.0.2.zip")},
                             {QStringLiteral("target"), QStringLiteral("lte")},
                             {QStringLiteral("kind"), QStringLiteral("modem")}}));
    EXPECT_EQ(rowOf(vm, QStringLiteral("File")).value(QStringLiteral("value")).toString(),
              QStringLiteral("/tmp/mfw_nrf91x1_2.0.2.zip"));
    EXPECT_EQ(rowOf(vm, QStringLiteral("MCU")).value(QStringLiteral("value")).toString(),
              QStringLiteral("LTE MCU"));

    show(vm, request("erase", ConsentCategory::Erase,
                     QJsonObject{{QStringLiteral("target"), QStringLiteral("radio")},
                                 {QStringLiteral("scope"), QStringLiteral("code")}}));
    EXPECT_EQ(labels(vm), (QStringList{QStringLiteral("Device"), QStringLiteral("MCU"),
                                       QStringLiteral("Scope"), QStringLiteral("Client")}));
    EXPECT_EQ(rowOf(vm, QStringLiteral("MCU")).value(QStringLiteral("value")).toString(),
              QStringLiteral("radio"));
    EXPECT_EQ(vm.rememberLabel(), QStringLiteral("Always allow erasing for Claude Code"));

    ConsentRequest connect = request("connect", ConsentCategory::Connect, {});
    show(vm, std::move(connect));
    EXPECT_EQ(labels(vm), (QStringList{QStringLiteral("Device"), QStringLiteral("Client")}));
    EXPECT_TRUE(rowOf(vm, QStringLiteral("Device")).value(QStringLiteral("action")).toBool());
    // Without a client version the Client row is absent (ADR-0241).
    ConsentRequest unversioned = request("erase", ConsentCategory::Erase, {});
    unversioned.client.version.clear();
    show(vm, std::move(unversioned));
    EXPECT_FALSE(labels(vm).contains(QStringLiteral("Client")));
    vm.deny(vm.promptId());
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
