#pragma once
#include <AYAbility/Runtime.h>
#include <AYCombat/EntityIntegration.h>

namespace ayt::ability {
struct FireActionResult {
    FireEvent shot;
    std::vector<ayt::combat::ActionResult> hits;
    /// Advanced per-projectile outcomes retain Combat source, attack and request identities.
    std::vector<ayt::combat::CombatOutcome> settlements;
    /// A fired shot keeps its ammo/cooldown when a later target settlement fails.
    std::optional<ayt::combat::Diagnostic> combatError;
};
struct AreaCastResult {
    ActivationEvent cast;
    std::uint64_t area=0;
};
/// Composition-time candidate factory. Each cast owns the returned callback until its area ends.
using AreaProviderFactory=std::function<ayt::combat::Result<ayt::combat::AreaCandidateProvider>(
    ayt::entity::EntityHandle)>;

/// World-owned gameplay entry. Equipment state and character skill state are coordinated here;
/// Stats owns cost values and Combat owns target permission/damage/life publication.
/// Fire currently executes one behavior; later Combat failures remain fired shots.
class AbilitySystem final : public ayt::entity::ISystem {
public:
    const char* getName() const override { return "AbilitySystem"; }
    void onUpdate(float) override;
    Result<void> bind(ayt::entity::World& world,ayt::combat::CombatSystem& combat,
        std::shared_ptr<const Catalog> catalog);
    Result<void> addEquipment(ayt::entity::EntityHandle actor,Id equipmentInstance,Id configuration);
    Result<void> equip(ayt::entity::EntityHandle actor,Id equipmentInstance);
    Result<void> grant(ayt::entity::EntityHandle actor,Id skill,std::uint32_t level);
    Result<void> setLevel(ayt::entity::EntityHandle actor,Id skill,std::uint32_t level);
    Result<void> beginReload(ayt::entity::EntityHandle actor);
    /// A missing target still fires; target combat failures are reported in FireActionResult.
    /// Source death/stun and skill resource failure reject before ammo or cooldown changes.
    Result<FireActionResult> fireAt(ayt::entity::EntityHandle actor,
        std::optional<ayt::entity::EntityHandle> target={},Id activeSkill=0,
        std::string_view recipe={});
    /// Register authored area behavior once; castArea then needs only actor and granted skill.
    Result<void> configureAreaSkill(Id skill,ayt::combat::AreaDefinition definition,
        AreaProviderFactory provider);
    Result<AreaCastResult> castArea(ayt::entity::EntityHandle actor,Id skill);
    Result<ayt::combat::AreaSweep> stopArea(std::uint64_t area);
    Result<EquipmentState> equipment(Id instance) const;
    Result<GrantedSkill> skill(ayt::entity::EntityHandle actor,Id skill) const;
private:
    struct ActorRecord {
        ayt::entity::EntityHandle handle;
        std::uint64_t statsGeneration=0,combatGeneration=0;
    };
    ayt::entity::World* _world=nullptr;
    ayt::combat::CombatSystem* _combat=nullptr;
    std::shared_ptr<const Catalog> _catalog;
    std::optional<Runtime> _runtime;
    std::map<Identity,ActorRecord> _actors;
    struct AreaSkill {
        ayt::combat::AreaDefinition definition;
        AreaProviderFactory provider;
    };
    std::map<Id,AreaSkill> _areaSkills;
    Result<Identity> actorIdentity(ayt::entity::EntityHandle) const;
    Identity equipmentIdentity(Id instance) const noexcept;
    Result<void> chargeCost(ayt::entity::EntityHandle actor,
        const std::optional<std::pair<std::string,Value>>& cost);
};

/// Register shared Stats/Combat gameplay components before ComponentRegistry seal.
Result<void> registerAbilityGameplayComponents(ayt::entity::ComponentRegistry& registry);
/// Installs/gets the World-owned Stats, Combat and Ability systems in their Sim lanes.
Result<AbilitySystem*> installAbility(ayt::entity::World& world,std::shared_ptr<const Catalog> catalog);
} // namespace ayt::ability
