#include <AYAbility/FixedActionDocument.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ayt::ability {
namespace {
using Json = nlohmann::json;
constexpr std::size_t MaxBytes = 1024 * 1024;
constexpr std::size_t MaxActions = 128;
constexpr std::size_t MaxMarkers = 4096;

template<class T> Result<T> invalid(std::string message) {
    return std::unexpected(Diagnostic{Error::Invalid, std::move(message), {}});
}

std::uint64_t unsignedValue(const Json& object, const char* name) {
    const auto& value = object.at(name);
    if (!value.is_number_unsigned())
        throw std::runtime_error(std::string(name) + " must be an unsigned integer");
    return value.get<std::uint64_t>();
}

std::uint32_t tickValue(const Json& object, const char* name) {
    const auto value = unsignedValue(object, name);
    if (value > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error(std::string(name) + " exceeds uint32 range");
    return static_cast<std::uint32_t>(value);
}

Result<FixedActionDocument> canonical(FixedActionDocument document) {
    if (document.version != 1 || document.actions.empty()
        || document.actions.size() > MaxActions)
        return invalid<FixedActionDocument>("invalid fixed action document version or action count");
    std::size_t markerCount = 0;
    for (auto& action : document.actions) {
        markerCount += action.markers.size();
        if (markerCount > MaxMarkers)
            return invalid<FixedActionDocument>("fixed action marker budget exceeded");
        std::sort(action.markers.begin(), action.markers.end(),
            [](const auto& left, const auto& right) {
                return left.offset != right.offset ? left.offset < right.offset
                    : left.id < right.id;
            });
    }
    std::sort(document.actions.begin(), document.actions.end(),
        [](const auto& left, const auto& right) { return left.id < right.id; });
    if (auto compiled = FixedActionTimeline::compile(document.actions); !compiled)
        return std::unexpected(compiled.error());
    return document;
}
} // namespace

Result<FixedActionDocument> loadFixedActionDocument(std::string_view text) {
    if (text.size() > MaxBytes)
        return invalid<FixedActionDocument>("fixed action document exceeds 1 MiB");
    try {
        const auto root = Json::parse(text.begin(), text.end());
        if (!root.is_object() || root.size() != 2
            || unsignedValue(root, "version") != 1
            || !root.at("actions").is_array())
            return invalid<FixedActionDocument>("invalid fixed action document header");
        const auto& rows = root.at("actions");
        if (rows.empty() || rows.size() > MaxActions)
            return invalid<FixedActionDocument>("invalid fixed action count");
        FixedActionDocument document;
        document.actions.reserve(rows.size());
        std::size_t markerCount = 0;
        for (const auto& row : rows) {
            if (!row.is_object() || row.size() != 6
                || !row.at("markers").is_array())
                return invalid<FixedActionDocument>("invalid fixed action row");
            FixedActionDefinition action;
            action.id = unsignedValue(row, "id");
            action.windupTicks = tickValue(row, "windupTicks");
            action.activeTicks = tickValue(row, "activeTicks");
            action.recoveryTicks = tickValue(row, "recoveryTicks");
            action.inputBufferTicks = tickValue(row, "inputBufferTicks");
            markerCount += row.at("markers").size();
            if (markerCount > MaxMarkers)
                return invalid<FixedActionDocument>("fixed action marker budget exceeded");
            for (const auto& value : row.at("markers")) {
                if (!value.is_object() || value.size() != 2)
                    return invalid<FixedActionDocument>("invalid fixed action marker");
                action.markers.push_back({tickValue(value, "offset"),
                                          unsignedValue(value, "id")});
            }
            document.actions.push_back(std::move(action));
        }
        return canonical(std::move(document));
    } catch (const std::exception& error) {
        return invalid<FixedActionDocument>(error.what());
    }
}

Result<std::string> saveFixedActionDocument(
    const FixedActionDocument& document) {
    try {
        auto sorted = canonical(document);
        if (!sorted) return std::unexpected(sorted.error());
        Json root = {{"version", 1}, {"actions", Json::array()}};
        for (const auto& action : sorted->actions) {
            Json markers = Json::array();
            for (const auto& marker : action.markers)
                markers.push_back({{"offset", marker.offset}, {"id", marker.id}});
            root["actions"].push_back({{"id", action.id},
                {"windupTicks", action.windupTicks},
                {"activeTicks", action.activeTicks},
                {"recoveryTicks", action.recoveryTicks},
                {"inputBufferTicks", action.inputBufferTicks},
                {"markers", std::move(markers)}});
        }
        auto encoded = root.dump();
        if (encoded.size() > MaxBytes)
            return invalid<std::string>("fixed action document exceeds 1 MiB");
        return encoded;
    } catch (const std::exception& error) {
        return invalid<std::string>(error.what());
    }
}
} // namespace ayt::ability
