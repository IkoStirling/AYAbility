#include <AYTest.h>
#include <AYAbility/EntityIntegration.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include "Fixtures.h"
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
TEST_SUITE_END
