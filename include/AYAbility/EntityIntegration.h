#pragma once
#include <AYAbility/Runtime.h>
#include <AYAbility/Derived.h>
#include <AYAbility/Combo.h>
#include <AYCombat/EntityIntegration.h>

namespace ayt::ability {
struct AuthoringDocument;
#define AY_CURRENT_CLASS AbilityEquipmentComponent
/// One authored equipment slot. Multiple instances may be placed on an Actor;
/// its display name is presentation only, never a gameplay key.
class AbilityEquipmentComponent final : public ayt::entity::IComponent {
public:
    const char* getName() const override { return "AbilityEquipment"; }
    AY_PROPERTY(Id, configuration, ayt::entity::kAttrSerialize)
    AY_PROPERTY(bool, equipped, ayt::entity::kAttrSerialize)
};
#undef AY_CURRENT_CLASS
#define AY_CURRENT_CLASS AbilitySkillComponent
/// One authored character skill grant. Multiple instances may be placed on an Actor.
class AbilitySkillComponent final : public ayt::entity::IComponent {
public:
    const char* getName() const override { return "AbilitySkill"; }
    AY_PROPERTY(Id, skillId, ayt::entity::kAttrSerialize)
    AY_PROPERTY(std::uint32_t, level, ayt::entity::kAttrSerialize)
};
#undef AY_CURRENT_CLASS
struct FireActionResult {
    FireEvent shot;
    std::vector<ComboEvent> combos;
    std::vector<ayt::combat::ActionResult> hits;
    /// Advanced per-projectile outcomes retain Combat source, attack and request identities.
    std::vector<ayt::combat::CombatOutcome> settlements;
    /// A fired shot keeps its ammo/cooldown when a later target settlement fails.
    std::optional<ayt::combat::Diagnostic> combatError;
};
struct AreaCastResult {
    ActivationEvent cast;
    std::uint64_t area=0;
    std::vector<ComboEvent> combos;
};
struct DerivedCastResult {
    ActivationEvent cast;
    /// Give these immutable spawn commands to a projectile/physics host.
    std::vector<EmissionCommand> emissions;
    std::vector<ComboEvent> combos;
};
struct DerivedImpactResult {
    /// Full cast snapshot, including the original equipment provider when present.
    std::shared_ptr<const ReleaseSnapshot> source;
    ayt::combat::ActionResult hit;
    ayt::combat::CombatOutcome settlement;
    /// Present for non-HP attribute and Combat-owned effect stages.
    std::optional<ayt::combat::InteractionResult> interaction;
    /// Newly triggered emissions, also available through takeEmissions for delivery hosts.
    std::vector<EmissionCommand> emissions;
};
struct FireDeliveryResult {
    FireEvent shot;
    std::vector<EmissionCommand> emissions;
    std::vector<ComboEvent> combos;
};
/// Composition-time candidate factory. Each cast owns the returned callback until its area ends.
using AreaProviderFactory=std::function<ayt::combat::Result<ayt::combat::AreaCandidateProvider>(
    ayt::entity::EntityHandle)>;
/// Composition-time Combat definitions keyed by references in an Ability document.
using OwnedEffectBindings=std::map<Id,ayt::combat::EffectDefinition>;
/// Strict preflight: every declared reference has one structurally valid definition,
/// with no extras. Target-specific Stats modifier compatibility is checked on application.
Result<void> validateOwnedEffectBindings(const AuthoringDocument& document,
    const OwnedEffectBindings& effects);

/// World-owned gameplay entry. Equipment state and character skill state are coordinated here;
/// Stats owns cost values and Combat owns target permission/damage/life publication.
/// Immediate fire, staged fire and Derived share the same Stats/Combat authority;
/// staged delivery and Area each keep their own lifetime.
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
    /// Install independent fixed-tick combo content. Accepted releases record tokens automatically.
    Result<void> configureCombos(std::vector<ComboDefinition> definitions);
    Result<void> beginReload(ayt::entity::EntityHandle actor);
    /// A missing target still fires; target combat failures are reported in FireActionResult.
    /// Source death/stun and skill resource failure reject before ammo or cooldown changes.
    Result<FireActionResult> fireAt(ayt::entity::EntityHandle actor,
        std::optional<ayt::entity::EntityHandle> target={},Id activeSkill=0,
        std::string_view recipe={});
    /// A configured equipment action uses the same stage timeline as an active Derived skill.
    Result<void> configureFireDelivery(Id configuration,DerivedDefinition definition);
    Result<FireDeliveryResult> fireDelivered(ayt::entity::EntityHandle actor,Id activeSkill=0);
    /// Register authored area behavior once; castArea then needs only actor and granted skill.
    Result<void> configureAreaSkill(Id skill,ayt::combat::AreaDefinition definition,
        AreaProviderFactory provider);
    /// Composition preflight; checks that this installed catalog owns an active Area skill.
    Result<void> validateAreaSkill(Id skill) const;
    Result<AreaCastResult> castArea(ayt::entity::EntityHandle actor,Id skill);
    Result<ayt::combat::AreaSweep> stopArea(std::uint64_t area);
    /// Configure one skill's bounded stages at composition time; casts freeze the current values.
    Result<void> configureDerivedSkill(Id skill,DerivedDefinition definition);
    /// Bind an authored effect reference used by one or more delivery stages.
    Result<void> configureOwnedEffect(Id reference,ayt::combat::EffectDefinition definition);
    /// Resolve a composition-time Combat effect reference for authored Area bindings.
    Result<ayt::combat::EffectDefinition> ownedEffect(Id reference) const;
    /// Cast once, charging Stats and opening Combat hit windows before publishing spawn commands.
    Result<DerivedCastResult> castDerived(ayt::entity::EntityHandle actor,Id skill);
    /// Physics/delivery host reports a candidate; Ability handles Combat, Stats and hit dedup.
    Result<DerivedImpactResult> reportDerivedHit(Identity emitter,
        ayt::entity::EntityHandle target,std::uint32_t part=0,std::uint32_t segment=0,
        std::optional<std::array<float,3>> contact={});
    Result<void> finishDerivedEmitter(Identity emitter);
    Result<void> cancelDerivedCast(std::uint64_t execution);
    /// Drains commands whose fixed-tick trigger has fired; stale/cancelled emissions are omitted.
    std::vector<EmissionCommand> takeEmissions();
    /// Delivery adapters use this to discard projectiles after cancellation or source loss.
    bool emitterActive(Identity emitter) const noexcept;
    Result<EquipmentState> equipment(Id instance) const;
    Result<GrantedSkill> skill(ayt::entity::EntityHandle actor,Id skill);
    struct DebugRecord { ayt::entity::EntityHandle actor; std::string label,details; };
    /// Owned read-only cooldown/equipment/stage observations, at most 512 rows. No hydration or delivery.
    std::vector<DebugRecord> debugRecords() const;
private:
    struct ActorRecord {
        ayt::entity::EntityHandle handle;
        std::uint64_t statsGeneration=0,combatGeneration=0;
        bool authoredInitialized=false;
    };
    ayt::entity::World* _world=nullptr;
    ayt::combat::CombatSystem* _combat=nullptr;
    std::shared_ptr<const Catalog> _catalog;
    std::optional<Runtime> _runtime;
    std::optional<ComboTracker> _combos;
    std::map<Identity,ActorRecord> _actors;
    struct AreaSkill {
        ayt::combat::AreaDefinition definition;
        AreaProviderFactory provider;
    };
    std::map<Id,AreaSkill> _areaSkills;
    std::map<Id,DerivedDefinition> _derivedSkills;
    std::map<Id,DerivedDefinition> _fireDeliveries;
    std::map<Id,ayt::combat::EffectDefinition> _ownedEffects;
    struct DerivedStageRecord {
        DerivedStage stage;
        ayt::combat::AttackRef attack;
        std::uint64_t endTick=0,nextRequest=1,dueTick=0;
        bool open=false,triggered=false,emitted=false,ended=false,hit=false;
        std::optional<ayt::combat::EffectDefinition> effect;
        std::optional<std::array<float,3>> origin;
    };
    struct DerivedEmissionRecord { Identity emitter; Id stage=0; bool active=true; };
    struct DerivedCastRecord {
        std::shared_ptr<const ReleaseSnapshot> source;
        ayt::entity::EntityHandle actor;
        std::uint64_t statsGeneration=0,combatGeneration=0;
        std::map<Id,DerivedStageRecord> stages;
        std::vector<Id> stageOrder;
        std::vector<DerivedEmissionRecord> emissions;
    };
    std::map<Id,DerivedCastRecord> _derivedCasts;
    std::vector<EmissionCommand> _emissionQueue;
    Id _nextEmitter=1;
    Id _nextAuthoredEquipment=Id{1}<<63;
    bool derivedSourceAvailable(const DerivedCastRecord&) const;
    void closeDerived(DerivedCastRecord&);
    Result<void> prepareDelivery(ayt::entity::EntityHandle actor,
        const ayt::combat::EntityBinding& binding,DerivedRelease& release,
        const std::optional<std::pair<std::string,Value>>& cost);
    std::vector<EmissionCommand> advanceDelivery(DerivedCastRecord&,std::uint64_t tick);
    void triggerDependents(DerivedCastRecord&,Id predecessor,StageTrigger trigger,
        std::uint64_t tick,std::optional<std::array<float,3>> origin={});
    Result<Identity> actorIdentity(ayt::entity::EntityHandle);
    Result<void> initializeAuthored(ayt::entity::EntityHandle,Identity,ActorRecord&);
    std::vector<ComboEvent> recordCombo(Identity actor,Id token,std::uint64_t tick);
    Identity equipmentIdentity(Id instance) const noexcept;
    Result<void> chargeCost(ayt::entity::EntityHandle actor,
        const std::optional<std::pair<std::string,Value>>& cost);
};

/// Register Stats, Combat and repeatable authored Ability components before registry seal.
Result<void> registerAbilityGameplayComponents(ayt::entity::ComponentRegistry& registry);
/// Installs/gets the World-owned Stats, Combat and Ability systems in their Sim lanes.
Result<AbilitySystem*> installAbility(ayt::entity::World& world,std::shared_ptr<const Catalog> catalog);
/// Advanced document entry: install stages, then bind Combat effects separately.
Result<AbilitySystem*> installAbility(ayt::entity::World& world,
    const AuthoringDocument& document);
/// Strict document installation: validates all Combat effect bindings before changing World.
Result<AbilitySystem*> installAbility(ayt::entity::World& world,
    const AuthoringDocument& document,const OwnedEffectBindings& effects);
} // namespace ayt::ability
