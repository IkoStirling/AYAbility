#pragma once

#include <AYAbility/FixedActionTimeline.h>

#include <string_view>

namespace ayt::ability {

/// Versioned author data for a fixed-step action timeline. IDs are stable
/// numeric content references; markers still have game-defined meanings.
/// Optional cancel windows authorize named action transitions at fixed ticks.
struct FixedActionDocument {
    std::uint32_t version = 1;
    std::vector<FixedActionDefinition> actions;
};

/// Parse and validate one bounded JSON document. No live timeline is changed
/// on failure. Output actions and markers are in canonical ID/tick order.
[[nodiscard]] Result<FixedActionDocument> loadFixedActionDocument(
    std::string_view json);

/// Validate and emit canonical JSON for authoring tools and content diffs.
[[nodiscard]] Result<std::string> saveFixedActionDocument(
    const FixedActionDocument& document);

} // namespace ayt::ability
