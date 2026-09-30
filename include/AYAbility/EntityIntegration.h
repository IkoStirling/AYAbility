#pragma once
#include <AYAbility/Runtime.h>
#include <AYCombat/EntityIntegration.h>

namespace ayt::ability {
struct FireActionResult {
    FireEvent shot;
    std::vector<ayt::combat::ActionResult> hits;
    /// A fired shot keeps its ammo/cooldown when a later target settlement fails.
    std::optional<ayt::combat::Diagnostic> combatError;
};

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
    Result<Identity> actorIdentity(ayt::entity::EntityHandle) const;
    Identity equipmentIdentity(Id instance) const noexcept;
};

/// Register shared Stats/Combat gameplay components before ComponentRegistry seal.
Result<void> registerAbilityGameplayComponents(ayt::entity::ComponentRegistry& registry);
/// Installs/gets the World-owned Stats, Combat and Ability systems in their Sim lanes.
Result<AbilitySystem*> installAbility(ayt::entity::World& world,std::shared_ptr<const Catalog> catalog);
} // namespace ayt::ability
