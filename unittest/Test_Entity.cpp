#include <AYTest.h>
#include <AYAbility/EntityIntegration.h>
#include <AYAbility/Authoring.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/ActorClassAsset.h>
#include "Fixtures.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace {
struct WorldFixture {
    ayt::entity::World& world=ayt::entity::World::processWorld();
    ayt::ability::AbilitySystem* ability=nullptr;
    ayt::combat::CombatSystem* combat=nullptr;
    ayt::entity::EntityHandle source,target;
    WorldFixture() {
        ayt::entity::World::setActiveWorld(nullptr);
        world.initialize();
        auto& registry=ayt::entity::ComponentRegistry::instance();
        if(!registry.isSealed()) {
            (void)ayt::entity::registerEntityCoreComponents(registry);
            if(!ayt::ability::registerAbilityGameplayComponents(registry))throw std::runtime_error("Ability registration");
            registry.seal();
        }
        ability=ayt::ability::installAbility(world,fixture::catalog()).value();
        combat=dynamic_cast<ayt::combat::CombatSystem*>(world.findSystemByName("CombatSystem"));
        auto* a=world.createEntity();auto* b=world.createEntity();
        source=world.getEntityHandle(a->getId());target=world.getEntityHandle(b->getId());
        auto compiled=ayt::stats::Schema::compile({
            {.name="health_max",.shape=ayt::stats::Shape::Scalar,.mode=ayt::stats::Mode::Advanced,
                .initial=100,.zones={{1,ayt::stats::ZoneOp::Minimum}}},
            {.name="health",.shape=ayt::stats::Shape::Pool,.mode=ayt::stats::Mode::Advanced,
                .current=100,.capacity=100,
                .capacityPolicy=ayt::stats::CapacityPolicy::KeepCurrent,
                .capacityFrom=ayt::stats::statId("health_max")},
            {.name="mana",.shape=ayt::stats::Shape::Pool,.current=80,.capacity=80}
        });
        if(!compiled)throw std::runtime_error(compiled.error().message);
        auto schema=*compiled;
        ayt::combat::ActorProfile profile{schema,{ayt::stats::statId("health")},1};
        if(!combat->configureActor(source,profile))throw std::runtime_error("source profile");
        profile.team=2;if(!combat->configureActor(target,profile))throw std::runtime_error("target profile");
        if(!combat->setRelation(1,2,ayt::combat::Relation::Hostile))throw std::runtime_error("relation");
    }
    ~WorldFixture(){world.shutdown();}
    ayt::stats::StatsComponent& stats(ayt::entity::EntityHandle handle) {
        return *world.getEntityByHandle(handle)->getComponent<ayt::stats::StatsComponent>();
    }
};
}
TEST_SUITE(AbilityEntityTests)
TEST_CASE(repeatable_authored_components_initialize_on_first_use_independent_of_display_name) {
    WorldFixture f;
    auto* actor=f.world.getEntityByHandle(f.source);
    auto* first=actor->createComponent<ayt::ability::AbilityEquipmentComponent>();
    auto* second=actor->createComponent<ayt::ability::AbilityEquipmentComponent>();
    auto* skill=actor->createComponent<ayt::ability::AbilitySkillComponent>();
    CHECK_NOT_NULL(first);CHECK_NOT_NULL(second);CHECK_NOT_NULL(skill);
    first->configuration=101;first->equipped=true;
    second->configuration=102;second->equipped=false;
    skill->skillId=202;skill->level=1;
    CHECK_TRUE(actor->setComponentDisplayName(first,"Renamed primary"));
    auto fired=f.ability->fireAt(f.source,f.target,202);
    CHECK_TRUE(fired.has_value());
    CHECK_TRUE(fired->shot.resolved.equipmentConfig==101);
    CHECK_TRUE(fired->shot.ammunitionRemaining==5);
    CHECK_TRUE(f.ability->skill(f.source,202)->level==1);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==90);
}
TEST_CASE(actor_class_with_stats_combat_and_ability_is_playable_before_first_tick) {
    WorldFixture f;
    auto schema=ayt::stats::Schema::compile({
        {.name="health",.shape=ayt::stats::Shape::Pool,.current=100,.capacity=100},
        {.name="mana",.shape=ayt::stats::Shape::Pool,.current=80,.capacity=80}}).value();
    const auto definitions=ayt::stats::encodeDefinitions(*schema).value();
    const auto root=std::filesystem::path(ayt::test::testTmpPath("ability-actor-assets"));
    std::filesystem::create_directories(root/"actors");
    ayt::entity::ActorClassAsset authored;
    authored.id="AbilityActor";
    authored.components={
        {"StatsComponent",nlohmann::json{{"definitionsJson",definitions}}.dump()},
        {"CombatComponent",nlohmann::json{{"team",1},{"lifeResourceName","health"}}.dump()},
        {"AbilityEquipmentComponent",R"({"configuration":101,"equipped":true})"},
        {"AbilityEquipmentComponent",R"({"configuration":102,"equipped":false})"},
        {"AbilitySkillComponent",R"({"skillId":202,"level":1})"}};
    authored.components[2].instanceId=ayt::entity::makeComponentInstanceId();
    authored.components[3].instanceId=ayt::entity::makeComponentInstanceId();
    std::string error;
    if(!ayt::entity::saveActorClassAsset((root/"actors"/"AbilityActor.act").string(),authored,&error))
        throw std::runtime_error("save Ability Actor: "+error);
    auto* actor=f.world.createEntity();
    if(!ayt::entity::instantiateActorClass(*actor,authored,"actors/AbilityActor.act",
        root.string(),&error))throw std::runtime_error("instantiate Ability Actor: "+error);
    const auto handle=f.world.getEntityHandle(actor->getId());
    auto fired=f.ability->fireAt(handle,f.target,202);
    if(!fired)throw std::runtime_error("fire Ability Actor: "+fired.error().message);
    CHECK_TRUE(fired->shot.resolved.equipmentConfig==101);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==90);
}
TEST_CASE(strict_document_rejects_missing_effects_before_world_installation) {
    auto& world=ayt::entity::World::processWorld();
    ayt::entity::World::setActiveWorld(nullptr);
    world.initialize();
    auto& registry=ayt::entity::ComponentRegistry::instance();
    if(!registry.isSealed()) {
        (void)ayt::entity::registerEntityCoreComponents(registry);
        CHECK_TRUE(ayt::ability::registerAbilityGameplayComponents(registry).has_value());
        registry.seal();
    }
    ayt::ability::AuthoringDocument document;
    document.actions={{.id=30,.behaviorId=ayt::ability::derived::Behavior,
        .defaults={{ayt::ability::derived::CooldownTicks,ayt::ability::Unit::Ticks,1}}}};
    document.skills={{.id=404,.action=30,.kind=ayt::ability::SkillKind::Active}};
    document.effectReferences={700};
    document.derivedSkills={{404,{.stages={{.id=1,.lifetimeTicks=3,
        .operation=ayt::ability::StageOperation::OwnedEffect,.effect=700}}}}};
    CHECK_FALSE(ayt::ability::installAbility(world,document,
        ayt::ability::OwnedEffectBindings{}).has_value());
    CHECK_FALSE(world.findSystemByName("AbilitySystem"));
    ayt::combat::EffectDefinition effect;
    effect.type=700;effect.key=700;effect.tags={99};
    ayt::ability::OwnedEffectBindings effects{{700,effect}};
    auto invalid=effects;invalid.at(700).type=0;
    CHECK_FALSE(ayt::ability::installAbility(world,document,invalid).has_value());
    CHECK_FALSE(world.findSystemByName("AbilitySystem"));
    invalid=effects;invalid.emplace(701,effect);
    CHECK_FALSE(ayt::ability::installAbility(world,document,invalid).has_value());
    CHECK_FALSE(world.findSystemByName("AbilitySystem"));
    auto* ability=ayt::ability::installAbility(world,document,effects).value();
    auto* combat=dynamic_cast<ayt::combat::CombatSystem*>(world.findSystemByName("CombatSystem"));
    auto* sourceEntity=world.createEntity();
    auto* targetEntity=world.createEntity();
    const auto source=world.getEntityHandle(sourceEntity->getId());
    const auto target=world.getEntityHandle(targetEntity->getId());
    auto schema=ayt::stats::Schema::compile({
        {.name="health",.shape=ayt::stats::Shape::Pool,.current=100,.capacity=100}}).value();
    ayt::combat::ActorProfile profile{schema,{ayt::stats::statId("health")},1};
    CHECK_TRUE(combat->configureActor(source,profile).has_value());
    profile.team=2;
    CHECK_TRUE(combat->configureActor(target,profile).has_value());
    CHECK_TRUE(combat->setRelation(1,2,ayt::combat::Relation::Hostile).has_value());
    CHECK_TRUE(ability->grant(source,404,1).has_value());
    auto cast=ability->castDerived(source,404).value();
    CHECK_TRUE(ability->reportDerivedHit(cast.emissions.front().emitter,target).has_value());
    CHECK_TRUE(combat->hasTag(target,99).value());
    world.shutdown();
}
TEST_CASE(simple_entry_coordinates_equipment_skill_cost_and_combat) {
    WorldFixture f;
    CHECK_TRUE(f.ability->addEquipment(f.source,1001,101).has_value());
    CHECK_TRUE(f.ability->addEquipment(f.source,1002,102).has_value());
    CHECK_TRUE(f.ability->equip(f.source,1001).has_value());
    CHECK_TRUE(f.ability->grant(f.source,201,1).has_value());
    CHECK_TRUE(f.ability->grant(f.source,202,1).has_value());
    auto first=f.ability->fireAt(f.source,f.target,202).value();
    CHECK_TRUE(first.shot.amount==10 && first.hits.size()==1 && first.hits[0].actual==10);
    CHECK_TRUE(f.stats(f.source).read(ayt::stats::statId("mana"))->current==75);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==90);
    CHECK_TRUE(f.ability->equipment(1001)->ammunition==5);
    CHECK_TRUE(f.ability->setLevel(f.source,201,3).has_value());
    CHECK_TRUE(f.ability->setLevel(f.source,202,3).has_value());
    CHECK_TRUE(f.ability->equip(f.source,1002).has_value());
    f.world.fixedUpdate(1.0f/60);
    auto second=f.ability->fireAt(f.source,f.target).value();
    CHECK_TRUE(second.shot.amount==14 && second.shot.projectiles==1);
    CHECK_TRUE(f.ability->equipment(1001)->ammunition==5 && f.ability->equipment(1002)->ammunition==11);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==76);
}
TEST_CASE(insufficient_skill_resource_does_not_consume_weapon_state) {
    WorldFixture f;
    CHECK_TRUE(f.ability->addEquipment(f.source,1001,101).has_value());
    CHECK_TRUE(f.ability->equip(f.source,1001).has_value());
    CHECK_TRUE(f.ability->grant(f.source,202,1).has_value());
    const std::array mutation{ayt::stats::Mutation{ayt::stats::statId("mana"),ayt::stats::MutationOp::SetCurrent,0}};
    CHECK_TRUE(f.stats(f.source).commit(mutation,f.stats(f.source).version()).has_value());
    auto denied=f.ability->fireAt(f.source,f.target,202);
    CHECK_TRUE(denied.error().code==ayt::ability::Error::Empty);
    CHECK_TRUE(f.ability->equipment(1001)->ammunition==6);
    CHECK_TRUE(f.ability->skill(f.source,202)->nextReadyTick==0);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==100);
}
TEST_CASE(combat_denial_is_a_fired_shot_with_no_damage) {
    WorldFixture f;
    CHECK_TRUE(f.ability->addEquipment(f.source,1001,101).has_value());
    CHECK_TRUE(f.ability->equip(f.source,1001).has_value());
    CHECK_TRUE(f.combat->setRelation(1,2,ayt::combat::Relation::Friendly).has_value());
    auto fired=f.ability->fireAt(f.source,f.target).value();
    CHECK_TRUE(fired.hits.size()==1 && fired.hits[0].kind==ayt::combat::OutcomeKind::Rejected);
    CHECK_TRUE(fired.shot.ammunitionRemaining==5);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==100);
}
TEST_CASE(burst_projectiles_share_attack_and_retain_distinct_hit_provenance) {
    WorldFixture f;
    CHECK_TRUE(f.ability->addEquipment(f.source,1001,101).has_value());
    CHECK_TRUE(f.ability->equip(f.source,1001).has_value());
    CHECK_TRUE(f.ability->grant(f.source,202,3).has_value());
    auto fired=f.ability->fireAt(f.source,f.target,202).value();
    CHECK_TRUE(fired.shot.projectiles==3 && fired.hits.size()==3 && fired.settlements.size()==3);
    CHECK_TRUE(fired.shot.ammunitionRemaining==5);
    CHECK_TRUE(f.stats(f.source).read(ayt::stats::statId("mana"))->current==75);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==70);
    const auto attack=fired.settlements.front().attack;
    CHECK_TRUE(attack.valid() && fired.hits.front().actionId==attack.id);
    for(std::uint64_t i=0;i<3;++i) {
        const auto& settlement=fired.settlements[i];
        CHECK_TRUE(settlement.attack==attack && settlement.request==i+1);
        CHECK_TRUE(settlement.source.ability==202 && settlement.source.instigator.has_value());
        CHECK_TRUE(settlement.source.instigator->object==f.source.id);
        CHECK_TRUE(settlement.source.abilityInstance==fired.shot.execution);
        CHECK_TRUE(settlement.source.directSource.has_value() &&
            settlement.source.directSource->object==fired.shot.equipment.object);
        CHECK_TRUE(fired.hits[i].actual==10);
    }
    CHECK_TRUE(f.combat->hits().activeAttacks()==0);
}
TEST_CASE(stunned_actor_cannot_spend_ammunition_or_skill_resource) {
    WorldFixture f;
    CHECK_TRUE(f.ability->addEquipment(f.source,1001,101).has_value());
    CHECK_TRUE(f.ability->equip(f.source,1001).has_value());
    CHECK_TRUE(f.ability->grant(f.source,202,1).has_value());
    ayt::combat::EffectDefinition stun;stun.type=700;stun.key=700;stun.stunned=true;
    CHECK_TRUE(f.combat->setRelation(2,1,ayt::combat::Relation::Hostile).has_value());
    ayt::combat::InteractionPolicy policy{ayt::combat::InteractionIntent::Harmful,
        ayt::combat::mask(ayt::combat::AllowedTarget::Hostile)};
    auto applied=f.combat->applyOwnedEffect(f.target,f.source,stun,policy);
    CHECK_TRUE(applied.has_value() && applied->decision.allowed);
    CHECK_FALSE(f.ability->fireAt(f.source,f.target,202).has_value());
    CHECK_TRUE(f.ability->equipment(1001)->ammunition==6);
    CHECK_TRUE(f.stats(f.source).read(ayt::stats::statId("mana"))->current==80);
    CHECK_TRUE(f.ability->skill(f.source,202)->nextReadyTick==0);
}
TEST_CASE(area_skill_keeps_overlapping_capacity_effects_independent) {
    WorldFixture f;
    CHECK_TRUE(f.combat->setRelation(1,2,ayt::combat::Relation::Friendly).has_value());
    CHECK_TRUE(f.ability->grant(f.source,303,1).has_value());
    ayt::combat::AreaDefinition area;
    area.effect.type=800;area.effect.key=800;
    area.effect.modifiers={{.rule=1,.target=ayt::stats::statId("health_max"),.zone=1,.value=1}};
    area.permission={ayt::combat::InteractionIntent::Beneficial,
        ayt::combat::mask(ayt::combat::AllowedTarget::Friendly)};
    std::vector<ayt::combat::AreaCandidate> candidates{{f.target,true}};
    CHECK_TRUE(f.ability->configureAreaSkill(303,area,[&](ayt::entity::EntityHandle)
        ->ayt::combat::Result<ayt::combat::AreaCandidateProvider>{
        return ayt::combat::AreaCandidateProvider{[&]()->ayt::combat::Result<std::vector<ayt::combat::AreaCandidate>>{
            return candidates;
        }};
    }).has_value());
    auto first=f.ability->castArea(f.source,303).value();
    CHECK_TRUE(first.cast.activeSkill==303 && first.area!=0);
    CHECK_TRUE(f.stats(f.source).read(ayt::stats::statId("mana"))->current==70);
    f.world.fixedUpdate(1.0f/60);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->capacity==1);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==1);
    for(int i=0;i<5;++i)f.world.fixedUpdate(1.0f/60);
    auto second=f.ability->castArea(f.source,303).value();
    f.world.fixedUpdate(1.0f/60);
    CHECK_TRUE(f.combat->areaMemberCount(first.area).value()==1);
    CHECK_TRUE(f.combat->areaMemberCount(second.area).value()==1);
    CHECK_TRUE(f.ability->stopArea(first.area)->ended);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->capacity==1);
    CHECK_TRUE(f.ability->stopArea(second.area)->ended);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->capacity==100);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==1);
}
TEST_CASE(area_skill_cost_failure_rolls_back_and_source_death_ends_effect) {
    WorldFixture f;
    CHECK_TRUE(f.combat->setRelation(1,2,ayt::combat::Relation::Friendly).has_value());
    CHECK_TRUE(f.ability->grant(f.source,303,1).has_value());
    ayt::combat::AreaDefinition area;area.effect.type=801;area.effect.key=801;area.effect.tags={99};
    area.permission={ayt::combat::InteractionIntent::Beneficial,
        ayt::combat::mask(ayt::combat::AllowedTarget::Friendly)};
    CHECK_TRUE(f.ability->configureAreaSkill(303,area,[&](ayt::entity::EntityHandle)
        ->ayt::combat::Result<ayt::combat::AreaCandidateProvider>{
        return ayt::combat::AreaCandidateProvider{[&]()->ayt::combat::Result<std::vector<ayt::combat::AreaCandidate>>{
            return std::vector<ayt::combat::AreaCandidate>{{f.target,true}};
        }};
    }).has_value());
    const std::array empty{ayt::stats::Mutation{ayt::stats::statId("mana"),
        ayt::stats::MutationOp::SetCurrent,0}};
    CHECK_TRUE(f.stats(f.source).commit(empty,f.stats(f.source).version()).has_value());
    CHECK_TRUE(f.ability->castArea(f.source,303).error().code==ayt::ability::Error::Empty);
    CHECK_TRUE(f.ability->skill(f.source,303)->nextReadyTick==0);
    CHECK_TRUE(f.combat->areaMemberCount(1).error().code==ayt::combat::Error::Missing);
    const std::array refill{ayt::stats::Mutation{ayt::stats::statId("mana"),
        ayt::stats::MutationOp::SetCurrent,80}};
    CHECK_TRUE(f.stats(f.source).commit(refill,f.stats(f.source).version()).has_value());
    auto cast=f.ability->castArea(f.source,303).value();
    f.world.fixedUpdate(1.0f/60);
    CHECK_TRUE(f.combat->hasTag(f.target,99).value());
    const std::array death{ayt::stats::Mutation{ayt::stats::statId("health"),
        ayt::stats::MutationOp::Decrease,100}};
    CHECK_TRUE(f.stats(f.source).commit(death,f.stats(f.source).version()).has_value());
    f.world.fixedUpdate(1.0f/60);
    CHECK_TRUE(!f.combat->hasTag(f.target,99).value());
    CHECK_TRUE(f.combat->areaMemberCount(cast.area).error().code==ayt::combat::Error::Missing);
}
TEST_CASE(destroyed_actor_state_is_pruned_on_fixed_sim) {
    WorldFixture f;
    CHECK_TRUE(f.ability->addEquipment(f.source,1001,101).has_value());
    auto* entity=f.world.getEntityByHandle(f.source);
    f.world.destroyEntity(entity);
    f.world.fixedUpdate(1.0f/60);
    CHECK_TRUE(f.ability->equipment(1001).error().code==ayt::ability::Error::Missing);
    CHECK_FALSE(f.ability->fireAt(f.source,f.target).has_value());
}
TEST_CASE(five_derived_stages_share_cast_but_keep_emitter_and_hit_identities) {
    WorldFixture f;
    auto doubled=ayt::stats::CalculationPlan::compile({
        {.id=1,.op=ayt::stats::Op::Input,.binding=0},
        {.id=2,.op=ayt::stats::Op::Constant,.value=20000},
        {.id=3,.op=ayt::stats::Op::MultiplyRate,.a=1,.b=2}},3,1).value();
    ayt::stats::Recipe recipe{"double_stage",ayt::stats::statId("health"),
        ayt::stats::AttributeOp::Decrease,std::move(doubled),
        {{ayt::stats::InputSource::Amount}}};
    CHECK_TRUE(f.combat->attributes().setRecipes(
        ayt::stats::RecipeBook::compile({std::move(recipe)}).value()).has_value());
    CHECK_TRUE(f.ability->addEquipment(f.source,1001,101).has_value());
    CHECK_TRUE(f.ability->equip(f.source,1001).has_value());
    CHECK_TRUE(f.ability->grant(f.source,404,1).has_value());
    DerivedDefinition definition;
    definition.sourceLoss=SourceLossPolicy::ContinueFromSnapshot;
    definition.stages={
        {.id=1,.kind=DeliveryKind::Contact,.amountParameter=2,.lifetimeTicks=20},
        {.id=2,.kind=DeliveryKind::Explosion,.amount=5,.recipe="double_stage",.lifetimeTicks=20},
        {.id=3,.kind=DeliveryKind::Fragment,.emitters=3,.amount=7,.lifetimeTicks=20},
        {.id=4,.kind=DeliveryKind::Return,.amount=11,.lifetimeTicks=20},
        {.id=5,.kind=DeliveryKind::Hazard,.amount=13,.lifetimeTicks=20}};
    CHECK_TRUE(f.ability->configureDerivedSkill(404,definition).has_value());
    auto cast=f.ability->castDerived(f.source,404).value();
    CHECK_TRUE(cast.emissions.size()==7 && cast.cast.execution!=0);
    CHECK_TRUE(f.stats(f.source).read(ayt::stats::statId("mana"))->current==70);
    CHECK_TRUE(cast.emissions.front().source->equipment->object==1001);
    CHECK_TRUE(cast.emissions.front().source->skillLevel==1);
    CHECK_TRUE(f.ability->setLevel(f.source,404,3).has_value());
    std::set<std::uint64_t> attacks;
    auto impact=[&](std::size_t emitter)->ayt::ability::DerivedImpactResult{
        auto result=f.ability->reportDerivedHit(cast.emissions[emitter].emitter,f.target).value();
        CHECK_TRUE(result.settlement.source.abilityInstance==cast.cast.execution);
        CHECK_TRUE(result.settlement.source.effect==cast.emissions[emitter].stage);
        CHECK_TRUE(result.settlement.source.directSource->object==cast.emissions[emitter].emitter.object);
        CHECK_TRUE(result.source->equipment->object==1001 && result.source->skillLevel==1);
        attacks.insert(result.settlement.attack.id);
        return result;
    };
    CHECK_TRUE(impact(0).hit.actual==3);
    CHECK_TRUE(impact(1).hit.actual==10);
    CHECK_TRUE(impact(2).hit.actual==7);
    CHECK_TRUE(f.ability->reportDerivedHit(cast.emissions[3].emitter,f.target).error().code==Error::Duplicate);
    CHECK_TRUE(f.ability->reportDerivedHit(cast.emissions[4].emitter,f.target).error().code==Error::Duplicate);
    auto* sourceEntity=f.world.getEntityByHandle(f.source);
    f.world.destroyEntity(sourceEntity);
    f.world.fixedUpdate(1.0f/60);
    CHECK_TRUE(impact(5).hit.actual==11);
    CHECK_TRUE(impact(6).hit.actual==13);
    CHECK_TRUE(attacks.size()==5);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==56);
    for(const auto& emission:cast.emissions)
        CHECK_TRUE(f.ability->finishDerivedEmitter(emission.emitter).has_value());
    CHECK_TRUE(f.combat->hits().activeAttacks()==0);
    CHECK_FALSE(f.ability->reportDerivedHit(cast.emissions[0].emitter,f.target).has_value());
}
TEST_CASE(derived_cancel_policy_closes_delayed_hits_on_source_loss) {
    WorldFixture f;
    CHECK_TRUE(f.ability->grant(f.source,404,1).has_value());
    DerivedDefinition definition;
    definition.stages={{.id=1,.amount=9,.lifetimeTicks=20}};
    CHECK_TRUE(f.ability->configureDerivedSkill(404,definition).has_value());
    auto cast=f.ability->castDerived(f.source,404).value();
    CHECK_TRUE(f.combat->hits().activeAttacks()==1);
    const std::array death{ayt::stats::Mutation{ayt::stats::statId("health"),
        ayt::stats::MutationOp::Decrease,100}};
    CHECK_TRUE(f.stats(f.source).commit(death,f.stats(f.source).version()).has_value());
    CHECK_TRUE(f.ability->reportDerivedHit(cast.emissions[0].emitter,f.target).error().code==Error::Stale);
    f.world.fixedUpdate(1.0f/60);
    CHECK_TRUE(f.combat->hits().activeAttacks()==0);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==100);
}
TEST_CASE(derived_cost_failure_and_expiry_leave_no_open_hit_windows) {
    WorldFixture f;
    CHECK_TRUE(f.ability->grant(f.source,404,1).has_value());
    DerivedDefinition definition;
    definition.stages={{.id=1,.amount=9,.lifetimeTicks=1}};
    CHECK_TRUE(f.ability->configureDerivedSkill(404,definition).has_value());
    const std::array empty{ayt::stats::Mutation{ayt::stats::statId("mana"),
        ayt::stats::MutationOp::SetCurrent,0}};
    CHECK_TRUE(f.stats(f.source).commit(empty,f.stats(f.source).version()).has_value());
    CHECK_TRUE(f.ability->castDerived(f.source,404).error().code==Error::Empty);
    CHECK_TRUE(f.combat->hits().activeAttacks()==0);
    CHECK_TRUE(f.ability->skill(f.source,404)->nextReadyTick==0);
    const std::array refill{ayt::stats::Mutation{ayt::stats::statId("mana"),
        ayt::stats::MutationOp::SetCurrent,80}};
    CHECK_TRUE(f.stats(f.source).commit(refill,f.stats(f.source).version()).has_value());
    auto cast=f.ability->castDerived(f.source,404).value();
    CHECK_TRUE(cast.cast.execution==1 && f.combat->hits().activeAttacks()==1);
    f.world.fixedUpdate(1.0f/60);
    CHECK_TRUE(f.combat->hits().activeAttacks()==0);
    CHECK_FALSE(f.ability->reportDerivedHit(cast.emissions[0].emitter,f.target).has_value());
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==100);
}
TEST_CASE(weapon_fire_and_skill_stages_use_one_release_timeline) {
    WorldFixture f;
    CHECK_TRUE(f.ability->addEquipment(f.source,1001,101).has_value());
    CHECK_TRUE(f.ability->equip(f.source,1001).has_value());
    CHECK_TRUE(f.ability->grant(f.source,202,3).has_value());
    DerivedDefinition definition;
    definition.stages={
        {.id=1,.kind=DeliveryKind::Contact,.amountParameter=fire::Amount,
            .lifetimeTicks=3,.emittersParameter=fire::Projectiles},
        {.id=2,.kind=DeliveryKind::Explosion,.amount=5,.lifetimeTicks=2,
            .trigger=StageTrigger::FirstConfirmedHit,.predecessor=1,.delayTicks=1},
        {.id=3,.kind=DeliveryKind::Hazard,.amount=7,.lifetimeTicks=2,
            .trigger=StageTrigger::StageFinished,.predecessor=2}};
    CHECK_TRUE(f.ability->configureFireDelivery(101,definition).has_value());
    auto fired=f.ability->fireDelivered(f.source,202).value();
    CHECK_TRUE(fired.shot.projectiles==3 && fired.emissions.size()==3);
    CHECK_TRUE(fired.shot.ammunitionRemaining==5 && f.ability->skill(f.source,202)->nextReadyTick==8);
    CHECK_TRUE(f.stats(f.source).read(ayt::stats::statId("mana"))->current==75);
    auto initial=f.ability->takeEmissions();
    CHECK_TRUE(initial.size()==3 && initial[0].stage==1);
    CHECK_TRUE(f.ability->reportDerivedHit(initial[0].emitter,f.target,0,0,
        std::array<float,3>{5.0f,6.0f,0.0f})->hit.actual==10);
    CHECK_TRUE(f.ability->reportDerivedHit(initial[1].emitter,f.target).error().code==Error::Duplicate);
    CHECK_TRUE(f.ability->takeEmissions().empty());
    f.world.fixedUpdate(1.0f/60);
    auto explosion=f.ability->takeEmissions();
    CHECK_TRUE(explosion.size()==1 && explosion[0].stage==2 && explosion[0].tick==1);
    CHECK_TRUE(explosion[0].origin && (*explosion[0].origin)[0]==5.0f);
    CHECK_TRUE(explosion[0].source->execution==fired.shot.execution);
    CHECK_TRUE(f.ability->reportDerivedHit(explosion[0].emitter,f.target)->hit.actual==5);
    CHECK_TRUE(f.ability->finishDerivedEmitter(explosion[0].emitter).has_value());
    auto hazard=f.ability->takeEmissions();
    CHECK_TRUE(hazard.size()==1 && hazard[0].stage==3 && hazard[0].tick==1);
    CHECK_TRUE(hazard[0].origin && (*hazard[0].origin)[1]==6.0f);
    CHECK_TRUE(f.ability->reportDerivedHit(hazard[0].emitter,f.target)->hit.actual==7);
    CHECK_TRUE(f.ability->finishDerivedEmitter(hazard[0].emitter).has_value());
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==78);
    f.world.fixedUpdate(1.0f/60);f.world.fixedUpdate(1.0f/60);
    CHECK_TRUE(f.combat->hits().activeAttacks()==0);
}
TEST_CASE(delivery_stages_route_attributes_and_owned_effects) {
    WorldFixture f;
    CHECK_TRUE(f.ability->grant(f.source,404,1).has_value());
    ayt::combat::EffectDefinition emptyEffect{};
    emptyEffect.type=701;emptyEffect.key=701;emptyEffect.durationTicks=3;
    CHECK_FALSE(f.ability->configureOwnedEffect(701,emptyEffect).has_value());
    ayt::combat::EffectDefinition effect{};
    effect.type=700;effect.key=700;effect.durationTicks=3;effect.tags={99};
    CHECK_TRUE(f.ability->configureOwnedEffect(700,effect).has_value());
    DerivedDefinition definition;
    definition.stages={
        {.id=1,.amount=3,.lifetimeTicks=3},
        {.id=2,.amount=4,.lifetimeTicks=3,.operation=StageOperation::Heal,
            .intent=StageIntent::Beneficial},
        {.id=3,.amount=5,.lifetimeTicks=3,.operation=StageOperation::DecreaseAttribute,
            .attribute="mana"},
        {.id=4,.amount=3,.lifetimeTicks=3,.operation=StageOperation::IncreaseAttribute,
            .intent=StageIntent::Beneficial,.attribute="mana"},
        {.id=5,.amount=20,.lifetimeTicks=3,.operation=StageOperation::SetAttribute,
            .intent=StageIntent::Beneficial,.attribute="mana"},
        {.id=6,.lifetimeTicks=3,.operation=StageOperation::OwnedEffect,.effect=700}};
    CHECK_TRUE(f.ability->configureDerivedSkill(404,definition).has_value());
    auto cast=f.ability->castDerived(f.source,404).value();
    CHECK_TRUE(cast.emissions.size()==6);
    auto hit=[&](std::size_t index) {
        auto result=f.ability->reportDerivedHit(cast.emissions[index].emitter,f.target);
        if(!result)throw std::runtime_error("stage "+std::to_string(index+1)+": "+result.error().message);
        return *result;
    };
    CHECK_TRUE(hit(0).hit.actual==3);
    CHECK_TRUE(hit(1).hit.actual==3);
    CHECK_TRUE(hit(2).hit.actual==5);
    CHECK_TRUE(hit(3).hit.actual==3);
    auto set=hit(4);
    CHECK_TRUE(set.interaction && set.interaction->decision.allowed);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("health"))->current==100);
    CHECK_TRUE(f.stats(f.target).read(ayt::stats::statId("mana"))->current==20);
    CHECK_TRUE(f.combat->setRelation(1,2,ayt::combat::Relation::Friendly).has_value());
    auto denied=hit(5);
    CHECK_TRUE(denied.interaction && !denied.interaction->decision.allowed);
    CHECK_TRUE(f.combat->setRelation(1,2,ayt::combat::Relation::Hostile).has_value());
    auto applied=hit(5);
    CHECK_TRUE(applied.interaction && applied.interaction->decision.allowed);
    CHECK_TRUE(f.combat->hasTag(f.target,99).value());
    auto duplicate=f.ability->reportDerivedHit(cast.emissions[5].emitter,f.target);
    CHECK_TRUE(!duplicate && duplicate.error().code==Error::Duplicate);
    for(int i=0;i<4;++i)f.world.fixedUpdate(1.0f/60);
    CHECK_FALSE(f.combat->hasTag(f.target,99).value());
}
TEST_CASE(derived_scalar_attributes_use_authored_intent_and_source_snapshot) {
    WorldFixture f;
    CHECK_TRUE(f.ability->grant(f.source,404,1).has_value());
    DerivedDefinition definition;
    definition.sourceLoss=SourceLossPolicy::ContinueFromSnapshot;
    definition.stages={
        {.id=1,.amount=5,.lifetimeTicks=3,.operation=StageOperation::IncreaseAttribute,
            .intent=StageIntent::Harmful,.attribute="health_max",
            .allowedTargets=ayt::combat::mask(ayt::combat::AllowedTarget::Hostile)},
        {.id=2,.amount=3,.lifetimeTicks=3,.operation=StageOperation::DecreaseAttribute,
            .intent=StageIntent::Beneficial,.attribute="health_max",
            .allowedTargets=ayt::combat::mask(ayt::combat::AllowedTarget::Friendly)},
        {.id=3,.amount=2,.lifetimeTicks=3,.operation=StageOperation::IncreaseAttribute,
            .intent=StageIntent::Harmful,.attribute="health_max",
            .allowedTargets=ayt::combat::mask(ayt::combat::AllowedTarget::Hostile)}};
    CHECK_TRUE(f.ability->configureDerivedSkill(404,definition).has_value());
    auto cast=f.ability->castDerived(f.source,404).value();
    auto* target=f.world.getEntityByHandle(f.target)->getComponent<ayt::combat::CombatComponent>();
    target->capabilities=ayt::combat::mask(ayt::combat::Capability::Damageable);
    auto harmful=f.ability->reportDerivedHit(cast.emissions[0].emitter,f.target);
    CHECK_TRUE(harmful && harmful->interaction && harmful->interaction->decision.allowed);
    CHECK_INT_EQ(f.stats(f.target).read(ayt::stats::statId("health_max"))->base,105);
    CHECK_TRUE(f.combat->setRelation(1,2,ayt::combat::Relation::Friendly).has_value());
    target->capabilities=ayt::combat::mask(ayt::combat::Capability::Healable);
    auto beneficial=f.ability->reportDerivedHit(cast.emissions[1].emitter,f.target);
    CHECK_TRUE(beneficial && beneficial->interaction && beneficial->interaction->decision.allowed);
    CHECK_INT_EQ(f.stats(f.target).read(ayt::stats::statId("health_max"))->base,102);
    CHECK_TRUE(f.combat->setRelation(1,2,ayt::combat::Relation::Hostile).has_value());
    target->capabilities=ayt::combat::mask(ayt::combat::Capability::Damageable);
    f.world.destroyEntity(f.world.getEntityByHandle(f.source));
    auto snapshot=f.ability->reportDerivedHit(cast.emissions[2].emitter,f.target);
    CHECK_TRUE(snapshot && snapshot->interaction && snapshot->interaction->decision.allowed);
    CHECK_INT_EQ(f.stats(f.target).read(ayt::stats::statId("health_max"))->base,104);
}
TEST_SUITE_END
