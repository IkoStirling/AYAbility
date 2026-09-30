#pragma once
#include <AYAbility/Runtime.h>
using namespace ayt::ability;
namespace fixture {
inline Identity actor{1,10,1}, pistol{1,100,1}, automatic{1,200,1};
inline NumericTrack constant(Value value){return {.mode=TrackMode::Constant,.constant=value};}
inline ParameterPatch replace(Id id,Value value){return {id,PatchOp::Replace,constant(value)};}
inline std::shared_ptr<const Catalog> catalog() {
    ActionDefinition fireAction{.id=10,.behaviorId=fire::Behavior,.defaults={
        {fire::Amount,Unit::Amount,10},{fire::IntervalTicks,Unit::Ticks,4},
        {fire::MinimumIntervalTicks,Unit::Ticks,2},{fire::IntervalStepTicks,Unit::Ticks,1},
        {fire::StreakResetTicks,Unit::Ticks,4},{fire::MagazineCapacity,Unit::Count,6},
        {fire::ReloadTicks,Unit::Ticks,3},{fire::Projectiles,Unit::Count,1},
        {fire::SkillCooldownTicks,Unit::Ticks,0}}};
    EquipmentActionConfig pistolConfig{.id=101,.action=10};
    EquipmentActionConfig automaticConfig{.id=102,.action=10,.parameters={
        replace(fire::MagazineCapacity,12),replace(fire::IntervalTicks,3),
        replace(fire::MinimumIntervalTicks,1),replace(fire::IntervalStepTicks,0)}};
    SkillDefinition mastery{.id=201,.action=10,.kind=SkillKind::Passive,.maxLevel=3,
        .parameters={{fire::Amount,PatchOp::Add,{.mode=TrackMode::Table,.table={0,2,4}}}},
        .features={{900,0,0,3}}};
    SkillDefinition burst{.id=202,.action=10,.kind=SkillKind::Active,.maxLevel=3,
        .parameters={{fire::Projectiles,PatchOp::Replace,{.mode=TrackMode::Curve,
            .curve={{1,1},{3,3}}}},replace(fire::SkillCooldownTicks,8)},
        .features={{901,0,0,2}},.cost=SkillCost{"mana",constant(5)}};
    return Catalog::compile({fireAction},{pistolConfig,automaticConfig},{mastery,burst}).value();
}
inline Runtime runtime(){return Runtime(catalog());}
}
