#include <AYTest.h>
#include <AYAbility/FixedActionTimeline.h>

using namespace ayt::ability;
namespace {
FixedActionTimeline timeline() {
    FixedActionDefinition attack;
    attack.id=10;
    attack.windupTicks=1;
    attack.activeTicks=2;
    attack.recoveryTicks=1;
    attack.inputBufferTicks=2;
    attack.markers={{2,12},{1,11}};
    FixedActionDefinition dodge;
    dodge.id=20;
    dodge.activeTicks=2;
    return FixedActionTimeline::compile({attack,dodge}).value();
}
}

TEST_SUITE(FixedActionTimelineTests)
TEST_CASE(compilation_rejects_bad_durations_duplicate_ids_and_outside_markers) {
    CHECK_TRUE(FixedActionTimeline::compile({}).error().code==Error::Invalid);
    CHECK_TRUE(FixedActionTimeline::compile({{.id=1,.activeTicks=0}})
        .error().code==Error::Invalid);
    CHECK_TRUE(FixedActionTimeline::compile({{.id=1},{.id=1}})
        .error().code==Error::Duplicate);
    FixedActionDefinition bad;
    bad.id=1;
    bad.markers={{1,7}};
    CHECK_TRUE(FixedActionTimeline::compile({bad}).error().code==Error::Invalid);
}
TEST_CASE(phase_markers_and_buffered_input_follow_exact_fixed_ticks) {
    auto actions=timeline();
    CHECK_TRUE(actions.canStart(10,1).has_value());
    CHECK_TRUE(actions.start(10,91,1).has_value());
    CHECK_TRUE(actions.canStart(20,1).error().code==Error::Busy);
    CHECK_TRUE(actions.buffer(20,1).error().code==Error::Busy);
    const auto windup=actions.step(1).value();
    CHECK_TRUE(windup.phase==FixedActionPhase::Windup);
    CHECK_TRUE(windup.cues.size()==1 &&
        windup.cues.front().kind==FixedActionCueKind::PhaseEntered);
    const auto active=actions.step(2).value();
    CHECK_TRUE(active.phase==FixedActionPhase::Active);
    CHECK_TRUE(active.cues.size()==2 && active.cues[1].marker==11);
    CHECK_TRUE(actions.start(20,92,3).error().code==Error::Busy);
    CHECK_TRUE(actions.buffer(20,3).has_value());
    CHECK_TRUE(actions.buffer(10,3).error().code==Error::Busy);
    const auto activeEnd=actions.step(3).value();
    CHECK_TRUE(activeEnd.phase==FixedActionPhase::Active);
    CHECK_TRUE(activeEnd.cues.size()==1 && activeEnd.cues[0].marker==12);
    const auto recovery=actions.step(4).value();
    CHECK_TRUE(recovery.phase==FixedActionPhase::Recovery);
    CHECK_TRUE(recovery.cues.size()==2 &&
        recovery.cues.back().kind==FixedActionCueKind::Finished);
    CHECK_TRUE(recovery.readyBufferedAction==20);
    CHECK_FALSE(actions.running());
    CHECK_TRUE(actions.canStart(20,5).has_value());
    CHECK_TRUE(actions.start(20,92,5).has_value());
    CHECK_TRUE(actions.step(5)->phase==FixedActionPhase::Active);
    CHECK_TRUE(actions.step(6)->cues.back().kind==FixedActionCueKind::Finished);
}
TEST_CASE(interrupt_drops_buffer_and_prevents_later_phase_events) {
    auto actions=timeline();
    CHECK_TRUE(actions.start(10,91,1).has_value());
    CHECK_TRUE(actions.step(1).has_value());
    CHECK_TRUE(actions.step(2).has_value());
    CHECK_TRUE(actions.buffer(20,3).has_value());
    const auto cue=actions.cancel(3,true).value();
    CHECK_TRUE(cue.kind==FixedActionCueKind::Interrupted && cue.execution==91);
    CHECK_TRUE(actions.step(3)->phase==FixedActionPhase::Idle);
    CHECK_TRUE(actions.step(4)->readyBufferedAction==0);
    CHECK_TRUE(actions.cancel(5).error().code==Error::Missing);
}
TEST_CASE(snapshot_restores_pending_and_running_action_but_rejects_stale_content) {
    auto actions=timeline();
    CHECK_TRUE(actions.start(10,91,1).has_value());
    const auto pending=actions.checkpoint();
    auto restored=timeline();
    CHECK_TRUE(restored.restore(pending).has_value());
    CHECK_TRUE(restored.step(1)->phase==FixedActionPhase::Windup);
    CHECK_TRUE(actions.step(1).has_value());
    CHECK_TRUE(actions.step(2).has_value());
    CHECK_TRUE(actions.buffer(20,3).has_value());
    const auto buffered=actions.checkpoint();
    CHECK_TRUE(restored.restore(buffered).has_value());
    CHECK_TRUE(restored.step(3)->cues.back().marker==12);
    const auto mid=restored.checkpoint();
    auto again=timeline();
    CHECK_TRUE(again.restore(mid).has_value());
    CHECK_TRUE(again.step(4)->readyBufferedAction==20);
    auto invalid=mid;
    invalid.definitionFingerprint^=1;
    const auto before=again.checkpoint();
    CHECK_TRUE(again.restore(invalid).error().code==Error::Invalid);
    CHECK_TRUE(again.checkpoint().lastTick==before.lastTick);
    invalid=mid;
    invalid.lastPhase=FixedActionPhase::Recovery;
    CHECK_TRUE(again.restore(invalid).error().code==Error::Invalid);
    invalid=mid;
    invalid.bufferedTick=1;
    CHECK_TRUE(again.restore(invalid).error().code==Error::Invalid);
}
TEST_CASE(skipped_repeated_and_late_ticks_are_rejected_without_mutation) {
    auto actions=timeline();
    CHECK_TRUE(actions.start(10,91,2).error().code==Error::Invalid);
    CHECK_TRUE(actions.start(10,91,1).has_value());
    CHECK_TRUE(actions.step(2).error().code==Error::Stale);
    CHECK_TRUE(actions.step(1)->phase==FixedActionPhase::Windup);
    CHECK_TRUE(actions.step(1).error().code==Error::Stale);
    CHECK_TRUE(actions.cancel(3).error().code==Error::Stale);
    CHECK_TRUE(actions.cancel(2).has_value());
    CHECK_TRUE(actions.step(2)->phase==FixedActionPhase::Idle);
}
TEST_SUITE_END
