#pragma once
#include <AYAbility/Runtime.h>
#include <AYAbility/Derived.h>
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
struct DerivedCastResult {
    ActivationEvent cast;
    /// Give these immutable spawn commands to a projectile/physics host.
    std::vector<EmissionCommand> emissions;
};
struct DerivedImpactResult {
    /// Full cast snapshot, including the original equipment provider when present.
    std::shared_ptr<const ReleaseSnapshot> source;
    ayt::combat::ActionResult hit;
    ayt::combat::CombatOutcome settlement;
};
/// Composition-time candidate factory. Each cast owns the returned callback until its area ends.
using AreaProviderFactory=std::function<ayt::combat::Result<ayt::combat::AreaCandidateProvider>(
    ayt::entity::EntityHandle)>;

/// World-owned gameplay entry. Equipment state and character skill state are coordinated here;
/// Stats owns cost values and Combat owns target permission/damage/life publication.
/// Fire is immediate; Area and Derived keep independent delivery lifetimes.
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
    /// Configure one skill's bounded stages at composition time; casts freeze the current values.
    Result<void> configureDerivedSkill(Id skill,DerivedDefinition definition);
    /// Cast once, charging Stats and opening Combat hit windows before publishing spawn commands.
    Result<DerivedCastResult> castDerived(ayt::entity::EntityHandle actor,Id skill);
    /// Physics/delivery host reports a candidate; Ability handles Combat, Stats and hit dedup.
    Result<DerivedImpactResult> reportDerivedHit(Identity emitter,
        ayt::entity::EntityHandle target,std::uint32_t part=0,std::uint32_t segment=0);
    Result<void> finishDerivedEmitter(Identity emitter);
    Result<void> cancelDerivedCast(std::uint64_t execution);
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
    std::map<Id,DerivedDefinition> _derivedSkills;
    struct DerivedStageRecord {
        DerivedStage stage;
        ayt::combat::AttackRef attack;
        std::uint64_t endTick=0,nextRequest=1;
        bool open=false;
    };
    struct DerivedEmissionRecord { Identity emitter; Id stage=0; bool active=true; };
    struct DerivedCastRecord {
        std::shared_ptr<const ReleaseSnapshot> source;
        ayt::entity::EntityHandle actor;
        std::uint64_t statsGeneration=0,combatGeneration=0;
        std::map<Id,DerivedStageRecord> stages;
        std::vector<DerivedEmissionRecord> emissions;
    };
    std::map<Id,DerivedCastRecord> _derivedCasts;
    Id _nextEmitter=1;
    bool derivedSourceAvailable(const DerivedCastRecord&) const;
    void closeDerived(DerivedCastRecord&);
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
