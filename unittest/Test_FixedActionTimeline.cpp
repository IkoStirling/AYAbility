#include <AYTest.h>
#include <AYAbility/FixedActionTimeline.h>
#include <AYAbility/FixedActionDocument.h>

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
TEST_CASE(preview_reports_the_next_frame_without_advancing_the_clock) {
    auto actions = timeline();
    CHECK_TRUE(actions.start(10, 91, 1).has_value());
    const auto before = actions.checkpoint();
    const auto firstPreview = actions.previewStep(1);
    CHECK_TRUE(firstPreview.has_value());
    CHECK_TRUE(firstPreview->phase == FixedActionPhase::Windup
        && firstPreview->cues.size() == 1);
    CHECK_TRUE(actions.checkpoint().lastTick == before.lastTick
        && actions.checkpoint().action == before.action);
    const auto firstCommit = actions.step(1);
    CHECK_TRUE(firstCommit.has_value());
    CHECK_TRUE(firstCommit->phase == firstPreview->phase
        && firstCommit->cues.size() == firstPreview->cues.size());
    CHECK_TRUE(!actions.previewStep(1).has_value());
    CHECK_TRUE(actions.previewStep(2)->cues.back().marker == 11);
    CHECK_TRUE(actions.step(2)->cues.back().marker == 11);
    CHECK_TRUE(actions.step(3).has_value());
    const auto finalPreview = actions.previewStep(4);
    CHECK_TRUE(finalPreview.has_value()
        && finalPreview->cues.back().kind == FixedActionCueKind::Finished);
    CHECK_TRUE(actions.running() && actions.checkpoint().lastTick == 3);
    const auto finalCommit = actions.step(4);
    CHECK_TRUE(finalCommit.has_value()
        && finalCommit->cues.back().kind == FixedActionCueKind::Finished);
    CHECK_TRUE(!actions.running() && actions.checkpoint().lastTick == 4);
}
TEST_CASE(authored_document_roundtrips_and_compiles_to_stable_timeline) {
    FixedActionDocument authored;
    authored.actions = {{.id=30,.windupTicks=1,.activeTicks=2,
        .recoveryTicks=1,.inputBufferTicks=1,
        .markers={{2,9},{1,7}},
        .cancelWindows={{1,3,{20}}},
        .hitWindows={{1,3,4}}},
        {.id=20,.activeTicks=3}};
    const auto saved = saveFixedActionDocument(authored);
    CHECK_TRUE(saved.has_value());
    const auto loaded = loadFixedActionDocument(*saved);
    CHECK_TRUE(loaded.has_value());
    CHECK_TRUE(loaded->actions.size() == 2 && loaded->actions[0].id == 20);
    CHECK_TRUE(loaded->actions[1].markers[0].offset == 1);
    CHECK_TRUE(loaded->actions[1].cancelWindows.size() == 1
        && loaded->actions[1].cancelWindows[0].nextActions[0] == 20);
    CHECK_TRUE(loaded->actions[1].hitWindows.size() == 1
        && loaded->actions[1].hitWindows[0].id == 4);
    const auto savedAgain = saveFixedActionDocument(*loaded);
    CHECK_TRUE(savedAgain && *savedAgain == *saved);
    auto timeline = FixedActionTimeline::compile(loaded->actions);
    CHECK_TRUE(timeline.has_value());
    CHECK_TRUE(timeline->start(30,91,1).has_value());
    CHECK_TRUE(timeline->step(1)->phase == FixedActionPhase::Windup);
    CHECK_TRUE(timeline->step(2)->cues.back().marker == 7);
    auto invalid = *loaded;
    invalid.actions[1].markers.push_back({1,7});
    CHECK_FALSE(saveFixedActionDocument(invalid).has_value());
    CHECK_FALSE(loadFixedActionDocument(R"({"version":2,"actions":[]})").has_value());
    CHECK_FALSE(loadFixedActionDocument(R"({"version":1,"actions":[],"unknown":1})").has_value());
    auto invalidWindow = *loaded;
    invalidWindow.actions[1].cancelWindows[0].endOffset = 8;
    CHECK_FALSE(saveFixedActionDocument(invalidWindow).has_value());
    invalidWindow = *loaded;
    invalidWindow.actions[1].hitWindows[0].endOffset = 4;
    CHECK_FALSE(saveFixedActionDocument(invalidWindow).has_value());
    CHECK_FALSE(loadFixedActionDocument(R"({"version":1,"actions":[{"id":-1}]})").has_value());
    CHECK_FALSE(loadFixedActionDocument(R"({"version":1,"actions":[{"id":1,"windupTicks":0,"activeTicks":0,"recoveryTicks":0,"inputBufferTicks":0,"markers":[]}]})").has_value());
}
TEST_CASE(authored_hit_windows_cover_exact_active_ticks_and_guard_restore) {
    FixedActionDefinition attack;
    attack.id=10;
    attack.windupTicks=1;
    attack.activeTicks=3;
    attack.recoveryTicks=1;
    attack.hitWindows={{2,4,8},{1,3,7}};
    auto timeline=FixedActionTimeline::compile({attack}).value();
    CHECK_TRUE(timeline.start(10,41,1).has_value());
    const auto saved=timeline.checkpoint();
    CHECK_TRUE(timeline.step(1)->hitWindows.empty());
    const auto first=timeline.step(2).value();
    CHECK_TRUE(first.hitWindows.size()==1 && first.hitWindows[0].id==7
        && first.hitWindows[0].beginTick==2 && first.hitWindows[0].endTick==4);
    auto resumed=FixedActionTimeline::compile({attack}).value();
    CHECK_TRUE(resumed.restore(timeline.checkpoint()).has_value());
    const auto overlap=timeline.step(3).value();
    const auto resumedOverlap=resumed.step(3).value();
    CHECK_TRUE(overlap.hitWindows.size()==2 && overlap.hitWindows[0].id==7
        && overlap.hitWindows[1].id==8
        && overlap.hitWindows[1].beginTick==3
        && overlap.hitWindows[1].endTick==5);
    CHECK_TRUE(resumedOverlap.hitWindows.size()==2
        && resumedOverlap.hitWindows[0].beginTick==2
        && resumedOverlap.hitWindows[1].endTick==5);
    const auto finalActive=timeline.step(4).value();
    CHECK_TRUE(finalActive.hitWindows.size()==1
        && finalActive.hitWindows[0].id==8);
    CHECK_TRUE(timeline.step(5)->hitWindows.empty());

    auto changed=attack;
    changed.hitWindows[0].startOffset=1;
    auto different=FixedActionTimeline::compile({changed}).value();
    CHECK_TRUE(different.restore(saved).error().code==Error::Invalid);
    auto invalid=attack;
    invalid.hitWindows.push_back({1,2,7});
    CHECK_TRUE(FixedActionTimeline::compile({invalid}).error().code==Error::Duplicate);
    invalid=attack;
    invalid.hitWindows[0].endOffset=5;
    CHECK_TRUE(FixedActionTimeline::compile({invalid}).error().code==Error::Invalid);
    invalid=attack;
    invalid.hitWindows[0].startOffset=0;
    CHECK_TRUE(FixedActionTimeline::compile({invalid}).error().code==Error::Invalid);
}
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
    bad.activeTicks=2;
    bad.markers={{0,7},{0,8},{0,7}};
    CHECK_TRUE(FixedActionTimeline::compile({bad}).error().code==Error::Duplicate);
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
    invalid=mid;
    invalid.lastTick=4; // The last tick would already have emitted Finished.
    invalid.lastPhase=FixedActionPhase::Recovery;
    CHECK_TRUE(again.restore(invalid).error().code==Error::Invalid);
    CHECK_TRUE(again.checkpoint().lastTick==before.lastTick);
}
TEST_CASE(cancel_and_new_start_share_one_unprocessed_tick) {
    auto actions=timeline();
    CHECK_TRUE(actions.start(10,91,1).has_value());
    CHECK_TRUE(actions.step(1).has_value());
    const auto cue=actions.cancel(2,true).value();
    CHECK_TRUE(cue.kind==FixedActionCueKind::Interrupted && cue.tick==2);
    CHECK_TRUE(actions.canStart(20,2).has_value());
    CHECK_TRUE(actions.start(20,92,2).has_value());
    const auto frame=actions.step(2).value();
    CHECK_TRUE(frame.action==20 && frame.phase==FixedActionPhase::Active);
    CHECK_TRUE(frame.cues.size()==1 && frame.cues.front().kind==FixedActionCueKind::PhaseEntered);
    CHECK_TRUE(actions.step(3)->cues.back().kind==FixedActionCueKind::Finished);
}
TEST_CASE(authored_cancel_window_transitions_atomically_after_cost_precheck) {
    FixedActionDefinition attack;
    attack.id=10;
    attack.windupTicks=1;
    attack.activeTicks=2;
    attack.recoveryTicks=1;
    attack.cancelWindows={{1,3,{20}}};
    FixedActionDefinition dodge;
    dodge.id=20;
    dodge.activeTicks=2;
    auto actions=FixedActionTimeline::compile({attack,dodge}).value();
    CHECK_TRUE(actions.start(10,91,1).has_value());
    CHECK_TRUE(actions.canCancelInto(20,1).error().code==Error::Busy);
    CHECK_TRUE(actions.step(1)->phase==FixedActionPhase::Windup);
    CHECK_TRUE(actions.canCancelInto(10,2).error().code==Error::Busy);
    const auto before=actions.checkpoint();
    CHECK_TRUE(actions.transition(20,0,2).error().code==Error::Invalid);
    CHECK_TRUE(actions.checkpoint().action==before.action
        && actions.checkpoint().lastTick==before.lastTick);
    CHECK_TRUE(actions.canCancelInto(20,2).has_value());
    const auto cancelled=actions.transition(20,92,2).value();
    CHECK_TRUE(cancelled.kind==FixedActionCueKind::Cancelled
        && cancelled.action==10 && cancelled.execution==91);
    const auto frame=actions.step(2).value();
    CHECK_TRUE(frame.action==20 && frame.execution==92
        && frame.phase==FixedActionPhase::Active);
    CHECK_TRUE(actions.canCancelInto(10,3).error().code==Error::Busy);

    auto changed=attack;
    changed.cancelWindows[0].startOffset=2;
    auto other=FixedActionTimeline::compile({changed,dodge}).value();
    CHECK_TRUE(other.restore(before).error().code==Error::Invalid);
    changed=attack;
    changed.cancelWindows[0].nextActions={30};
    CHECK_TRUE(FixedActionTimeline::compile({changed,dodge}).error().code==Error::Missing);
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
