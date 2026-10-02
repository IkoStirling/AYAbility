#pragma once
#include <AYAbility/Derived.h>
#include <array>

namespace ayt::ability {
enum class CollisionResponse2D { Stop, PassThrough };
/// Physics-independent authoring values; interpreted by the optional Box2D adapter.
struct Delivery2DProfile {
    std::array<float,3> velocity{};
    std::array<float,3> spawnOffset{};
    float radius=0.0f;
    std::uint32_t layerMask=0xffffffffu;
    std::uint32_t reverseAfterTicks=0;
    bool ignoreSource=true;
    CollisionResponse2D obstacle=CollisionResponse2D::Stop;
    CollisionResponse2D target=CollisionResponse2D::Stop;
};
struct Delivery2DBinding {
    Id action=0,stage=0;
    Delivery2DProfile profile;
};
Result<void> validateDelivery2DProfile(const Delivery2DProfile& profile);
/// Physics-independent source-following circle and Combat permission for an Area skill.
struct AreaSkill2DBinding {
    Id skill=0,effect=0;
    float radius=0.0f;
    std::array<float,2> offset{};
    std::uint32_t layerMask=0xffffffffu;
    bool includeSource=false;
    /// Combat InteractionIntent: Beneficial 0, Harmful 1, Neutral 2.
    std::uint32_t intent=2;
    std::uint32_t allowed=0;
    Id requiredTag=0;
    bool requireSelected=false;
    /// Host-provided predicate name; required exactly when requireSelected is true.
    std::string selectionKey;
    std::uint32_t maxCandidates=1024,maxMembers=1024;
    bool requireTargetAlive=true;
};
struct DeliveryBinding {
    Id owner=0; // equipment configuration for fire, skill id for derived
    DerivedDefinition definition;
};
/// Versioned, editable definitions. Compile validates references before replacing a live catalog.
struct AuthoringDocument {
    std::uint32_t version=1;
    std::vector<ActionDefinition> actions;
    std::vector<EquipmentActionConfig> equipment;
    std::vector<SkillDefinition> skills;
    std::vector<DeliveryBinding> fireDeliveries;
    std::vector<DeliveryBinding> derivedSkills;
    std::vector<Delivery2DBinding> delivery2D;
    /// Bind source-following 2D Area skills during installAbility2D.
    std::vector<AreaSkill2DBinding> areaSkills2D;
    /// Combat-owned definitions are supplied at composition time under these references.
    std::vector<Id> effectReferences;
};
Result<AuthoringDocument> loadAuthoring(std::string_view json);
Result<std::string> saveAuthoring(const AuthoringDocument& document);
Result<std::shared_ptr<const Catalog>> compileAuthoring(const AuthoringDocument& document);
} // namespace ayt::ability
