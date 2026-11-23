#include "viewmodels/firmware_source_view_model.h"

#include "gui_log.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/application_backup.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/designated_firmware.h"
#include "hwio/firmware/firmware_cache.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/firmware/firmware_image.h"
#include "pages/firmware_services.h"
#include "viewmodels/error_info.h"
#include "viewmodels/recent_sources_model.h"
#include "workers/firmware_loader.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QObject>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimeZone>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hwio::gui {

namespace {

// Plan 0040 (ADR-0040 extended): where STM32 flash starts; nRF images start below it.
constexpr std::uint64_t stm32FlashStart = 0x08000000;
constexpr std::uint64_t stm32SystemEnd = 0x10000000; // the nRF UICR (0x10001000) is above

// The image starts in the STM32 flash / data EEPROM area (an nRF image starts at 0x0).
bool stm32Image(const firmware::FirmwareImage& image) {
    if (image.empty()) {
        return false;
    }
    const std::uint64_t start = image.segments().front().address;
    return start >= stm32FlashStart && start < stm32SystemEnd;
}

QString hexAddress(std::uint64_t address) {
    return QStringLiteral("0x") +
           QString::number(address, 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

QString sizeText(std::uint64_t bytes) {
    return QLocale().formattedDataSize(static_cast<qint64>(bytes), 0, QLocale::DataSizeSIFormat);
}

// "2.1 MB" (one decimal, as in "Downloading… 2.1 / 6.1 MB").
QString megabytes(std::uint64_t bytes) {
    return QLocale().toString(static_cast<double>(bytes) / 1e6, 'f', 1);
}

// The version in a Nordic modem firmware file name ("mfw_nrf91x1_2.0.2.zip" -> "2.0.2"), "" when
// the name has none.
QString modemVersion(const QString& fileName) {
    static const QRegularExpression pattern(QStringLiteral("_(\\d+\\.\\d+\\.\\d+)\\.zip$"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(fileName);
    return match.hasMatch() ? match.captured(1) : QString();
}

} // namespace

FirmwareSourceViewModel::FirmwareSourceViewModel(
    FirmwareServices& services, std::function<const device::DeviceProfile*()> profile,
    QObject* parent)
    : QObject(parent), m_services(services), m_profile(std::move(profile)),
      // Qt parent ownership: this object.
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      m_recent(new RecentSourcesModel(services.settings(), this)) {
    m_recent->setKind(kindId(m_kind));
    connect(&m_services.loader(), &FirmwareLoader::fileLoaded, this,
            &FirmwareSourceViewModel::onFileLoaded);
    connect(&m_services, &FirmwareServices::languageChanged, this,
            &FirmwareSourceViewModel::retranslate);
}

FirmwareSourceViewModel::~FirmwareSourceViewModel() {
    // The downloads first, while every member still exists: aborting them must not reach this
    // object.
    if (m_download) {
        m_download->disconnect(this);
        m_download.reset();
    }
    for (auto& item : m_designated) {
        if (item.second.download) {
            item.second.download->disconnect(this);
            item.second.download.reset();
        }
    }
    if (m_lookup) {
        m_lookup->disconnect(this);
        m_lookup.reset();
    }
    if (m_nameLookup) {
        m_nameLookup->disconnect(this);
        m_nameLookup->cancel();
        m_nameLookup.reset();
    }
}

QString FirmwareSourceViewModel::targetIdFor(Kind kind) {
    switch (kind) {
    case AppFirmware:
        return QStringLiteral("app");
    case LteFirmware:
        return QStringLiteral("lte");
    case LrwFirmware:
        return QStringLiteral("lrw");
    case Stm32Firmware:
        break;
    }
    return {};
}

QString FirmwareSourceViewModel::kindId(Kind kind) {
    switch (kind) {
    case AppFirmware:
        return QStringLiteral("app");
    case LteFirmware:
        return QStringLiteral("lte");
    case Stm32Firmware:
        return QStringLiteral("stm32");
    case LrwFirmware:
        return QStringLiteral("lrw");
    }
    return {};
}

QString FirmwareSourceViewModel::targetId() const {
    return m_kind == Stm32Firmware ? m_stm32Target : targetIdFor(m_kind);
}

void FirmwareSourceViewModel::setStm32TargetId(const QString& targetId) {
    if (targetId == m_stm32Target) {
        return;
    }
    m_stm32Target = targetId;
    const device::TargetProfile* chip = target();
    m_binAddress = hexAddress(chip != nullptr && m_kind == Stm32Firmware
                                  ? static_cast<std::uint32_t>(chip->flash.start)
                                  : 0x08000000U);
    binAddressChanged(); // signal
    sourceChanged();     // signal
}

std::optional<std::uint32_t> FirmwareSourceViewModel::binStart() const {
    bool ok = false;
    const QString text = m_binAddress.trimmed();
    const qulonglong value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                                 ? text.mid(2).toULongLong(&ok, 16)
                                 : text.toULongLong(&ok, 10);
    if (!ok || value > 0xFFFFFFFFULL || value % 4 != 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

void FirmwareSourceViewModel::setBinAddress(const QString& text) {
    if (text == m_binAddress) {
        return;
    }
    m_binAddress = text;
    binAddressChanged(); // signal
    sourceChanged();     // signal: the summary shows the address
}

QString FirmwareSourceViewModel::binAddressError() const {
    if (binStart()) {
        return {};
    }
    return tr("Enter an address such as 0x08000000 (a multiple of 4).");
}

bool FirmwareSourceViewModel::binAddressVisible() const {
    return (m_kind == Stm32Firmware || m_kind == LrwFirmware) && m_loaded &&
           m_loaded->sourceClass == SourceClass::Binary;
}

std::shared_ptr<const firmware::FirmwareImage> FirmwareSourceViewModel::flashImage() const {
    const LoadedSource* source = readySource();
    if (source == nullptr || !source->image) {
        return nullptr;
    }
    if (source->sourceClass != SourceClass::Binary) {
        return source->image;
    }
    const std::optional<std::uint32_t> start = binStart();
    if (!start || source->image->segments().empty()) {
        return nullptr;
    }
    auto placed =
        firmware::FirmwareImage::fromBinary(source->image->segments().front().data, *start);
    return placed ? std::make_shared<const firmware::FirmwareImage>(std::move(*placed)) : nullptr;
}

bool FirmwareSourceViewModel::hasEeprom() const {
    const device::TargetProfile* chip = target();
    if ((m_kind != Stm32Firmware && m_kind != LrwFirmware) || chip == nullptr || !chip->stm32) {
        return false;
    }
    const auto image = flashImage();
    return image && image->intersects(chip->dataEeprom.value_or(chip->stm32->dataEepromArea));
}

bool FirmwareSourceViewModel::binaryElsewhere() const {
    return m_loaded && m_loaded->sourceClass == SourceClass::Binary && m_kind != Stm32Firmware &&
           m_kind != LrwFirmware;
}

void FirmwareSourceViewModel::setKind(Kind kind) {
    if (kind == m_kind) {
        return;
    }
    const QString designatedBefore = designatedKey();
    m_kind = kind;
    m_recent->setKind(kindId(kind));
    // Plan 0040 (B4): the other card's entry is kept (prepared firmware, running download).
    if (forgetDesignatedOfAnotherProfile() || designatedKey() != designatedBefore) {
        designatedChanged(); // signal: the entry of the new kind
    }
    refreshBackups(); // ADR-0381: application kind only
    if (m_kind != AppFirmware) {
        // 0021 (ADR-0174): no catalog ID on the LTE card.
        cancelDownload();
        cancelLookup();
    }
    if (m_catalogIdError.size() > 0) {
        m_catalogIdError.clear();
        catalogIdErrorChanged(); // signal
    }
    kindChanged(); // signal
    if (m_loaded && (m_state == Ready || m_state == Invalid) && !m_error) {
        // Re-evaluated only: Recent gets an entry on an explicit load or download (review F7).
        setState(fits() ? Ready : Invalid);
    }
    sourceChanged(); // signal: hint, UICR of the other target, modem note
}

const device::TargetProfile* FirmwareSourceViewModel::target() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return profile != nullptr ? device::findTarget(*profile, toStdString(targetId())) : nullptr;
}

const device::TargetProfile* FirmwareSourceViewModel::designatedTarget() const {
    if (m_kind != LteFirmware && m_kind != LrwFirmware) {
        return nullptr;
    }
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return profile != nullptr ? device::findTarget(*profile, toStdString(targetIdFor(m_kind)))
                              : nullptr;
}

bool FirmwareSourceViewModel::lrwAvailable() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return profile != nullptr && device::hasConsole(*profile) &&
           device::findTarget(*profile, toStdString(targetIdFor(LrwFirmware))) != nullptr;
}

int FirmwareSourceViewModel::sourceState() const {
    if (!designatedActive()) {
        return m_state;
    }
    switch (currentDesignatedState()) {
    case DesignatedIdle:
    case DesignatedLoading: // plan 0040 (B4): "… is not downloaded yet" for that moment
        return None;
    case DesignatedDownloading:
        return Downloading;
    case DesignatedReady:
        return Ready;
    case DesignatedFailed:
        return Invalid;
    }
    return None;
}

QString FirmwareSourceViewModel::sourceName() const {
    if (designatedActive()) {
        const device::TargetProfile* designated = designatedTarget();
        return designated != nullptr && designated->designatedFirmware
                   ? QStringLiteral("%1 %2").arg(toQString(designated->designatedFirmware->name),
                                                 toQString(designated->designatedFirmware->version))
                   : QString();
    }
    switch (m_state) {
    case Loading:
    case Downloading:
        return m_pendingName;
    case Ready:
    case Invalid:
        return m_loaded ? m_loadedName : m_pendingName;
    case None:
        break;
    }
    return {};
}

QString FirmwareSourceViewModel::sourceSummary() const {
    if (designatedActive()) {
        if (currentDesignatedState() != DesignatedReady) {
            return {};
        }
        return m_kind == LrwFirmware
                   ? tr("%1 · LoRaWAN module firmware").arg(sourceName())
                   : tr("%1 · LTE bundle (application + modem firmware)").arg(sourceName());
    }
    if (m_state == Loading) {
        return tr("Loading %1…").arg(m_pendingName);
    }
    if (m_state == Downloading) {
        return tr("Downloading %1…").arg(m_pendingName);
    }
    if (!m_loaded || (m_state != Ready && m_state != Invalid)) {
        return {};
    }
    const LoadedSource& loaded = *m_loaded;
    switch (loaded.sourceClass) {
    case SourceClass::IntelHex: {
        QString range;
        if (loaded.image && !loaded.image->empty()) {
            const auto& segments = loaded.image->segments();
            range = QStringLiteral("%1–%2").arg(hexAddress(segments.front().address),
                                                hexAddress(segments.back().range().end));
        }
        if (m_kind == LteFirmware) {
            // 0021: summary per classification (ADR-0040).
            return tr("%1 · LTE application · %2 · %3")
                .arg(m_loadedName, sizeText(loaded.fileBytes), range);
        }
        return tr("%1 · Intel HEX · %2 · %3").arg(m_loadedName, sizeText(loaded.fileBytes), range);
    }
    case SourceClass::LteBundle:
        return tr("%1 · LTE bundle (application + modem firmware) · %2")
            .arg(m_loadedName, sizeText(loaded.fileBytes));
    case SourceClass::Binary: {
        const std::uint64_t bytes =
            loaded.image && !loaded.image->empty() ? loaded.image->totalBytes() : 0;
        return binStart() ? tr("%1 · binary · %2 · from %3")
                                .arg(m_loadedName, sizeText(bytes), hexAddress(*binStart()))
                          : tr("%1 · binary · %2").arg(m_loadedName, sizeText(bytes));
    }
    case SourceClass::ModemZip: {
        const QString version = modemVersion(m_loadedName);
        return version.isEmpty() ? tr("%1 · Modem firmware (modem only) · %2")
                                       .arg(m_loadedName, sizeText(loaded.fileBytes))
                                 : tr("%1 · Modem firmware %2 (modem only) · %3")
                                       .arg(m_loadedName, version, sizeText(loaded.fileBytes));
    }
    }
    return {};
}

bool FirmwareSourceViewModel::hasUicr() const {
    const LoadedSource* source = readySource();
    const device::TargetProfile* targetProfile = target();
    // STM32 chips have no UICR (their data EEPROM: hasEeprom()).
    return source != nullptr && source->image && targetProfile != nullptr &&
           targetProfile->vendor == device::ChipVendor::Nordic &&
           source->image->intersects(targetProfile->uicr);
}

void FirmwareSourceViewModel::setAllowUicr(bool allow) {
    if (allow == m_allowUicr) {
        return;
    }
    m_allowUicr = allow;
    allowUicrChanged(); // signal
}

std::optional<FirmwareSourceViewModel::Kind> FirmwareSourceViewModel::mismatch() const {
    if (!m_loaded) {
        return std::nullopt;
    }
    // 0021 (ADR-0174): every LTE input (HEX, bundle, modem zip) fits the LTE kind; an LTE bundle
    // or modem zip on Application firmware is LTE firmware. A HEX file fits both nRF kinds
    // (ADR-0040 cannot tell the MCU of a HEX file); plan 0040: its address range tells an STM32
    // image (the LoRaWAN module) from an nRF image.
    switch (m_loaded->sourceClass) {
    case SourceClass::IntelHex:
        return hexMismatch();
    case SourceClass::Binary:
        // binaryElsewhere(); plan 0040: on a CHESTER the LoRaWAN module takes it.
        return binaryElsewhere() && lrwAvailable() ? std::optional<Kind>(LrwFirmware)
                                                   : std::nullopt;
    case SourceClass::LteBundle:
    case SourceClass::ModemZip:
        return m_kind == LteFirmware ? std::nullopt : std::optional<Kind>(LteFirmware);
    }
    return std::nullopt;
}

QString FirmwareSourceViewModel::kindHint() const {
    if (designatedActive() || m_state != Invalid || m_error || !m_loaded) {
        return {};
    }
    if (binaryElsewhere()) {
        return tr("A .bin file can only be flashed to an STM32 chip.");
    }
    const std::optional<Kind> other = mismatch();
    if (!other) {
        return {};
    }
    // Towards LTE firmware: the ADR-0040 classification cannot tell the nRF MCU of a HEX file, so
    // an nRF HEX fits both nRF kinds (user decision 2026-10-23: no "application firmware" hint).
    // Plan 0040: the address range tells the LoRaWAN module (STM32) from the nRF MCUs.
    switch (*other) {
    case LrwFirmware:
        return tr("This is LoRaWAN module firmware — switch?");
    case AppFirmware:
        return tr("This is not LoRaWAN module firmware — switch to Application firmware?");
    case LteFirmware:
    case Stm32Firmware:
        break;
    }
    return tr("This is LTE firmware — switch to LTE firmware?");
}

std::optional<FirmwareSourceViewModel::Kind> FirmwareSourceViewModel::hexMismatch() const {
    if (!m_loaded || !m_loaded->image || m_kind == Stm32Firmware) {
        return std::nullopt;
    }
    const bool stm32 = stm32Image(*m_loaded->image);
    if (m_kind == LrwFirmware) {
        return stm32 ? std::nullopt : std::optional<Kind>(AppFirmware);
    }
    return stm32 && lrwAvailable() ? std::optional<Kind>(LrwFirmware) : std::nullopt;
}

int FirmwareSourceViewModel::suggestedKind() const {
    if (designatedActive() || m_state != Invalid || m_error) {
        return -1;
    }
    const std::optional<Kind> other = mismatch();
    return other ? static_cast<int>(*other) : -1;
}

ErrorInfo FirmwareSourceViewModel::error() const {
    return m_error && !designatedActive() ? ErrorInfo(*m_error) : ErrorInfo();
}

bool FirmwareSourceViewModel::catalogAvailable() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    return m_kind == AppFirmware && (profile == nullptr || !profile->firmwareCatalogUrl.empty());
}

QUrl FirmwareSourceViewModel::docsUrl() const {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    if (profile == nullptr) {
        return {};
    }
    return {toQString(device::firmwareDocsUrlFor(*profile, toStdString(m_services.uiLanguage())))};
}

bool FirmwareSourceViewModel::modemNoteVisible() const {
    if (m_kind != LteFirmware) {
        return false;
    }
    if (designatedActive()) {
        return true; // the designated bundle always contains modem firmware
    }
    return m_loaded && (m_loaded->sourceClass == SourceClass::LteBundle ||
                        m_loaded->sourceClass == SourceClass::ModemZip);
}

QStringList FirmwareSourceViewModel::nameFilters() const {
    if (m_kind == Stm32Firmware || m_kind == LrwFirmware) {
        return {tr("STM32 firmware (*.hex *.bin)"), tr("All files (*)")};
    }
    if (m_kind == AppFirmware) {
        return {tr("Intel HEX files (*.hex)"), tr("All files (*)")};
    }
    return {tr("LTE firmware (*.hex *.zip)"), tr("All files (*)")};
}

QString FirmwareSourceViewModel::dropText() const {
    if (m_kind == Stm32Firmware || m_kind == LrwFirmware) {
        return tr("Drop a .hex or .bin file here");
    }
    if (m_kind == AppFirmware) {
        return tr("Drop a .hex file here");
    }
    return tr("Drop a .hex file, an LTE bundle .zip or a modem firmware .zip here");
}

// ---- 0021: the designated LTE firmware (ADR-0165) ----------------------------------------------

bool FirmwareSourceViewModel::designatedAvailable() const {
    const device::TargetProfile* designated = designatedTarget();
    return designated != nullptr && designated->designatedFirmware.has_value();
}

bool FirmwareSourceViewModel::designatedActive() const {
    return (m_kind == LteFirmware || m_kind == LrwFirmware) && m_lteSource == Designated &&
           designatedAvailable();
}

QString FirmwareSourceViewModel::designatedKey() const {
    return m_kind == LteFirmware || m_kind == LrwFirmware ? targetIdFor(m_kind) : QString();
}

const FirmwareSourceViewModel::DesignatedEntry* FirmwareSourceViewModel::currentDesignated() const {
    const QString key = designatedKey();
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    if (key.isEmpty() || profile == nullptr || profile != m_designatedProfile) {
        return nullptr;
    }
    const auto found = m_designated.find(key);
    return found != m_designated.end() ? &found->second : nullptr;
}

FirmwareSourceViewModel::DesignatedState FirmwareSourceViewModel::currentDesignatedState() const {
    const DesignatedEntry* entry = currentDesignated();
    return entry != nullptr ? entry->state : DesignatedIdle;
}

int FirmwareSourceViewModel::designatedState() const { return currentDesignatedState(); }

bool FirmwareSourceViewModel::forgetDesignatedOfAnotherProfile() {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    if (profile == m_designatedProfile) {
        return false;
    }
    m_designatedProfile = profile;
    if (m_designated.empty()) {
        return false;
    }
    for (auto& item : m_designated) {
        if (item.second.download) {
            item.second.download->disconnect(this);
            item.second.download->cancel();
        }
    }
    m_designated.clear();
    return true;
}

void FirmwareSourceViewModel::setLteSource(int source) {
    if ((source != Designated && source != OwnFile) || source == m_lteSource) {
        return;
    }
    m_lteSource = static_cast<LteSource>(source);
    lteSourceChanged(); // signal
    sourceChanged();    // signal: the source of the LTE kind changed
}

QString FirmwareSourceViewModel::designatedTitle() const {
    const device::TargetProfile* designated = designatedTarget();
    if (designated == nullptr || !designated->designatedFirmware) {
        return {};
    }
    return tr("%1 %2 (recommended)")
        .arg(toQString(designated->designatedFirmware->name),
             toQString(designated->designatedFirmware->version));
}

QString FirmwareSourceViewModel::designatedStatus() const {
    const DesignatedEntry* entry = currentDesignated();
    switch (currentDesignatedState()) {
    case DesignatedDownloading:
        return entry != nullptr && entry->total > 0
                   ? tr("Downloading… %1 / %2 MB")
                         .arg(megabytes(entry->done), megabytes(entry->total))
                   : tr("Downloading…");
    case DesignatedReady:
        return tr("Ready");
    case DesignatedIdle:
    case DesignatedFailed:
    case DesignatedLoading: // plan 0040 (B4): a cache hit says nothing
        break;
    }
    return {};
}

qreal FirmwareSourceViewModel::designatedProgress() const {
    const DesignatedEntry* entry = currentDesignated();
    return entry != nullptr && entry->total > 0
               ? static_cast<qreal>(entry->done) / static_cast<qreal>(entry->total)
               : -1;
}

ErrorInfo FirmwareSourceViewModel::designatedError() const {
    const DesignatedEntry* entry = currentDesignated();
    return entry != nullptr && entry->error ? ErrorInfo(*entry->error) : ErrorInfo();
}

void FirmwareSourceViewModel::prepareDesignated() {
    if (forgetDesignatedOfAnotherProfile()) {
        designatedChanged(); // signal
        sourceChanged();     // signal
    }
    if (currentDesignatedState() == DesignatedIdle && designatedAvailable()) {
        startDesignated();
    }
}

void FirmwareSourceViewModel::retryDesignated() {
    if (forgetDesignatedOfAnotherProfile()) {
        designatedChanged(); // signal
        sourceChanged();     // signal
    }
    if (const DesignatedState state = currentDesignatedState();
        state == DesignatedFailed || state == DesignatedIdle) {
        startDesignated();
    }
}

void FirmwareSourceViewModel::cancelDesignated() {
    const bool forgotten = forgetDesignatedOfAnotherProfile();
    const auto found = m_designated.find(designatedKey());
    if (found == m_designated.end() || !found->second.download) {
        if (forgotten) {
            designatedChanged(); // signal
            sourceChanged();     // signal
        }
        return;
    }
    DesignatedEntry& entry = found->second;
    const std::unique_ptr<firmware::DesignatedOperation> download = std::move(entry.download);
    download->disconnect(this);
    download->cancel();
    entry.state = DesignatedIdle;
    designatedChanged(); // signal
    sourceChanged();     // signal
}

void FirmwareSourceViewModel::startDesignated() {
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    const device::TargetProfile* designated = designatedTarget();
    firmware::FirmwareCatalog* catalog =
        profile != nullptr ? m_services.catalog(*profile) : nullptr;
    const QString key = designatedKey();
    if (designated == nullptr || !designated->designatedFirmware || catalog == nullptr ||
        key.isEmpty()) {
        return;
    }
    // The callers (prepareDesignated, retryDesignated) dropped the entries of another profile.
    // At most one entry per target with designated firmware (see m_designated).
    DesignatedEntry& entry = m_designated[key];
    entry.error.reset();
    entry.loaded.reset();
    entry.done = 0;
    entry.total = 0;
    entry.download = catalog->downloadDesignated(
        *designated->designatedFirmware, {}, [this, key](std::uint64_t done, std::uint64_t total) {
            const auto found = m_designated.find(key);
            if (found == m_designated.end()) {
                return;
            }
            found->second.done = done;
            found->second.total = total;
            if (key == designatedKey()) {
                designatedChanged(); // signal
            }
        });
    connect(entry.download.get(), &firmware::DesignatedOperation::finished, this,
            [this, key] { onDesignatedFinished(key); });
    // Plan 0040 (B4): a verified cache file is only handed over - no "Downloading…".
    entry.state = entry.download->fromCache() ? DesignatedLoading : DesignatedDownloading;
    designatedChanged(); // signal
    sourceChanged();     // signal
}

void FirmwareSourceViewModel::onDesignatedFinished(const QString& key) {
    const auto found = m_designated.find(key);
    if (found == m_designated.end()) {
        return;
    }
    DesignatedEntry& entry = found->second;
    if (!entry.download || !entry.download->result()) {
        return;
    }
    Result<firmware::DesignatedDownload> result = *entry.download->result();
    // Deleted later: this runs inside the operation's own signal (Qt takes over the ownership).
    entry.download.release()->deleteLater(); // NOLINT(bugprone-unused-return-value)
    const bool current = key == designatedKey();
    if (!result) {
        Error error = std::move(result).error();
        if (error.code == ErrorCode::Cancelled) {
            entry.state = DesignatedIdle;
        } else {
            qCInfo(lcGui).noquote()
                << "Designated firmware:" << toQString(idOf(error.code)) << toQString(error.detail);
            if (error.code == ErrorCode::FirmwareChecksumMismatch) {
                error.message = tr("The downloaded file is damaged. Try again.").toStdString();
            } else if (error.code == ErrorCode::NetworkDownloadFailed) {
                error.suggestion += " " + tr("You can also choose your own file.").toStdString();
            }
            entry.error = std::move(error);
            entry.state = DesignatedFailed;
        }
        if (current) {
            designatedChanged(); // signal
            sourceChanged();     // signal
        }
        return;
    }
    for (const Error& warning : result->warnings) {
        qCInfo(lcGui).noquote() << "Designated firmware:" << toQString(warning.message)
                                << toQString(warning.detail);
    }
    if (result->bundle) {
        entry.loaded = LoadedSource{.sourceClass = SourceClass::LteBundle,
                                    .path = {},
                                    .fileBytes = result->fileBytes,
                                    .image = std::shared_ptr<const firmware::FirmwareImage>(
                                        result->bundle, &result->bundle->application),
                                    .bundle = result->bundle};
    } else {
        // Plan 0040: an Intel HEX (FirmwareKind::AppHex, the LoRaWAN module's lora-modem).
        entry.loaded = LoadedSource{.sourceClass = SourceClass::IntelHex,
                                    .path = {},
                                    .fileBytes = result->fileBytes,
                                    .image = result->image,
                                    .bundle = nullptr};
    }
    entry.state = DesignatedReady;
    if (current) {
        designatedChanged(); // signal
        sourceChanged();     // signal
    }
}

// ---- 0021: the catalog page of an ID (ADR-0166) -----------------------------------------------

QVariantMap FirmwareSourceViewModel::catalogInfo() const {
    QVariantMap info{{QStringLiteral("lookupState"), m_lookupState},
                     {QStringLiteral("text"), catalogInfoText()}};
    if (m_info) {
        info.insert(QStringLiteral("name"), toQString(m_info->name));
        info.insert(QStringLiteral("version"), toQString(m_info->version));
        info.insert(QStringLiteral("uploaded"), toQString(m_info->uploaded).left(10));
    }
    return info;
}

QString FirmwareSourceViewModel::catalogInfoText() const {
    if (m_lookupState == QStringLiteral("loading")) {
        return tr("Looking up the firmware…");
    }
    if (m_lookupState == QStringLiteral("unknown")) {
        return tr("Unknown firmware — it can still be flashed.");
    }
    if (m_lookupState == QStringLiteral("found") && m_info) {
        const QString nameVersion =
            QStringLiteral("%1 %2").arg(toQString(m_info->name), toQString(m_info->version));
        return m_info->uploaded.empty()
                   ? nameVersion
                   : tr("%1 · uploaded %2").arg(nameVersion, toQString(m_info->uploaded).left(10));
    }
    return {};
}

void FirmwareSourceViewModel::startLookup(const QString& id) {
    if (id == m_lookupId && m_lookupState != QStringLiteral("none")) {
        return; // this ID is known or being looked up
    }
    cancelLookup();
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    firmware::FirmwareCatalog* catalog =
        profile != nullptr ? m_services.catalog(*profile) : nullptr;
    auto parsed = firmware::FirmwareId::parse(toStdString(id));
    if (catalog == nullptr || !parsed) {
        return;
    }
    m_lookupId = id;
    m_lookup = catalog->lookup(*parsed);
    connect(m_lookup.get(), &firmware::LookupOperation::finished, this,
            &FirmwareSourceViewModel::onLookupFinished);
    m_lookupState = QStringLiteral("loading");
    catalogInfoChanged(); // signal
}

void FirmwareSourceViewModel::onLookupFinished() {
    if (!m_lookup || !m_lookup->result()) {
        return;
    }
    const Result<firmware::CatalogInfo> result = *m_lookup->result();
    m_lookup.release()->deleteLater(); // NOLINT(bugprone-unused-return-value)
    if (result) {
        m_info = std::make_unique<firmware::CatalogInfo>(*result);
        m_lookupState = QStringLiteral("found");
        // ADR-0385: a Recent entry of this ID gets its name.
        m_recent->setName(RecentSourcesModel::catalogPrefix + m_lookupId, toQString(m_info->name),
                          toQString(m_info->version));
    } else if (result.error().code == ErrorCode::Cancelled) {
        m_lookupState = QStringLiteral("none");
    } else {
        // Not an error (plan 0021 § Errors): the HEX download is authoritative.
        qCInfo(lcGui).noquote() << "Catalog page:" << toQString(idOf(result.error().code))
                                << toQString(result.error().detail);
        m_lookupState = QStringLiteral("unknown");
    }
    catalogInfoChanged(); // signal
    if (m_downloadWaitsForLookup && m_lookupId == m_downloadId) {
        startCatalogDownload(); // review F1: with the page's hash (or without, when unknown)
    }
}

void FirmwareSourceViewModel::cancelLookup() {
    if (m_downloadWaitsForLookup) {
        // Another ID replaced the one being looked up: its pending download never starts
        // unverified (review F1).
        m_downloadWaitsForLookup = false;
        restoreState();
    }
    if (m_lookup) {
        const std::unique_ptr<firmware::LookupOperation> lookup = std::move(m_lookup);
        lookup->disconnect(this);
        lookup->cancel();
    }
    const bool changed = m_lookupState != QStringLiteral("none") || m_info;
    m_info.reset();
    m_lookupId.clear();
    m_lookupState = QStringLiteral("none");
    if (changed) {
        catalogInfoChanged(); // signal
    }
}

void FirmwareSourceViewModel::loadFile(const QUrl& url) {
    if (!url.isLocalFile()) {
        setError(makeError(ErrorCode::UsageInvalidArgument,
                           "not a local file: " + toStdString(url.toString())));
        return;
    }
    loadPath(url.toLocalFile());
}

void FirmwareSourceViewModel::loadPath(const QString& path) {
    if (m_kind == LteFirmware || m_kind == LrwFirmware) {
        setLteSource(OwnFile); // 0021: a file (drop, Browse…, Recent) is the own-file source
    }
    cancelDownload();
    m_pendingName = QFileInfo(path).fileName();
    m_generation = m_services.loader().loadFile(this, std::filesystem::path(path.toStdU16String()));
    setState(Loading);
}

bool FirmwareSourceViewModel::checkCatalogId(const QString& text) {
    const QString trimmed = text.trimmed();
    QString message;
    const auto parsed = firmware::FirmwareId::parse(toStdString(trimmed));
    if (!trimmed.isEmpty() && !parsed) {
        message = tr("The ID has 32 characters 0–9 a–f.");
    }
    if (message != m_catalogIdError) {
        m_catalogIdError = message;
        catalogIdErrorChanged(); // signal
    }
    // 0021 (ADR-0166): a complete ID is looked up at once; anything else clears the info.
    if (parsed && catalogAvailable()) {
        startLookup(toQString(parsed->value()));
    } else {
        cancelLookup();
    }
    return message.isEmpty();
}

void FirmwareSourceViewModel::loadCatalogId(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || !checkCatalogId(trimmed) || !catalogAvailable()) {
        if (trimmed.isEmpty()) {
            m_catalogIdError = tr("The ID has 32 characters 0–9 a–f.");
            catalogIdErrorChanged(); // signal
        }
        return;
    }
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    firmware::FirmwareCatalog* catalog =
        profile != nullptr ? m_services.catalog(*profile) : nullptr;
    auto id = firmware::FirmwareId::parse(toStdString(trimmed));
    if (catalog == nullptr || !id) {
        setError(makeError(ErrorCode::UsageInvalidArgument, "no firmware catalog for this device"));
        return;
    }
    cancelDownload();
    m_generation = 0; // a pending file load is stale now
    m_downloadId = toQString(id->value());
    m_pendingName = tr("Catalog ID %1").arg(m_downloadId);
    m_downloadProgress = -1;
    startLookup(m_downloadId); // e.g. from the recent list (no-op when known)
    setState(Downloading);
    downloadProgressChanged(); // signal
    if (m_lookupState == QStringLiteral("loading") && m_lookupId == m_downloadId) {
        // 0021 review F1: the page's SHA-256 must verify the download (also a cached HEX), so the
        // download waits for the running lookup (capped by CatalogOptions::lookupTimeout).
        m_downloadWaitsForLookup = true;
        return;
    }
    startCatalogDownload();
}

void FirmwareSourceViewModel::startCatalogDownload() {
    m_downloadWaitsForLookup = false;
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    firmware::FirmwareCatalog* catalog =
        profile != nullptr ? m_services.catalog(*profile) : nullptr;
    auto id = firmware::FirmwareId::parse(toStdString(m_downloadId));
    if (catalog == nullptr || !id) {
        setError(makeError(ErrorCode::UsageInvalidArgument, "no firmware catalog for this device"));
        return;
    }
    // 0021 (ADR-0166): the catalog page's SHA-256 verifies the download when it is known; an
    // unreadable page (unknown firmware) does not stop it.
    const std::optional<firmware::Sha256> expected =
        m_info && m_lookupId == m_downloadId ? m_info->sha256 : std::nullopt;
    m_download = catalog->download(
        *id, firmware::DownloadOptions{.noCache = false, .expectedSha256 = expected},
        [this](std::uint64_t done, std::uint64_t total) {
            m_downloadProgress =
                total > 0 ? static_cast<qreal>(done) / static_cast<qreal>(total) : -1;
            downloadProgressChanged(); // signal
        });
    connect(m_download.get(), &firmware::CatalogOperation::finished, this,
            &FirmwareSourceViewModel::onDownloadFinished);
}

void FirmwareSourceViewModel::onDownloadFinished() {
    if (!m_download || !m_download->result()) {
        return;
    }
    Result<firmware::CachedFirmware> result = *m_download->result();
    // Deleted later: this runs inside the operation's own signal (Qt takes over the ownership).
    m_download.release()->deleteLater(); // NOLINT(bugprone-unused-return-value)
    const QString id = m_downloadId;
    if (!result) {
        if (result.error().code == ErrorCode::Cancelled) {
            restoreState();
            return;
        }
        setError(std::move(result).error());
        return;
    }
    for (const Error& warning : result->warnings) {
        qCInfo(lcGui).noquote() << "Firmware download:" << toQString(warning.message)
                                << toQString(warning.detail);
    }
    m_pendingName = catalogName(id);
    setLoaded(LoadedSource{.sourceClass = SourceClass::IntelHex,
                           .path = {},
                           .fileBytes = result->fileBytes,
                           .image = result->image,
                           .bundle = nullptr},
              RecentSourcesModel::catalogPrefix + id);
}

void FirmwareSourceViewModel::onFileLoaded(quint64 generation, const Result<LoadedSource>& result) {
    if (generation != m_generation || m_generation == 0) {
        return; // stale or another requester's
    }
    m_generation = 0;
    if (!result) {
        setError(result.error());
        return;
    }
    const LoadedSource& loaded = *result;
    setLoaded(loaded, QString::fromStdU16String(loaded.path.u16string()));
}

void FirmwareSourceViewModel::setLoaded(LoadedSource loaded, QString recentSource) {
    m_loaded = std::move(loaded);
    m_loadedName = m_pendingName;
    m_recentSource = std::move(recentSource);
    m_error.reset();
    m_allowUicr = false; // never carried over to another image (ADR-0036)
    allowUicrChanged();  // signal
    const bool fitting = fits();
    setState(fitting ? Ready : Invalid);
    if (fitting) {
        // ADR-0385: a catalog entry with the name and version of its page.
        const bool named =
            m_info && m_recentSource == RecentSourcesModel::catalogPrefix + m_lookupId;
        m_recent->add(kindId(m_kind), m_recentSource, named ? toQString(m_info->name) : QString(),
                      named ? toQString(m_info->version) : QString());
    }
}

// ---- plan 0051 (ADR-0381): the application copies ----------------------------------------------

void FirmwareSourceViewModel::setDeviceSerial(const QString& serial) {
    if (serial == m_deviceSerial) {
        return;
    }
    m_deviceSerial = serial;
    refreshBackups();
}

void FirmwareSourceViewModel::setBackupDirectory(const std::filesystem::path& directory) {
    m_backupDirectory = directory;
    refreshBackups();
}

void FirmwareSourceViewModel::refreshBackups() {
    std::vector<device::ApplicationBackup::Entry> found;
    if (m_kind == AppFirmware) {
        const std::filesystem::path directory = m_backupDirectory.empty()
                                                    ? device::ApplicationBackup::defaultDirectory()
                                                    : m_backupDirectory;
        found = device::ApplicationBackup::list(
            directory, m_deviceSerial.isEmpty()
                           ? std::nullopt
                           : std::optional<std::string>(toStdString(m_deviceSerial)));
    }
    const bool same = std::ranges::equal(found, m_backups, [](const auto& a, const auto& b) {
        return a.path == b.path && a.time == b.time;
    });
    m_backups = std::move(found);
    if (!same) {
        backupsChanged(); // signal
    }
}

QVariantList FirmwareSourceViewModel::backups() const {
    QVariantList list;
    for (const device::ApplicationBackup::Entry& entry : m_backups) {
        const QDateTime time = QDateTime::fromMSecsSinceEpoch(
            std::chrono::duration_cast<std::chrono::milliseconds>(entry.time.time_since_epoch())
                .count(),
            QTimeZone::UTC);
        const QString when = QLocale().toString(time.toLocalTime(), QLocale::ShortFormat);
        list.append(QVariantMap{
            {QStringLiteral("text"),
             m_deviceSerial.isEmpty()
                 ? tr("Application copy of %1 from %2").arg(toQString(entry.serial), when)
                 : tr("Application copy from %1").arg(when)},
            {QStringLiteral("path"),
             QDir::toNativeSeparators(QString::fromStdU16String(entry.path.u16string()))}});
    }
    return list;
}

QUrl FirmwareSourceViewModel::backupsFolder() const {
    if (m_backups.empty()) {
        return {};
    }
    return QUrl::fromLocalFile(
        QString::fromStdU16String(m_backups.front().path.parent_path().u16string()));
}

void FirmwareSourceViewModel::loadBackup(int index) {
    if (index < 0 || std::cmp_greater_equal(index, m_backups.size())) {
        return;
    }
    loadPath(
        QString::fromStdU16String(m_backups.at(static_cast<std::size_t>(index)).path.u16string()));
}

void FirmwareSourceViewModel::openBackupsFolder() {
    const std::filesystem::path directory = m_backupDirectory.empty()
                                                ? device::ApplicationBackup::defaultDirectory()
                                                : m_backupDirectory;
    const QString path = QString::fromStdU16String(directory.u16string());
    if (!QDir().mkpath(path)) {
        qCWarning(lcGui).noquote() << "Backups folder not created:" << path;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        qCWarning(lcGui).noquote() << "Backups folder not opened:" << path;
    }
}

void FirmwareSourceViewModel::resolveRecentNames() {
    if (!m_nameLookup) {
        lookUpNextRecentName();
    }
}

void FirmwareSourceViewModel::lookUpNextRecentName() {
    if (m_namesTried.size() >= maxNameLookups) {
        return;
    }
    const device::DeviceProfile* profile = m_profile ? m_profile() : nullptr;
    firmware::FirmwareCatalog* catalog =
        profile != nullptr ? m_services.catalog(*profile) : nullptr;
    if (catalog == nullptr) {
        return;
    }
    for (const QString& id : m_recent->unnamedCatalogIds()) {
        if (m_namesTried.contains(id)) {
            continue;
        }
        m_namesTried.append(id);
        auto parsed = firmware::FirmwareId::parse(toStdString(id));
        if (!parsed) {
            continue;
        }
        m_nameLookupId = id;
        m_nameLookup = catalog->lookup(*parsed);
        connect(m_nameLookup.get(), &firmware::LookupOperation::finished, this,
                &FirmwareSourceViewModel::onRecentNameFinished);
        return;
    }
}

void FirmwareSourceViewModel::onRecentNameFinished() {
    if (!m_nameLookup || !m_nameLookup->result()) {
        return;
    }
    const Result<firmware::CatalogInfo> result = *m_nameLookup->result();
    m_nameLookup.release()->deleteLater(); // NOLINT(bugprone-unused-return-value)
    if (result) {
        m_recent->setName(RecentSourcesModel::catalogPrefix + m_nameLookupId,
                          toQString(result->name), toQString(result->version));
    } else {
        // "Catalog ID %1" stays (not an error).
        qCInfo(lcGui).noquote() << "Recent name of" << m_nameLookupId
                                << "not found:" << toQString(idOf(result.error().code));
    }
    m_nameLookupId.clear();
    lookUpNextRecentName();
}

void FirmwareSourceViewModel::setError(Error error) {
    qCInfo(lcGui).noquote() << "Firmware source:" << toQString(idOf(error.code))
                            << toQString(error.detail);
    m_loaded.reset();
    m_recentSource.clear();
    m_error = std::move(error);
    setState(Invalid);
}

void FirmwareSourceViewModel::restoreState() {
    if (!m_loaded) {
        setState(None);
    } else {
        setState(fits() ? Ready : Invalid);
    }
}

void FirmwareSourceViewModel::setState(SourceState state) {
    m_state = state;
    sourceChanged(); // signal
}

void FirmwareSourceViewModel::loadRecent(int row) {
    const QString source = m_recent->sourceAt(row);
    if (source.isEmpty()) {
        return;
    }
    if (source.startsWith(RecentSourcesModel::catalogPrefix)) {
        loadCatalogId(source.mid(RecentSourcesModel::catalogPrefix.size()));
    } else {
        loadPath(source);
    }
}

void FirmwareSourceViewModel::removeRecent(int row) { m_recent->remove(row); }

void FirmwareSourceViewModel::clear() {
    cancelDownload();
    m_generation = 0;
    m_loaded.reset();
    m_recentSource.clear();
    m_error.reset();
    m_allowUicr = false;
    allowUicrChanged(); // signal
    setState(None);
}

void FirmwareSourceViewModel::cancelDownload() {
    if (m_downloadWaitsForLookup) {
        m_downloadWaitsForLookup = false;
        if (m_state == Downloading) {
            restoreState();
        }
        return;
    }
    if (!m_download) {
        return;
    }
    const std::unique_ptr<firmware::CatalogOperation> download = std::move(m_download);
    download->disconnect(this);
    download->cancel();
    if (m_state == Downloading) {
        restoreState();
    }
}

void FirmwareSourceViewModel::acceptSuggestedKind() {
    if (const int suggested = suggestedKind(); suggested >= 0) {
        if (suggested == LteFirmware || suggested == LrwFirmware) {
            setLteSource(OwnFile); // 0021: the file that caused the hint is the new card's file
        }
        kindSuggestionAccepted(suggested); // signal
    }
}

const LoadedSource* FirmwareSourceViewModel::readySource() const {
    if (designatedActive()) {
        const DesignatedEntry* entry = currentDesignated();
        return entry != nullptr && entry->state == DesignatedReady && entry->loaded
                   ? &*entry->loaded
                   : nullptr;
    }
    return m_state == Ready && m_loaded ? &*m_loaded : nullptr;
}

const LoadedSource* FirmwareSourceViewModel::loadedSource() const {
    if (designatedActive()) {
        const DesignatedEntry* entry = currentDesignated();
        return entry != nullptr && entry->loaded ? &*entry->loaded : nullptr;
    }
    return m_loaded ? &*m_loaded : nullptr;
}

QString FirmwareSourceViewModel::catalogName(const QString& id) const {
    // 0021: what the catalog page says, else the ID.
    if (m_info && m_lookupId == id) {
        return QStringLiteral("%1 %2").arg(toQString(m_info->name), toQString(m_info->version));
    }
    return tr("Catalog ID %1").arg(id);
}

void FirmwareSourceViewModel::retranslate() {
    // File names are not translated; "Catalog ID <id>" is.
    if (m_loaded && m_recentSource.startsWith(RecentSourcesModel::catalogPrefix)) {
        m_loadedName = catalogName(m_recentSource.mid(RecentSourcesModel::catalogPrefix.size()));
    }
    if (!m_catalogIdError.isEmpty()) {
        m_catalogIdError = tr("The ID has 32 characters 0–9 a–f.");
        catalogIdErrorChanged(); // signal
    }
    m_recent->retranslate();
    kindChanged();        // signal: filters, drop text, docs URL
    sourceChanged();      // signal
    catalogInfoChanged(); // signal
    designatedChanged();  // signal
}

} // namespace hwio::gui
