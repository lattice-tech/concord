#include "engine/ui/UiDocumentIO.h"

#include "engine/asset/cook/CookedUiDocument.h"
#include "engine/debug/Logger.h"

#include <filesystem>
#include <fstream>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Concord::UI {

namespace {

Asset::CookedUiDocumentLimits FileLimits() noexcept
{
    Asset::CookedUiDocumentLimits limits;
    limits.maxFileBytes = UiDocumentIO::kMaxFileBytes;
    limits.maxWidgets = static_cast<std::uint32_t>(UiDocumentIO::kMaxWidgetCount);
    limits.maxWidgetTextBytes =
        static_cast<std::uint32_t>(UiDocumentIO::kMaxWidgetTextBytes);
    return limits;
}

bool ReadFile(const std::string& path, std::vector<std::uint8_t>& bytes)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const std::streamsize streamSize = input.tellg();
    if (streamSize <= 0
        || streamSize > static_cast<std::streamsize>(UiDocumentIO::kMaxFileBytes)) {
        return false;
    }
    input.seekg(0);
    bytes.resize(static_cast<std::size_t>(streamSize));
    return static_cast<bool>(input.read(
        reinterpret_cast<char*>(bytes.data()), streamSize));
}

bool WriteFile(const std::string& path, const std::vector<std::uint8_t>& bytes)
{
    std::error_code error;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, error)) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    return static_cast<bool>(output);
}

} // namespace

bool UiDocumentIO::Save(const UiDocument& document, const std::string& path)
{
    try {
        const std::vector<std::uint8_t> bytes =
            Asset::CookedUiDocument::Encode(document, FileLimits());
        if (!WriteFile(path, bytes)) {
            Debug::Logger::Error("UI", "cui save: cannot write '%s'", path.c_str());
            return false;
        }
        Debug::Logger::Info("UI", "saved '%s' (%zu widgets, %zu bytes)",
                            path.c_str(), document.widgets.size(), bytes.size());
        return true;
    } catch (const std::invalid_argument&) {
        Debug::Logger::Error("UI", "cui save: invalid or oversized document for '%s'",
                             path.c_str());
    } catch (const std::bad_alloc&) {
        Debug::Logger::Error("UI", "cui save: allocation failed for '%s'", path.c_str());
    } catch (const std::length_error&) {
        Debug::Logger::Error("UI", "cui save: allocation size rejected for '%s'",
                             path.c_str());
    }
    return false;
}

bool UiDocumentIO::Load(UiDocument& document, const std::string& path)
{
    try {
        std::vector<std::uint8_t> bytes;
        if (!ReadFile(path, bytes)) {
            Debug::Logger::Error("UI", "cui load: cannot read '%s'", path.c_str());
            return false;
        }
        std::optional<UiDocument> loaded = Asset::CookedUiDocument::Decode(
            bytes.data(), bytes.size(), FileLimits());
        if (!loaded) {
            Debug::Logger::Error("UI", "cui load: invalid cooked document in '%s'",
                                 path.c_str());
            return false;
        }
        document = std::move(*loaded);
        Debug::Logger::Info("UI", "loaded '%s' (%zu widgets)",
                            path.c_str(), document.widgets.size());
        return true;
    } catch (const std::bad_alloc&) {
        Debug::Logger::Error("UI", "cui load: allocation failed for '%s'", path.c_str());
    } catch (const std::length_error&) {
        Debug::Logger::Error("UI", "cui load: allocation size rejected for '%s'",
                             path.c_str());
    }
    return false;
}

} // namespace Concord::UI
