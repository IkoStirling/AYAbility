#include <AYTest.h>
#include <AYAbility/Derived.h>
#include "Fixtures.h"
#include <stdexcept>

TEST_SUITE(AbilityRuntimeTests)
TEST_CASE(shared_fire_behavior_preserves_each_weapon_state) {
    auto runtime=fixture::runtime();
    CHECK_TRUE(runtime.addEquipment(fixture::actor,fixture::pistol,101).has_value());
    CHECK_TRUE(runtime.addEquipment(fixture::actor,fixture::automatic,102).has_value());
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::pistol).has_value());
    auto first=runtime.fire({fixture::actor,1}).value();
    CHECK_TRUE(first.amount==10 && first.ammunitionRemaining==5 && first.nextFireTick==5);
    CHECK_TRUE(runtime.fire({fixture::actor,2}).error().code==Error::Cooldown);
    auto second=runtime.fire({fixture::actor,5}).value();
    CHECK_TRUE(second.streak==2 && second.nextFireTick==8 && second.ammunitionRemaining==4);
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::automatic).has_value());
    auto other=runtime.fire({fixture::actor,6}).value();
    CHECK_TRUE(other.ammunitionRemaining==11 && other.nextFireTick==9);
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::pistol).has_value());
    CHECK_TRUE(runtime.equipment(fixture::pistol)->ammunition==4);
    CHECK_TRUE(runtime.fire({fixture::actor,10})->streak==1);
}
TEST_CASE(failed_precommit_keeps_ammo_cooldown_and_execution_identity) {
    auto runtime=fixture::runtime();
    CHECK_TRUE(runtime.addEquipment(fixture::actor,fixture::pistol,101).has_value());
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::pistol).has_value());
    auto denied=runtime.fire({fixture::actor,1},[](const FireEvent&)->Result<void>{
        return std::unexpected(Diagnostic{Error::Empty,"no resource",{}});
    });
    CHECK_TRUE(denied.error().code==Error::Empty);
    CHECK_TRUE(runtime.equipment(fixture::pistol)->ammunition==6);
    auto fired=runtime.fire({fixture::actor,1}).value();
    CHECK_TRUE(fired.execution==1 && fired.ammunitionRemaining==5);
}
TEST_CASE(skill_level_and_cooldown_are_character_owned) {
    auto runtime=fixture::runtime();
    CHECK_TRUE(runtime.addEquipment(fixture::actor,fixture::pistol,101).has_value());
    CHECK_TRUE(runtime.addEquipment(fixture::actor,fixture::automatic,102).has_value());
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::pistol).has_value());
    CHECK_TRUE(runtime.grant(fixture::actor,201,1).has_value());
    CHECK_TRUE(runtime.grant(fixture::actor,202,1).has_value());
    auto first=runtime.fire({fixture::actor,1,202}).value();
    CHECK_TRUE(first.amount==10 && first.projectiles==1);
    CHECK_TRUE(runtime.setLevel(fixture::actor,201,3).has_value());
    CHECK_TRUE(runtime.setLevel(fixture::actor,202,3).has_value());
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::automatic).has_value());
    CHECK_TRUE(runtime.fire({fixture::actor,6,202}).error().code==Error::Cooldown);
    auto second=runtime.fire({fixture::actor,9,202}).value();
    CHECK_TRUE(second.amount==14 && second.projectiles==3);
    CHECK_TRUE(first.amount==10 && first.projectiles==1);
    CHECK_TRUE(runtime.skill(fixture::actor,202)->nextReadyTick==17);
}
TEST_CASE(reload_and_reentrancy_have_explicit_boundaries) {
    auto runtime=fixture::runtime();
    CHECK_TRUE(runtime.addEquipment(fixture::actor,fixture::pistol,101).has_value());
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::pistol).has_value());
    CHECK_TRUE(runtime.fire({fixture::actor,1}).has_value());
    CHECK_TRUE(runtime.beginReload(fixture::actor,2).has_value());
    CHECK_TRUE(runtime.fire({fixture::actor,4}).error().code==Error::Busy);
    CHECK_TRUE(runtime.fire({fixture::actor,5})->ammunitionRemaining==5);
    auto inner=runtime.fire({fixture::actor,9},[&](const FireEvent&)->Result<void>{
        CHECK_TRUE(runtime.equip(fixture::actor,fixture::pistol).error().code==Error::Busy);
        return {};
    });
    CHECK_TRUE(inner.has_value());
}
TEST_CASE(area_skill_has_character_cooldown_and_does_not_change_weapon_resolution) {
    auto runtime=fixture::runtime();
    CHECK_TRUE(runtime.addEquipment(fixture::actor,fixture::pistol,101).has_value());
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::pistol).has_value());
    CHECK_TRUE(runtime.grant(fixture::actor,303,1).has_value());
    auto first=runtime.activate({fixture::actor,1,303}).value();
    CHECK_TRUE(first.activeSkill==303 && first.resolved.cost->second==10);
    CHECK_TRUE(runtime.skill(fixture::actor,303)->nextReadyTick==7);
    CHECK_TRUE(runtime.fire({fixture::actor,1}).has_value());
    CHECK_TRUE(runtime.activate({fixture::actor,2,303}).error().code==Error::Cooldown);
    CHECK_TRUE(runtime.setLevel(fixture::actor,303,3).has_value());
    auto denied=runtime.activate({fixture::actor,7,303},[](const ActivationEvent&)->Result<void>{
        return std::unexpected(Diagnostic{Error::Empty,"resource empty",{}});
    });
    CHECK_TRUE(denied.error().code==Error::Empty);
    CHECK_TRUE(runtime.skill(fixture::actor,303)->nextReadyTick==7);
    auto second=runtime.activate({fixture::actor,7,303}).value();
    CHECK_TRUE(second.execution==3 && runtime.skill(fixture::actor,303)->nextReadyTick==11);
}
TEST_CASE(derived_release_freezes_level_equipment_and_bounded_emitter_identity) {
    auto runtime=fixture::runtime();
    CHECK_TRUE(runtime.addEquipment(fixture::actor,fixture::pistol,101).has_value());
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::pistol).has_value());
    auto granted=runtime.grant(fixture::actor,404,1);
    if(!granted)throw std::runtime_error(granted.error().message);
    auto cast=runtime.activate({fixture::actor,1,404}).value();
    DerivedDefinition definition;
    definition.sourceLoss=SourceLossPolicy::ContinueFromSnapshot;
    definition.stages={{.id=1,.kind=DeliveryKind::Fragment,.emitters=3,
        .amountParameter=2,.lifetimeTicks=20}};
    auto released=makeDerivedRelease(definition,cast,*runtime.equipped(fixture::actor),
        runtime.skill(fixture::actor,404)->level,7,fixture::actor.domain,1).value();
    CHECK_TRUE(released.emissions.size()==3 && released.stages[0].amount==3);
    CHECK_TRUE(released.source->equipment==fixture::pistol && released.source->team==7);
    CHECK_TRUE(released.emissions[0].source==released.emissions[2].source);
    CHECK_TRUE(released.emissions[0].emitter!=released.emissions[1].emitter);
    CHECK_TRUE(runtime.setLevel(fixture::actor,404,3).has_value());
    CHECK_TRUE(runtime.equip(fixture::actor,fixture::pistol).has_value());
    CHECK_TRUE(released.source->skillLevel==1 && released.stages[0].amount==3);
    CHECK_TRUE(released.source->resolved.value(2,Unit::Amount).value()==3);
    definition.stages.push_back(definition.stages.front());
    CHECK_TRUE(validateDerived(definition).error().code==Error::Invalid);
}
TEST_SUITE_END
