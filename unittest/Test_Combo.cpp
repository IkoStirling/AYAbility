#include <AYTest.h>
#include <AYAbility/Combo.h>

using namespace ayt::ability;
TEST_SUITE(AbilityComboTests)
TEST_CASE(accepted_releases_match_suffix_with_fixed_tick_gap_and_actor_isolation) {
    auto tracker=ComboTracker::compile({{10,{{101,0},{202,3},{303,2}}},
        {20,{{202,0},{303,2}}}}).value();
    const Identity first{1,1,1},second{1,2,1};
    CHECK_TRUE(tracker.accept(first,101,1).empty());
    CHECK_TRUE(tracker.accept(first,202,4).empty());
    auto matched=tracker.accept(first,303,6);
    CHECK_TRUE(matched.size()==2 && matched[0].combo==10 && matched[1].combo==20);
    CHECK_TRUE(tracker.accept(second,303,6).empty());
    CHECK_TRUE(tracker.accept(first,202,10).empty());
    CHECK_TRUE(tracker.accept(first,303,13).empty());
    CHECK_TRUE(tracker.accept(first,202,14).empty());
    CHECK_TRUE(tracker.accept(first,303,15).size()==1);
    tracker.removeActor(first);
    CHECK_TRUE(tracker.accept(first,303,16).empty());
}
TEST_CASE(invalid_combo_definitions_reject_before_runtime_creation) {
    CHECK_FALSE(ComboTracker::compile({{1,{{101,0}}}}).has_value());
    CHECK_FALSE(ComboTracker::compile({{1,{{101,0},{202,0}}}}).has_value());
    CHECK_FALSE(ComboTracker::compile({{1,{{101,0},{202,1}}},
        {1,{{202,0},{303,1}}}}).has_value());
}
TEST_SUITE_END
