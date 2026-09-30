#pragma once
#include <AYAbility/Derived.h>

namespace ayt::ability {
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
    /// Combat-owned definitions are supplied at composition time under these references.
    std::vector<Id> effectReferences;
};
Result<AuthoringDocument> loadAuthoring(std::string_view json);
Result<std::string> saveAuthoring(const AuthoringDocument& document);
Result<std::shared_ptr<const Catalog>> compileAuthoring(const AuthoringDocument& document);
} // namespace ayt::ability
