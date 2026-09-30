#pragma once
#include <algorithm>
#include <compare>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ayt::ability {
using Id = std::uint64_t;
using Value = std::int64_t;
enum class Error { Invalid, Missing, Duplicate, Overflow, Stale, Busy, Cooldown, Empty, Limit, Unsupported };
struct Diagnostic { Error code=Error::Invalid; std::string message; std::string path; };
template<class T> using Result=std::expected<T,Diagnostic>;

/// Stable host identity; no pointer or World singleton is retained by Core.
struct Identity {
    Id domain=0, object=0, generation=0;
    bool valid() const noexcept { return domain && object && generation; }
    auto operator<=>(const Identity&) const = default;
};

/// Numeric units are checked when authored layers are combined.
enum class Unit { Amount, Ticks, Count, BasisPoints };
struct Parameter { Id id=0; Unit unit=Unit::Amount; Value value=0; };
enum class TrackMode { Constant, Table, Curve };
struct CurvePoint { std::uint32_t level=0; Value value=0; };
struct NumericTrack {
    TrackMode mode=TrackMode::Constant;
    Value constant=0;
    std::vector<Value> table;
    std::vector<CurvePoint> curve;
};
enum class PatchOp { Replace, Add, MultiplyBasisPoints };
struct ParameterPatch { Id parameter=0; PatchOp operation=PatchOp::Replace; NumericTrack values; };
struct Feature { Id id=0, prerequisite=0, exclusiveGroup=0; std::uint32_t unlockLevel=1; };

/// A reusable behavior is identified by behaviorId; no game-specific executor lives in Core.
struct ActionDefinition {
    Id id=0, behaviorId=0;
    std::vector<Parameter> defaults;
    std::vector<Feature> features;
};
/// Equipment configuration supplies values, never an implicit character skill level.
struct EquipmentActionConfig {
    Id id=0, action=0;
    std::vector<ParameterPatch> parameters;
};
enum class SkillKind { Passive, Active };
struct SkillCost { std::string resource; NumericTrack amount; };
struct SkillDefinition {
    Id id=0, action=0;
    SkillKind kind=SkillKind::Passive;
    std::uint32_t maxLevel=1;
    std::vector<ParameterPatch> parameters;
    std::vector<Feature> features;
    std::optional<SkillCost> cost;
};
struct SkillLevel { Id skill=0; std::uint32_t level=0; };
struct ResolvedAction {
    Id action=0, behaviorId=0, equipmentConfig=0, activeSkill=0;
    std::vector<Parameter> parameters;
    std::vector<Id> features;
    std::optional<std::pair<std::string,Value>> cost;
    Result<Value> value(Id parameter, Unit unit) const;
    bool hasFeature(Id feature) const noexcept;
};

/// Compile once; resolution owns a snapshot and never modifies author definitions.
class Catalog {
public:
    static Result<std::shared_ptr<const Catalog>> compile(std::vector<ActionDefinition> actions,
        std::vector<EquipmentActionConfig> equipment, std::vector<SkillDefinition> skills);
    Result<ResolvedAction> resolve(Id action, Id equipmentConfig,
        std::span<const SkillLevel> levels, Id activeSkill=0) const;
    const ActionDefinition* findAction(Id id) const noexcept;
    const EquipmentActionConfig* findEquipment(Id id) const noexcept;
    const SkillDefinition* findSkill(Id id) const noexcept;
private:
    std::vector<ActionDefinition> _actions;
    std::vector<EquipmentActionConfig> _equipment;
    std::vector<SkillDefinition> _skills;
};
} // namespace ayt::ability
