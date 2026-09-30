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
    ActionDefinition areaAction{.id=20,.behaviorId=area::Behavior,.defaults={
        {area::CooldownTicks,Unit::Ticks,6}}};
    ActionDefinition derivedAction{.id=30,.behaviorId=derived::Behavior,.defaults={
        {derived::CooldownTicks,Unit::Ticks,6},{2,Unit::Amount,3}}};
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
    SkillDefinition aura{.id=303,.action=20,.kind=SkillKind::Active,.maxLevel=3,
        .parameters={{area::CooldownTicks,PatchOp::Replace,
            {.mode=TrackMode::Table,.table={6,5,4}}}},
        .cost=SkillCost{"mana",constant(10)}};
    SkillDefinition derivedSkill{.id=404,.action=30,.kind=SkillKind::Active,.maxLevel=3,
        .parameters={{2,PatchOp::Replace,{.mode=TrackMode::Table,.table={3,4,5}}}},
        .cost=SkillCost{"mana",constant(10)}};
    return Catalog::compile({fireAction,areaAction,derivedAction},
        {pistolConfig,automaticConfig},{mastery,burst,aura,derivedSkill}).value();
}
inline Runtime runtime(){return Runtime(catalog());}
}
