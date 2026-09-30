#include <AYTest.h>
#include "Fixtures.h"

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
TEST_SUITE_END
