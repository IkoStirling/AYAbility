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
    /// Combat-owned definitions are supplied at composition time under these references.
    std::vector<Id> effectReferences;
};
Result<AuthoringDocument> loadAuthoring(std::string_view json);
Result<std::string> saveAuthoring(const AuthoringDocument& document);
Result<std::shared_ptr<const Catalog>> compileAuthoring(const AuthoringDocument& document);
} // namespace ayt::ability
