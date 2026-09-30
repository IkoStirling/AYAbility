#include <AYTest.h>
#include "Fixtures.h"
#include <array>

TEST_SUITE(AbilityDefinitionTests)
TEST_CASE(equipment_and_skill_layers_are_distinct) {
    auto catalog=fixture::catalog();
    const std::array levels{SkillLevel{201,3},SkillLevel{202,2}};
    auto pistol=catalog->resolve(10,101,levels,202).value();
    auto automatic=catalog->resolve(10,102,levels,202).value();
    CHECK_TRUE(pistol.value(fire::Amount,Unit::Amount).value()==14);
    CHECK_TRUE(automatic.value(fire::Amount,Unit::Amount).value()==14);
    CHECK_TRUE(pistol.value(fire::IntervalTicks,Unit::Ticks).value()==4);
    CHECK_TRUE(automatic.value(fire::IntervalTicks,Unit::Ticks).value()==3);
    CHECK_TRUE(pistol.value(fire::Projectiles,Unit::Count).value()==2);
    CHECK_TRUE(pistol.cost && pistol.cost->first=="mana" && pistol.cost->second==5);
    CHECK_TRUE(pistol.hasFeature(900) && pistol.hasFeature(901));
    CHECK_TRUE(!catalog->resolve(10,101,levels).value().hasFeature(901));
}
TEST_CASE(levels_resolve_from_table_and_curve_without_mutating_definitions) {
    auto catalog=fixture::catalog();
    const std::array before{SkillLevel{201,1},SkillLevel{202,1}};
    const std::array after{SkillLevel{201,3},SkillLevel{202,3}};
    auto old=catalog->resolve(10,101,before,202).value();
    auto upgraded=catalog->resolve(10,101,after,202).value();
    CHECK_TRUE(old.value(fire::Amount,Unit::Amount).value()==10);
    CHECK_TRUE(old.value(fire::Projectiles,Unit::Count).value()==1);
    CHECK_TRUE(upgraded.value(fire::Amount,Unit::Amount).value()==14);
    CHECK_TRUE(upgraded.value(fire::Projectiles,Unit::Count).value()==3);
    CHECK_TRUE(old.value(fire::Amount,Unit::Amount).value()==10);
}
TEST_CASE(invalid_content_rejects_missing_parameters_and_bad_growth) {
    ActionDefinition action{.id=1,.behaviorId=1,.defaults={{1,Unit::Amount,10}}};
    EquipmentActionConfig badEquipment{.id=2,.action=1,.parameters={fixture::replace(4,1)}};
    CHECK_TRUE(Catalog::compile({action},{badEquipment},{}).error().code==Error::Missing);
    SkillDefinition badTable{.id=3,.action=1,.maxLevel=3,
        .parameters={{1,PatchOp::Replace,{.mode=TrackMode::Table,.table={1,2}}}}};
    CHECK_TRUE(Catalog::compile({action},{},{badTable}).error().code==Error::Invalid);
    SkillDefinition badCurve{.id=3,.action=1,.maxLevel=3,
        .parameters={{1,PatchOp::Replace,{.mode=TrackMode::Curve,.curve={{1,2},{1,3},{3,4}}}}}};
    CHECK_TRUE(Catalog::compile({action},{},{badCurve}).error().code==Error::Invalid);
}
TEST_CASE(duplicate_skill_and_locked_feature_are_rejected) {
    auto catalog=fixture::catalog();
    const std::array duplicate{SkillLevel{201,1},SkillLevel{201,2}};
    CHECK_TRUE(catalog->resolve(10,101,duplicate).error().code==Error::Duplicate);
    const std::array passiveOnly{SkillLevel{201,2}};
    CHECK_TRUE(catalog->resolve(10,101,passiveOnly,202).error().code==Error::Missing);
    CHECK_TRUE(!catalog->resolve(10,101,passiveOnly).value().hasFeature(900));
}
TEST_SUITE_END
