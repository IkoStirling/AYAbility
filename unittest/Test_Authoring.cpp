#include <AYTest.h>
#include <AYAbility/Authoring.h>

using namespace ayt::ability;
TEST_SUITE(AbilityAuthoringTests)
TEST_CASE(versioned_document_roundtrips_and_validates_references) {
    AuthoringDocument authored;
    authored.actions={
        {.id=10,.behaviorId=fire::Behavior,.defaults={
            {fire::Amount,Unit::Amount,10},{fire::IntervalTicks,Unit::Ticks,4},
            {fire::MinimumIntervalTicks,Unit::Ticks,2},
            {fire::IntervalStepTicks,Unit::Ticks,1},
            {fire::StreakResetTicks,Unit::Ticks,4},
            {fire::MagazineCapacity,Unit::Count,6},
            {fire::ReloadTicks,Unit::Ticks,3},
            {fire::Projectiles,Unit::Count,1},
            {fire::SkillCooldownTicks,Unit::Ticks,0}}},
        {.id=20,.behaviorId=derived::Behavior,.defaults={
            {derived::CooldownTicks,Unit::Ticks,6},{55,Unit::Amount,4}}}};
    authored.equipment={{.id=101,.action=10}};
    authored.skills={{.id=202,.action=20,.kind=SkillKind::Active,.maxLevel=3,
        .parameters={{55,PatchOp::Replace,{.mode=TrackMode::Curve,
            .curve={{1,4},{3,10}}}}}}};
    authored.fireDeliveries={{101,{.stages={{.id=1,.amountParameter=fire::Amount,
        .emittersParameter=fire::Projectiles}}}}};
    authored.derivedSkills={{202,{.stages={{.id=1,.amountParameter=55}}}}};
    auto saved=saveAuthoring(authored);
    CHECK_TRUE(saved.has_value());
    auto loaded=loadAuthoring(*saved);
    CHECK_TRUE(loaded.has_value());
    CHECK_TRUE(loaded->actions.size()==2 && loaded->derivedSkills.size()==1);
    CHECK_TRUE(loaded->skills[0].parameters[0].values.curve[1].value==10);
    auto savedAgain=saveAuthoring(*loaded);
    CHECK_TRUE(savedAgain && *savedAgain==*saved);
    loaded->version=2;
    CHECK_FALSE(compileAuthoring(*loaded).has_value());
    loaded->version=1;loaded->derivedSkills[0].owner=999;
    CHECK_FALSE(compileAuthoring(*loaded).has_value());
    loaded->derivedSkills[0].owner=202;
    loaded->derivedSkills[0].definition.stages.push_back({.id=2,
        .trigger=StageTrigger::FirstConfirmedHit,.predecessor=999});
    CHECK_FALSE(compileAuthoring(*loaded).has_value());
    loaded->derivedSkills[0].definition.stages.pop_back();
    loaded->derivedSkills[0].definition.stages[0].operation=StageOperation::OwnedEffect;
    loaded->derivedSkills[0].definition.stages[0].amountParameter=0;
    loaded->derivedSkills[0].definition.stages[0].effect=700;
    CHECK_FALSE(compileAuthoring(*loaded).has_value());
    loaded->effectReferences={700};
    CHECK_TRUE(compileAuthoring(*loaded).has_value());
    CHECK_FALSE(loadAuthoring(R"({"version":1,"actions":[{"id":"wrong"}]})").has_value());
}
TEST_SUITE_END
