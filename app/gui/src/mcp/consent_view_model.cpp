#include "mcp/consent_view_model.h"

#include "hwio/core/json.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/mcp/consent.h"
#include "mcp/gui_consent_broker.h"

#include <QChar>
#include <QCursor>
#include <QGuiApplication>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QStringLiteral>
#include <QStringView>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>
#include <QtContainerFwd>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace hwio::gui {

namespace {

using hwio::mcp::ConsentCategory;

// One detail row (plan 0054): `note` is a small second line under the value, `action` puts the
// "Show device" link into the row.
QVariantMap row(const QString& label, const QString& value, bool warning = false,
                const QString& note = QString(), bool action = false) {
    return {{QStringLiteral("label"), label},
            {QStringLiteral("value"), value},
            {QStringLiteral("warning"), warning},
            {QStringLiteral("note"), note},
            {QStringLiteral("action"), action}};
}

constexpr std::size_t utf8Size(char32_t codePoint) {
    if (codePoint < 0x80U) {
        return 1;
    }
    if (codePoint < 0x800U) {
        return 2;
    }
    return codePoint < 0x10000U ? 3 : 4;
}

// A field from the catalog page (plan 0054, ADR-0389): one line of printable text (whitespace
// runs become one space; control and format characters such as bidi overrides are dropped), at
// most maxCatalogFieldBytes of UTF-8, cut with "…". It never replaces the catalog ID, which is
// always shown in full.
QString plainField(const QString& text) {
    QString result;
    std::size_t bytes = 0;
    bool space = false;
    for (const char32_t codePoint : text.toUcs4()) {
        if (QChar::isSpace(codePoint)) {
            space = !result.isEmpty();
            continue;
        }
        if (!QChar::isPrint(codePoint)) {
            continue;
        }
        const std::size_t size = utf8Size(codePoint) + (space ? 1 : 0);
        if (bytes + size > hwio::firmware::maxCatalogFieldBytes) {
            result.append(QChar(u'\u2026')); // …
            return result;
        }
        if (space) {
            result.append(QLatin1Char(' '));
            space = false;
        }
        result.append(QStringView(QChar::fromUcs4(codePoint)));
        bytes += size;
    }
    return result;
}

// The MCU of a target as the mock of plan 0054 shows it; an unknown target shows its id.
// (Plan 0040's LoRaWAN module target adds "LoRaWAN module" here when the branches meet.)
QString mcuTitle(const QString& target) {
    if (target == QStringLiteral("app")) {
        return ConsentViewModel::tr("Application MCU");
    }
    if (target == QStringLiteral("lte")) {
        return ConsentViewModel::tr("LTE MCU");
    }
    return target;
}

// ADR-0389: the permanent choice says that it is permanent.
QString rememberText(hwio::mcp::ConsentCategories categories, const QString& client) {
    int count = 0;
    for (const ConsentCategory category : GuiConsentBroker::categories) {
        count += categories.testFlag(category) ? 1 : 0;
    }
    if (count != 1) {
        return ConsentViewModel::tr("Always allow these for %1").arg(client);
    }
    if (categories.testFlag(ConsentCategory::Flash)) {
        return ConsentViewModel::tr("Always allow flashing for %1").arg(client);
    }
    if (categories.testFlag(ConsentCategory::Erase)) {
        return ConsentViewModel::tr("Always allow erasing for %1").arg(client);
    }
    if (categories.testFlag(ConsentCategory::MemoryWrite)) {
        return ConsentViewModel::tr("Always allow memory writes for %1").arg(client);
    }
    if (categories.testFlag(ConsentCategory::Uicr)) {
        return ConsentViewModel::tr("Always allow UICR writes for %1").arg(client);
    }
    return ConsentViewModel::tr("Always allow connecting devices for %1").arg(client);
}

// Plan 0054 (ADR-0389): what is written first. The full file path and the full catalog ID are
// always shown; a firmware name from the catalog page never replaces them.
void appendWrittenRow(QVariantList& rows, const QJsonObject& details) {
    const QLocale locale;
    const qint64 size = details.value(QStringLiteral("size")).toInteger(-1);
    const QString sizeText = size >= 0 ? locale.formattedDataSize(size) : QString();
    const auto withSize = [&sizeText](const QString& text) {
        return sizeText.isEmpty() ? text : QStringLiteral("%1  (%2)").arg(text, sizeText);
    };
    const QString file = details.value(QStringLiteral("file")).toString();
    const QString catalog = details.value(QStringLiteral("catalog_id")).toString();
    if (!file.isEmpty()) {
        rows.append(row(ConsentViewModel::tr("File"), withSize(file)));
        return;
    }
    if (catalog.isEmpty()) {
        if (!sizeText.isEmpty()) {
            rows.append(row(ConsentViewModel::tr("Size"), sizeText));
        }
        return;
    }
    const QString name = plainField(details.value(QStringLiteral("firmware_name")).toString());
    const QString version =
        plainField(details.value(QStringLiteral("firmware_version")).toString());
    if (name.isEmpty()) {
        rows.append(row(ConsentViewModel::tr("Catalog ID"), withSize(catalog)));
        return;
    }
    rows.append(row(ConsentViewModel::tr("Firmware"),
                    withSize(version.isEmpty() ? name : QStringLiteral("%1 %2").arg(name, version)),
                    false, ConsentViewModel::tr("Catalog ID %1").arg(catalog)));
}

// The device, with the "Show device" link when it has a tab.
QVariantMap deviceRow(const hwio::mcp::ConsentRequest& request,
                      const ConsentViewModel::DeviceDescription& device) {
    const bool showDevice = device.tabIndex >= 0;
    if (request.categories.testFlag(ConsentCategory::Connect)) {
        return row(ConsentViewModel::tr("Device"),
                   showDevice ? ConsentViewModel::tr("Connect the tab %1").arg(device.text)
                              : ConsentViewModel::tr("Open a new tab for %1").arg(device.text),
                   false, QString(), showDevice);
    }
    return row(ConsentViewModel::tr("Device"), device.text, false, QString(), showDevice);
}

// Profile, MCU, scope and address range, each only when given.
void appendTargetRows(QVariantList& rows, const QJsonObject& details) {
    if (const QString profile = details.value(QStringLiteral("profile")).toString();
        !profile.isEmpty()) {
        rows.append(row(ConsentViewModel::tr("Profile"), profile));
    }
    if (const QString target = details.value(QStringLiteral("target")).toString();
        !target.isEmpty()) {
        rows.append(row(ConsentViewModel::tr("MCU"), mcuTitle(target)));
    }
    if (const QString scope = details.value(QStringLiteral("scope")).toString(); !scope.isEmpty()) {
        rows.append(row(ConsentViewModel::tr("Scope"), scope));
    }
    if (const QString range = details.value(QStringLiteral("range")).toString(); !range.isEmpty()) {
        rows.append(row(ConsentViewModel::tr("Address range"), range));
    }
}

// Warnings (ADR-0161, ADR-0164).
void appendWarningRows(QVariantList& rows, const hwio::mcp::ConsentRequest& request) {
    const bool pibLoss =
        request.tool == "recover" ||
        (request.tool == "erase" && request.categories.testFlag(ConsentCategory::Uicr));
    if (pibLoss) {
        rows.append(
            row(QString(), ConsentViewModel::tr("The PIB (serial number, keys) is lost."), true));
    } else if (request.categories.testFlag(ConsentCategory::Uicr) ||
               request.details.value(QStringLiteral("uicr")).toBool()) {
        rows.append(
            row(QString(),
                ConsentViewModel::tr("Contains UICR data: factory data of the device may be "
                                     "overwritten."),
                true));
    }
    if (request.categories.testFlag(ConsentCategory::MemoryWrite)) {
        rows.append(row(
            QString(),
            ConsentViewModel::tr("Writing memory can make the device run arbitrary code."), true));
    }
}

} // namespace

ConsentViewModel::ConsentViewModel(GuiConsentBroker& broker, DeviceDescriber describe,
                                   std::function<void(int)> showTab, QObject* parent)
    : QObject(parent), m_broker(&broker), m_describe(std::move(describe)),
      m_showTab(std::move(showTab)) {
    m_tick.setInterval(std::chrono::seconds(1));
    connect(&m_tick, &QTimer::timeout, this, [this] { secondsLeftChanged(); });
    m_arm.setSingleShot(true);
    m_arm.setInterval(armDelay);
    connect(&m_arm, &QTimer::timeout, this, [this] {
        m_armed = true;
        armedChanged(); // signal
    });
    connect(m_broker, &GuiConsentBroker::promptChanged, this, &ConsentViewModel::update);
}

void ConsentViewModel::update() {
    if (active()) {
        m_tick.start();
    } else {
        m_tick.stop();
    }
    // Review H1: another prompt is shown: the Allow buttons wait armDelay.
    const std::uint64_t shown = m_broker->current() != nullptr ? m_broker->current()->id : 0;
    if (shown != m_shownId) {
        m_shownId = shown;
        m_arm.stop();
        if (m_armed) {
            m_armed = false;
            armedChanged(); // signal
        }
        if (shown != 0) {
            m_arm.start();
        }
    }
    changed();            // signal
    secondsLeftChanged(); // signal
}

bool ConsentViewModel::active() const { return m_broker->current() != nullptr; }

qint64 ConsentViewModel::promptId() const { return static_cast<qint64>(m_shownId); }

QString ConsentViewModel::client() const {
    const auto* prompt = m_broker->current();
    if (prompt == nullptr) {
        return {};
    }
    const QString name = toQString(prompt->request.client.name);
    return name.isEmpty() ? tr("An AI assistant") : name;
}

QString ConsentViewModel::title() const {
    const auto* prompt = m_broker->current();
    if (prompt == nullptr) {
        return {};
    }
    const QString who = client();
    const std::string& tool = prompt->request.tool;
    if (tool == "flash") {
        return tr("%1 wants to flash firmware").arg(who);
    }
    if (tool == "flash_lte") {
        return tr("%1 wants to update the LTE modem firmware").arg(who);
    }
    if (tool == "write_flash") {
        return tr("%1 wants to write to the flash memory").arg(who);
    }
    if (tool == "erase") {
        return tr("%1 wants to erase the device").arg(who);
    }
    if (tool == "recover") {
        return tr("%1 wants to recover the device (erases everything)").arg(who);
    }
    if (tool == "write_memory") {
        return tr("%1 wants to write to the memory").arg(who);
    }
    if (tool == "connect") {
        return tr("%1 wants to connect a device").arg(who);
    }
    return tr("%1 wants to run %2").arg(who, toQString(tool));
}

QVariantList ConsentViewModel::details() const {
    const auto* prompt = m_broker->current();
    if (prompt == nullptr) {
        return {};
    }
    const hwio::mcp::ConsentRequest& request = prompt->request;
    QVariantList rows;
    appendWrittenRow(rows, request.details);
    const DeviceDescription device =
        m_describe ? m_describe(request.deviceId)
                   : DeviceDescription{.text = toQString(request.deviceId), .tabIndex = -1};
    rows.append(deviceRow(request, device));
    appendTargetRows(rows, request.details);
    // ADR-0241: the client with its version when given (last, ADR-0389).
    if (!request.client.version.empty()) {
        rows.append(row(tr("Client"),
                        tr("%1, version %2").arg(client(), toQString(request.client.version))));
    }
    appendWarningRows(rows, request);
    return rows;
}

QString ConsentViewModel::rememberLabel() const {
    const auto* prompt = m_broker->current();
    return prompt == nullptr ? QString() : rememberText(prompt->request.categories, client());
}

bool ConsentViewModel::canShowDevice() const {
    const auto* prompt = m_broker->current();
    return prompt != nullptr && m_describe && m_describe(prompt->request.deviceId).tabIndex >= 0;
}

int ConsentViewModel::queued() const { return static_cast<int>(m_broker->queued()); }

int ConsentViewModel::secondsLeft() const {
    const auto left = std::chrono::ceil<std::chrono::seconds>(m_broker->remaining());
    return static_cast<int>(left.count());
}

QString ConsentViewModel::countdownText() const {
    if (!active()) {
        return {};
    }
    const int seconds = secondsLeft();
    const QString time =
        QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return tr("Denied automatically in %1 if not answered.").arg(time);
}

void ConsentViewModel::deny(qint64 promptId) {
    m_broker->answer(static_cast<std::uint64_t>(promptId), GuiConsentBroker::Answer::Deny);
}

void ConsentViewModel::allowOnce(qint64 promptId) {
    if (m_armed) {
        m_broker->answer(static_cast<std::uint64_t>(promptId), GuiConsentBroker::Answer::AllowOnce);
    }
}

void ConsentViewModel::allowForClient(qint64 promptId) {
    if (m_armed) {
        m_broker->answer(static_cast<std::uint64_t>(promptId),
                         GuiConsentBroker::Answer::AllowForClient);
    }
}

void ConsentViewModel::showDevice() {
    const auto* prompt = m_broker->current();
    if (prompt == nullptr || !m_describe || !m_showTab) {
        return;
    }
    if (const int tab = m_describe(prompt->request.deviceId).tabIndex; tab >= 0) {
        m_showTab(tab);
    }
}

void ConsentViewModel::place(QWindow* prompt, QWindow* mainWindow) {
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr && mainWindow != nullptr) {
        screen = mainWindow->screen();
    }
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (prompt != nullptr && screen != nullptr) {
        prompt->setScreen(screen);
        const QRect area = screen->availableGeometry();
        prompt->setPosition(area.center() - QPoint(prompt->width() / 2, prompt->height() / 2));
    }
    if (mainWindow != nullptr) {
        mainWindow->alert(0); // taskbar flash (Windows/Linux) / dock bounce (macOS)
    }
}

} // namespace hwio::gui
