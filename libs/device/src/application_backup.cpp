#include "hwio/device/application_backup.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/device/profile.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/intel_hex.h"
#include "owner_only.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFileDevice>
#include <QIODevice>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimeZone>
#include <QtContainerFwd>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::device {

namespace fs = std::filesystem;

namespace {

std::string safeSerial(std::string_view serial) {
    if (serial.empty()) {
        return "unknown";
    }
    std::string safe;
    for (const char c : serial.substr(0, 32)) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '-' || c == '_';
        safe.push_back(allowed ? c : '_');
    }
    return safe;
}

Error writeFailed(const QString& path, const QString& reason) {
    Error error =
        makeError(ErrorCode::IoWriteFailed,
                  std::format("application backup {}: {}", toStdString(path), toStdString(reason)));
    return error;
}

// Keeps the newest keepFiles backups of `directory`: ordered by the UTC stamp at the end of the
// name ("<serial>-yyyyMMdd-HHmmss.hex"), oldest first.
void removeOldBackups(QDir& directory) {
    QStringList files = directory.entryList({QStringLiteral("*.hex")}, QDir::Files, QDir::Name);
    if (std::cmp_less_equal(files.size(), ApplicationBackup::keepFiles)) {
        return;
    }
    constexpr qsizetype stampLength = 15; // yyyyMMdd-HHmmss
    const auto stampOf = [](const QString& name) {
        const QString stem = name.chopped(4); // ".hex"
        return stem.size() >= stampLength ? stem.right(stampLength) : stem;
    };
    std::ranges::stable_sort(
        files, [&stampOf](const QString& a, const QString& b) { return stampOf(a) < stampOf(b); });
    const auto excess = static_cast<qsizetype>(static_cast<std::size_t>(files.size()) -
                                               ApplicationBackup::keepFiles);
    for (qsizetype i = 0; i < excess; ++i) {
        if (!directory.remove(files.at(i))) {
            qCWarning(log::lcDevice).noquote() << "Could not remove the old backup" << files.at(i);
        }
    }
}

} // namespace

Result<firmware::AddressRange> ApplicationBackup::rangeOf(const TargetProfile& target) {
    if (target.flash.empty() || target.flash.size() > maxBytes) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                         std::format("{} flash: {} bytes (backup limit {})",
                                                     target.id, target.flash.size(), maxBytes)));
    }
    return target.flash;
}

std::vector<std::byte> ApplicationBackup::trimmed(std::vector<std::byte> data) {
    std::size_t end = data.size();
    while (end > 0) {
        const std::size_t pageStart = ((end - 1) / pageBytes) * pageBytes;
        const bool erased = std::all_of(data.begin() + static_cast<std::ptrdiff_t>(pageStart),
                                        data.begin() + static_cast<std::ptrdiff_t>(end),
                                        [](std::byte b) { return b == std::byte{0xFF}; });
        if (!erased) {
            break;
        }
        end = pageStart;
    }
    data.resize(end);
    return data;
}

Result<fs::path> ApplicationBackup::save(const fs::path& directory, std::string_view serial,
                                         std::chrono::system_clock::time_point now,
                                         std::uint32_t base, std::span<const std::byte> data) {
    if (data.empty()) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "application backup: nothing to save"));
    }
    std::vector<firmware::Segment> segments;
    segments.push_back(firmware::Segment{.address = base,
                                         .data = std::vector<std::byte>(data.begin(), data.end())});
    auto image = firmware::FirmwareImage::fromSegments(std::move(segments));
    if (!image) {
        return std::unexpected(std::move(image).error());
    }
    const QString dirPath = QString::fromStdU16String(directory.u16string());
    QDir dir(dirPath);
    if (!dir.mkpath(QStringLiteral("."))) {
        return std::unexpected(writeFailed(dirPath, QStringLiteral("cannot create the directory")));
    }
    const QString stamp =
        QDateTime::fromMSecsSinceEpoch(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(),
            QTimeZone::UTC)
            .toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString name =
        QStringLiteral("%1-%2.hex").arg(QString::fromStdString(safeSerial(serial)), stamp);
    const QString path = dir.filePath(name);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(writeFailed(path, file.errorString()));
    }
    detail::restrictToOwner(file);
    const std::string text = firmware::toIntelHex(*image);
    const QByteArray bytes = QByteArray::fromStdString(text);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return std::unexpected(writeFailed(path, file.errorString()));
    }
    qCInfo(log::lcDevice).noquote()
        << "Application backup written:" << path << QStringLiteral("(%1 bytes)").arg(data.size());
    removeOldBackups(dir);
    return fs::path(path.toStdU16String());
}

fs::path ApplicationBackup::defaultDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "backups";
}

} // namespace hwio::device
