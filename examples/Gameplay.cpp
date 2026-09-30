#include <AYAbility/EntityIntegration.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <iostream>
#include <stdexcept>
using namespace ayt::ability;
template<class T> T require(Result<T> result) {
    if(!result)throw std::runtime_error(result.error().message);return std::move(*result);
}
void require(Result<void> result){if(!result)throw std::runtime_error(result.error().message);}
int main() {
    auto& world=ayt::entity::World::processWorld();
    try {
        world.initialize();auto& registry=ayt::entity::ComponentRegistry::instance();
        (void)ayt::entity::registerEntityCoreComponents(registry);
        require(registerAbilityGameplayComponents(registry));registry.seal();
        ActionDefinition fireAction{.id=10,.behaviorId=fire::Behavior,.defaults={
            {fire::Amount,Unit::Amount,10},{fire::IntervalTicks,Unit::Ticks,4},
            {fire::MinimumIntervalTicks,Unit::Ticks,2},{fire::IntervalStepTicks,Unit::Ticks,1},
            {fire::StreakResetTicks,Unit::Ticks,4},{fire::MagazineCapacity,Unit::Count,6},
            {fire::ReloadTicks,Unit::Ticks,3},{fire::Projectiles,Unit::Count,1},
            {fire::SkillCooldownTicks,Unit::Ticks,0}}};
        EquipmentActionConfig pistol{.id=101,.action=10};
        EquipmentActionConfig automatic{.id=102,.action=10,.parameters={
            {fire::MagazineCapacity,PatchOp::Replace,{.constant=12}},
            {fire::IntervalTicks,PatchOp::Replace,{.constant=3}}}};
        SkillDefinition mastery{.id=201,.action=10,.kind=SkillKind::Passive,.maxLevel=3,
            .parameters={{fire::Amount,PatchOp::Add,{.mode=TrackMode::Table,.table={0,2,4}}}}};
        auto catalog=require(Catalog::compile({fireAction},{pistol,automatic},{mastery}));
        auto& ability=*require(installAbility(world,catalog));
        auto* combat=dynamic_cast<ayt::combat::CombatSystem*>(world.findSystemByName("CombatSystem"));
        auto* actorEntity=world.createEntity();auto* targetEntity=world.createEntity();
        auto actor=world.getEntityHandle(actorEntity->getId()),target=world.getEntityHandle(targetEntity->getId());
        auto schema=ayt::stats::Schema::compile({{.name="health",.shape=ayt::stats::Shape::Pool,.current=100,.capacity=100}}).value();
        ayt::combat::ActorProfile profile{schema,{ayt::stats::statId("health")},1};
        if(!combat->configureActor(actor,profile))throw std::runtime_error("actor profile");
        profile.team=2;if(!combat->configureActor(target,profile))throw std::runtime_error("target profile");
        if(!combat->setRelation(1,2,ayt::combat::Relation::Hostile))throw std::runtime_error("relation");
        require(ability.addEquipment(actor,1001,101));require(ability.addEquipment(actor,1002,102));
        require(ability.equip(actor,1001));require(ability.grant(actor,201,3));
        auto shot=require(ability.fireAt(actor,target));
        std::cout<<"pistol damage: "<<shot.hits[0].actual<<", ammo: "<<shot.shot.ammunitionRemaining<<"\n";
        require(ability.equip(actor,1002));world.fixedUpdate(1.0f/60);
        shot=require(ability.fireAt(actor,target));
        std::cout<<"automatic damage: "<<shot.hits[0].actual<<", ammo: "<<shot.shot.ammunitionRemaining<<"\n";
        world.shutdown();return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<"\n";world.shutdown();return 1;}
}
