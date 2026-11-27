#include "hwio/firmware/firmware_file.h"

#include "file_io.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/intel_hex.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace hwio::firmware {

bool isBinaryFile(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return extension == ".bin";
}

Result<FirmwareImage> readFirmwareFile(const std::filesystem::path& path,
                                       std::optional<std::uint32_t> binAddress,
                                       const std::stop_token& stop) {
    if (!isBinaryFile(path)) {
        return readIntelHexFile(path, stop);
    }
    if (!binAddress) {
        return std::unexpected(makeError(ErrorCode::FirmwareBinAddressRequired, path.string()));
    }
    auto bytes = detail::readFileBounded(path, maxBinaryBytes);
    if (!bytes) {
        return std::unexpected(std::move(bytes).error());
    }
    if (stop.stop_requested()) {
        return fail(ErrorCode::Cancelled);
    }
    return FirmwareImage::fromBinary(*bytes, *binAddress);
}

} // namespace hwio::firmware
