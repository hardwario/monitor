#include "hwio/core/docs_urls.h"

#include "hwio/core/error.h"

#include <QString>
#include <QStringLiteral>
#include <QStringView>
#include <QUrl>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace hwio {

namespace {

using namespace std::string_view_literals;
using namespace Qt::StringLiterals;

// ADR-0247: the only two places that change when the docs move (e.g. to docs.hardwario.com).
constexpr std::string_view baseUrl = "https://github.com/hardwario/monitor/blob/";
// <base><ref>/docs/<lang>/<page>.md#<anchor>
QString pagePath(std::string_view ref, std::string_view language, std::string_view page) {
    return QString::fromUtf8(baseUrl) + QString::fromUtf8(ref) + u"/docs/"_s +
           QString::fromUtf8(language) + u'/' + QString::fromUtf8(page) + u".md"_s;
}

constexpr std::string_view mainRef = "main";
constexpr std::string_view english = "en";

constexpr std::array<std::string_view, 5> languages{"en"sv, "cs"sv, "de"sv, "sk"sv, "pl"sv};

struct TopicEntry {
    DocsTopic topic = DocsTopic::Monitor;
    DocsPage page;
};

// A page in every docs language / in English only (ADR-0221).
constexpr TopicEntry userPage(DocsTopic topic, std::string_view page,
                              std::string_view anchor = {}) {
    return {.topic = topic, .page = {.page = page, .anchor = anchor, .translated = true}};
}
constexpr TopicEntry englishPage(DocsTopic topic, std::string_view page) {
    return {.topic = topic, .page = {.page = page, .anchor = {}, .translated = false}};
}

// Index == enum value (checked below). Anchors: the `<a id>` lines of docs/<lang>/*.md (ADR-0259).
constexpr std::array topicTable{
    userPage(DocsTopic::Monitor, "index"),
    userPage(DocsTopic::Install, "install"),
    userPage(DocsTopic::InstallJLink, "install", "segger-j-link-software"),
    userPage(DocsTopic::InstallUsbLinux, "install", "usb-access-to-the-j-link"),
    userPage(DocsTopic::InstallNrfUtil, "install", "nordic-nrf-util-lte-modem-firmware-only"),
    userPage(DocsTopic::GettingStarted, "getting-started"),
    userPage(DocsTopic::Console, "console"),
    userPage(DocsTopic::ConsoleBatch, "console", "batch-files"),
    userPage(DocsTopic::Flashing, "flashing"),
    userPage(DocsTopic::FlashingLte, "flashing", "lte-firmware-update"),
    userPage(DocsTopic::FlashingRecover, "flashing", "recover"),
    userPage(DocsTopic::Bluetooth, "bluetooth"),
    userPage(DocsTopic::BluetoothPairing, "bluetooth", "pairing"),
    userPage(DocsTopic::Troubleshooting, "troubleshooting"),
    userPage(DocsTopic::TroubleNoProbe, "troubleshooting", "no-j-link-found"),
    userPage(DocsTopic::TroubleProbeBusy, "troubleshooting",
             "the-j-link-is-used-by-another-program"),
    userPage(DocsTopic::TroubleNoPower, "troubleshooting",
             "the-device-has-no-power-or-does-not-respond"),
    userPage(DocsTopic::TroubleNoConsole, "troubleshooting", "no-console-rtt-found"),
    userPage(DocsTopic::TroubleFlashing, "troubleshooting",
             "flashing-fails-or-the-mcu-is-protected"),
    userPage(DocsTopic::TroubleOtherConnector, "troubleshooting",
             "the-j-link-is-on-the-lte-connector"),
    userPage(DocsTopic::TroubleNrfUtil, "troubleshooting", "lte-modem-firmware-needs-nrf-util"),
    userPage(DocsTopic::TroubleBluetooth, "troubleshooting",
             "bluetooth-device-not-found-or-pairing-fails"),
    userPage(DocsTopic::Privacy, "privacy"),
    userPage(DocsTopic::PrivacyReport, "privacy", "diagnostic-report"),
    englishPage(DocsTopic::Cli, "cli"),
    englishPage(DocsTopic::CliMigration, "cli-migration"),
    englishPage(DocsTopic::Mcp, "mcp"),
};

consteval bool topicTableMatchesEnum() {
    if (topicTable.size() != static_cast<std::size_t>(DocsTopic::Mcp) + 1) {
        return false;
    }
    for (std::size_t i = 0; i < topicTable.size(); ++i) {
        if (static_cast<std::size_t>(topicTable.at(i).topic) != i) {
            return false;
        }
    }
    return true;
}
static_assert(topicTableMatchesEnum(), "topicTable must list every DocsTopic in enum order");
static_assert(topicTable.size() <= 64, "plan 0037 bound: at most 64 topics");

// The docs language for a UI language: its primary subtag ("de_AT" → "de") when the docs have
// it and the page is translated, else English.
std::string_view docsLanguage(QStringView language, bool translated) {
    if (!translated) {
        return english;
    }
    const qsizetype end = [&]() -> qsizetype {
        for (qsizetype i = 0; i < language.size(); ++i) {
            if (language.at(i) == u'_' || language.at(i) == u'-') {
                return i;
            }
        }
        return language.size();
    }();
    const QString primary = language.first(end).toString().toLower();
    // A range-for, not find_if: the iterator type differs between the standard libraries.
    for (const std::string_view candidate : languages) {
        if (primary ==
            QLatin1StringView(candidate.data(), static_cast<qsizetype>(candidate.size()))) {
            return candidate;
        }
    }
    return english;
}

bool isRefCharacter(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' ||
           c == '-';
}

QUrl makeUrl(DocsPage page, std::string_view anchor, QStringView language,
             std::string_view appVersion) {
    QUrl url(pagePath(docsRef(appVersion), docsLanguage(language, page.translated), page.page));
    if (!anchor.empty()) {
        url.setFragment(QString::fromUtf8(anchor));
    }
    return url;
}

} // namespace

DocsPage docsPage(DocsTopic topic) noexcept {
    const auto index = static_cast<std::size_t>(topic);
    return index < topicTable.size() ? topicTable.at(index).page : topicTable.front().page;
}

std::span<const std::string_view> docsLanguages() noexcept { return languages; }

std::string docsRef(std::string_view appVersion) {
    if (appVersion.empty() || !std::ranges::all_of(appVersion, isRefCharacter)) {
        return std::string(mainRef); // also any "+build" metadata ('+' is not a ref character)
    }
    if (const std::size_t dash = appVersion.find('-'); dash != std::string_view::npos) {
        std::string_view preRelease = appVersion.substr(dash + 1);
        while (!preRelease.empty()) {
            const std::size_t dot = preRelease.find('.');
            if (preRelease.substr(0, dot) == "dev") {
                return std::string(mainRef);
            }
            preRelease =
                dot == std::string_view::npos ? std::string_view{} : preRelease.substr(dot + 1);
        }
    }
    return "v" + std::string(appVersion);
}

QUrl docsUrl(DocsTopic topic, QStringView language, std::string_view appVersion) {
    const DocsPage page = docsPage(topic);
    return makeUrl(page, page.anchor, language, appVersion);
}

QUrl errorDocsUrl(std::string_view errorId, QStringView language, std::string_view appVersion) {
    const bool known =
        std::ranges::any_of(errorTable, [&](const ErrorInfo& info) { return info.id == errorId; });
    if (!known) {
        return {};
    }
    return makeUrl(docsPage(DocsTopic::Troubleshooting), errorId, language, appVersion);
}

} // namespace hwio
