#pragma once

// The user documentation links of the app in one table (ADR-0138's central URL table; plan 0028
// D18, ADR-0203; plan 0037). Every link goes to the `docs/<lang>/<page>.md` pages of the
// repository on GitHub at the release tag of the running app (ADR-0247), in the UI language
// when the page is translated (ADR-0221), else in English. Anchors are explicit `<a id>` lines
// with English ids, identical in all languages (ADR-0259); the `DocsAnchors` test checks that
// every anchor of the table and every error id exists in the pages. Publishing the docs on
// docs.hardwario.com later changes only this table (base URL and path pattern).

#include "hwio/core/version.h"

#include <QStringView>
#include <QUrl>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace hwio {

enum class DocsTopic : std::uint8_t {
    Monitor,               // index.md (Help → Documentation, Settings → About)
    Install,               // install.md
    InstallJLink,          // install.md#segger-j-link-software
    InstallUsbLinux,       // install.md#usb-access-to-the-j-link
    InstallNrfUtil,        // install.md#nordic-nrf-util-lte-modem-firmware-only
    GettingStarted,        // getting-started.md
    Console,               // console.md
    ConsoleBatch,          // console.md#batch-files (Console → Batch ⓘ)
    Flashing,              // flashing.md
    FlashingLte,           // flashing.md#lte-firmware-update
    FlashingRecover,       // flashing.md#recover
    Bluetooth,             // bluetooth.md
    BluetoothPairing,      // bluetooth.md#pairing
    Troubleshooting,       // troubleshooting.md
    TroubleNoProbe,        // troubleshooting.md#no-j-link-found
    TroubleProbeBusy,      // troubleshooting.md#the-j-link-is-used-by-another-program
    TroubleNoPower,        // troubleshooting.md#the-device-has-no-power-or-does-not-respond
    TroubleNoConsole,      // troubleshooting.md#no-console-rtt-found
    TroubleFlashing,       // troubleshooting.md#flashing-fails-or-the-mcu-is-protected
    TroubleOtherConnector, // troubleshooting.md#the-j-link-is-on-the-lte-connector
    TroubleNrfUtil,        // troubleshooting.md#lte-modem-firmware-needs-nrf-util
    TroubleBluetooth,      // troubleshooting.md#bluetooth-device-not-found-or-pairing-fails
    Privacy,               // privacy.md
    PrivacyReport,         // privacy.md#diagnostic-report
    Cli,                   // cli.md            (English only)
    CliMigration,          // cli-migration.md  (English only)
    Mcp,                   // mcp.md            (English only; the last topic)
};

// One table entry: the page name without ".md", the anchor (empty = top of the page) and
// whether the page exists in every docs language (false = English only, ADR-0221).
struct DocsPage {
    std::string_view page;
    std::string_view anchor;
    bool translated;
};

[[nodiscard]] DocsPage docsPage(DocsTopic topic) noexcept;

// The languages of the docs: {"en", "cs", "de", "sk", "pl"} (ADR-0221).
[[nodiscard]] std::span<const std::string_view> docsLanguages() noexcept;

// `language`: a UI language ("de", "de_DE", "de-AT", "pt"); its primary subtag is used. Not in
// docsLanguages() or the page is English only → "en". `appVersion`: the app version; the ref
// rule is docsRef().
[[nodiscard]] QUrl docsUrl(DocsTopic topic, QStringView language,
                           std::string_view appVersion = buildInfo().version);

// troubleshooting.md#<errorId> in `language` (ADR-0258). An empty URL when `errorId` is not an
// id of errorTable.
[[nodiscard]] QUrl errorDocsUrl(std::string_view errorId, QStringView language,
                                std::string_view appVersion = buildInfo().version);

// The git ref of the docs for an app version (ADR-0259): "v<version>" for a release or release
// candidate ("2.0.0" → "v2.0.0", "2.0.0-rc.3" → "v2.0.0-rc.3"); "main" for a version with build
// metadata ("+…"), a "dev" pre-release identifier ("2.0.1-dev.5"), an empty version or one with
// characters outside [0-9A-Za-z.-].
[[nodiscard]] std::string docsRef(std::string_view appVersion);

} // namespace hwio
