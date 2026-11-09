// Plan 0039 A4 (ADR-0280 D5): .bin input - FirmwareImage::fromBinary() and readFirmwareFile().

#include "hwio/core/error.h"
#include "hwio/firmware/firmware_file.h"
#include "hwio/firmware/firmware_image.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace {

using hwio::ErrorCode;
using hwio::firmware::FirmwareImage;

std::vector<std::byte> bytes(std::size_t size) {
    std::vector<std::byte> data(size);
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::byte>(i & 0xFFU);
    }
    return data;
}

TEST(Binary, PlacedAtTheStartAddress) {
    const auto image = FirmwareImage::fromBinary(bytes(1000), 0x08000000);
    ASSERT_TRUE(image) << image.error().detail;
    ASSERT_EQ(image->segments().size(), 1U);
    EXPECT_EQ(image->segments().front().address, 0x08000000U);
    EXPECT_EQ(image->totalBytes(), 1000U);
    EXPECT_FALSE(image->startAddress().has_value());
}

TEST(Binary, Bounds) {
    EXPECT_EQ(FirmwareImage::fromBinary({}, 0x08000000).error().code,
              ErrorCode::FirmwareInvalidImage);
    EXPECT_TRUE(FirmwareImage::fromBinary(bytes(hwio::firmware::maxBinaryBytes), 0x08000000));
    EXPECT_EQ(FirmwareImage::fromBinary(bytes(hwio::firmware::maxBinaryBytes + 1), 0x08000000)
                  .error()
                  .code,
              ErrorCode::FirmwareInvalidImage);
    // Past the end of the 32-bit address space.
    EXPECT_EQ(FirmwareImage::fromBinary(bytes(16), 0xFFFFFFF8).error().code,
              ErrorCode::UsageInvalidArgument);
}

TEST(Binary, StartAddressMustBeWordAligned) {
    EXPECT_EQ(FirmwareImage::fromBinary(bytes(16), 0x08000002).error().code,
              ErrorCode::UsageInvalidArgument);
    EXPECT_TRUE(FirmwareImage::fromBinary(bytes(16), 0x08000004));
}

class FirmwareFile : public ::testing::Test {
protected:
    std::filesystem::path write(const QString& name, const QByteArray& content) {
        const QString path = m_dir.filePath(name);
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(content);
        return {path.toStdString()};
    }

private:
    QTemporaryDir m_dir;
};

TEST_F(FirmwareFile, BinNeedsAnAddress) {
    const auto path = write(QStringLiteral("fw.BIN"), QByteArray(64, '\x5A'));
    EXPECT_TRUE(hwio::firmware::isBinaryFile(path));
    EXPECT_EQ(hwio::firmware::readFirmwareFile(path, std::nullopt).error().code,
              ErrorCode::FirmwareBinAddressRequired);
    const auto image = hwio::firmware::readFirmwareFile(path, 0x08000000U);
    ASSERT_TRUE(image) << image.error().detail;
    EXPECT_EQ(image->totalBytes(), 64U);
}

TEST_F(FirmwareFile, HexIgnoresTheAddress) {
    const auto path =
        write(QStringLiteral("fw.hex"), QByteArray(":0400000001020304F2\n:00000001FF\n"));
    EXPECT_FALSE(hwio::firmware::isBinaryFile(path));
    const auto image = hwio::firmware::readFirmwareFile(path, 0x08000000U);
    ASSERT_TRUE(image) << image.error().detail;
    EXPECT_EQ(image->segments().front().address, 0U);
}

TEST_F(FirmwareFile, MissingFile) {
    EXPECT_EQ(hwio::firmware::readFirmwareFile("/nonexistent/x.bin", 0x08000000U).error().code,
              ErrorCode::IoFileNotFound);
}

} // namespace
