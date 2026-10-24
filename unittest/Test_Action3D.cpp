#include <AYTest.h>
#include "../examples/Action3DScenario.h"

TEST_SUITE(AbilityAction3DTests)
TEST_CASE(fixed_step_attack_dodge_interrupt_and_exact_hit) {
    const auto result=ayt::ability::action3d_example::run();
    CHECK_INT_EQ(result.attackExecution,1u);
    CHECK_INT_EQ(result.attackHitTick,2u);
    CHECK_INT_EQ(result.manaAfterAttack,15);
    CHECK_INT_EQ(result.manaAfterDodge,5);
    CHECK_INT_EQ(result.healthAfterHit,70);
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
    CHECK_TRUE(result.exactCollider);
    CHECK_TRUE(result.visualEffectCue);
    CHECK_TRUE(result.boundaryRejected);
    CHECK_TRUE(result.cancelledDodgeStopped);
}
TEST_CASE(denied_buffered_dodge_does_not_charge_or_move) {
    const auto result=ayt::ability::action3d_example::run(false,true);
    CHECK_INT_EQ(result.manaAfterAttack,15);
    CHECK_INT_EQ(result.healthAfterHit,70);
    CHECK_TRUE(result.deniedBufferedActionStayedIdle);
}
TEST_SUITE_END
