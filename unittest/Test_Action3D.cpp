#include <AYTest.h>
#include <AYCombat/Action3DBatch.h>
#include "../examples/Action3DScenario.h"
#include <array>

TEST_SUITE(AbilityAction3DTests)
TEST_CASE(fixed_step_attack_dodge_interrupt_and_exact_hit) {
    const auto result=ayt::ability::action3d_example::run();
    CHECK_INT_EQ(result.attackExecution,1u);
    CHECK_INT_EQ(result.attackHitTick,2u);
    CHECK_INT_EQ(result.manaAfterAttack,15);
    CHECK_INT_EQ(result.manaAfterDodge,5);
    CHECK_INT_EQ(result.healthAfterHit,70);
    CHECK_INT_EQ(result.secondHealthAfterHit,70);
    CHECK_TRUE(result.positionAfterAttack>0.45f);
    CHECK_TRUE(result.swingNotify);
    CHECK_TRUE(result.visualEffectCue);
    CHECK_TRUE(result.exactCollider);
    CHECK_TRUE(result.boundaryRejected);
    CHECK_TRUE(result.cancelledDodgeStopped);
}
TEST_CASE(target_disappearing_in_active_tick_cannot_receive_hit) {
    const auto result=ayt::ability::action3d_example::run(true);
    CHECK_INT_EQ(result.healthAfterHit,100);
    CHECK_INT_EQ(result.secondHealthAfterHit,100);
    CHECK_TRUE(result.exactCollider);
    CHECK_TRUE(result.visualEffectCue);
    CHECK_TRUE(result.boundaryRejected);
    CHECK_TRUE(result.cancelledDodgeStopped);
}
TEST_CASE(second_target_settlement_failure_keeps_both_queried_targets_unhurt) {
    const auto result=ayt::ability::action3d_example::run(false,false,nullptr,true);
    CHECK_INT_EQ(result.healthAfterHit,100);
    CHECK_INT_EQ(result.secondHealthAfterHit,100);
    CHECK_TRUE(result.exactCollider);
    CHECK_TRUE(result.boundaryRejected);
}
TEST_CASE(fixed_action_and_two_combat_targets_publish_together) {
    using namespace ayt::ability;
    using namespace ayt::combat;
    using namespace ayt::stats;
    FixedActionDefinition strike;
    strike.id = 1;
    strike.activeTicks = 2;
    strike.hitWindows = {{0, 1, 7}, {0, 1, 8}};
    auto compiled = FixedActionTimeline::compile({strike});
    CHECK_TRUE(compiled.has_value());
    auto timeline = std::move(*compiled);
    CHECK_TRUE(timeline.start(1, 11, 1).has_value());
    const auto before = timeline.checkpoint();
    const auto preview = timeline.previewStep(1);
    CHECK_TRUE(preview.has_value() && preview->hitWindows.size() == 2
        && timeline.checkpoint().lastTick == before.lastTick);
    auto schema = Schema::compile({Definition{
        .name = "health", .shape = Shape::Pool, .current = 100,
        .capacity = 100}}).value();
    auto first = StatSet::create(schema).value();
    auto second = StatSet::create(schema).value();
    const auto plan = CalculationPlan::compile(
        {{.id = 1, .op = Op::Input, .binding = 0}}, 1, 1).value();
    const std::array<Value, 1> minor{10}, major{20};
    const CombatantRef source{1, 1, 1}, left{1, 2, 1}, right{1, 3, 1};
    const std::array<CombatBatchTarget, 2> targets{{
        {left, &first, nullptr}, {right, &second, nullptr}}};
    RelationTable relations;
    CHECK_TRUE(relations.set(10, 20, Relation::Hostile).has_value());
    HitLedger ledger;
    CHECK_TRUE(ledger.open({1, 1}, {HitScope::Segment}, 0, 100).has_value());
    bool rejectSecond = true;
    bool wrongWindow = true;
    const ActionCombatHit3DFactory query = [&](const FixedActionFrame& frame)
        -> ayt::combat::Result<std::vector<CombatBatchHit>> {
        std::vector<CombatBatchHit> hits;
        for (std::size_t i = 0; i < frame.hitWindows.size(); ++i) {
            const auto target = i == 0 ? left : right;
            const auto& state = i == 0 ? first : second;
            SettlementRequest request;
            request.hit = {{1, 1}, i + 1, target, 1, 0,
                           i == 1 && wrongWindow ? 999u : frame.hitWindows[i].id};
            request.source.instigator = source;
            request.source.team = 10;
            request.source.ability = 1;
            request.source.abilityInstance = frame.execution;
            request.target = {target, 20, mask(Capability::Damageable)};
            request.resource = i == 1 && rejectSecond
                ? statId("missing") : statId("health");
            request.expectedVersion = state.version();
            request.calculation = {.seed = 123, .tick = 1,
                .calculation = i + 1, .hit = i + 1};
            hits.push_back({request, &plan, i == 0
                ? std::span<const Value>(minor) : std::span<const Value>(major)});
        }
        return hits;
    };
    ActionCombatBatch3DResult out;
    out.frame.action = 99;
    auto stale = before;
    ++stale.lastTick;
    CHECK_TRUE(stepAndSettleFixedAction3D(timeline, 1, stale, query, targets,
        relations, ledger, out).error().code == ayt::combat::Error::Stale);
    CHECK_TRUE(stepAndSettleFixedAction3D(timeline, 1, before, query, targets,
        relations, ledger, out).error().code == ayt::combat::Error::Invalid);
    wrongWindow = false;
    CHECK_TRUE(!stepAndSettleFixedAction3D(timeline, 1, before, query,
        targets, relations, ledger, out).has_value());
    CHECK_TRUE(timeline.checkpoint().lastTick == before.lastTick
        && timeline.checkpoint().action == before.action
        && first.read(statId("health"))->current == 100
        && second.read(statId("health"))->current == 100
        && ledger.retainedRequests() == 0
        && out.frame.action == 99 && out.combat.empty());
    rejectSecond = false;
    const ActionCombatPreflight staleWorld = [](std::span<const CombatBatchHit>)
        -> ayt::combat::Result<void> {
        return std::unexpected(ayt::combat::Diagnostic{ayt::combat::Error::Stale,
            "target generation changed", {}});
    };
    CHECK_TRUE(stepAndSettleFixedAction3D(timeline, 1, before, query,
        targets, relations, ledger, out, nullptr, staleWorld).error().code
        == ayt::combat::Error::Stale);
    CHECK_TRUE(timeline.checkpoint() == before
        && first.read(statId("health"))->current == 100
        && second.read(statId("health"))->current == 100
        && ledger.retainedRequests() == 0);
    CHECK_TRUE(stepAndSettleFixedAction3D(timeline, 1, before, query, targets,
        relations, ledger, out).has_value());
    CHECK_TRUE(timeline.checkpoint().lastTick == 1
        && out.frame.execution == 11 && out.combat.size() == 2
        && out.frame.hitWindows.size() == preview->hitWindows.size()
        && first.read(statId("health"))->current == 90
        && second.read(statId("health"))->current == 80
        && ledger.retainedRequests() == 2);
}
TEST_CASE(denied_buffered_dodge_does_not_charge_or_move) {
    const auto result=ayt::ability::action3d_example::run(false,true);
    CHECK_INT_EQ(result.manaAfterAttack,15);
    CHECK_INT_EQ(result.healthAfterHit,70);
    CHECK_INT_EQ(result.secondHealthAfterHit,70);
    CHECK_TRUE(result.deniedBufferedActionStayedIdle);
}
TEST_SUITE_END
