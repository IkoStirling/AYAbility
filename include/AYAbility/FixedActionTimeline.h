#pragma once

#include <AYAbility/Ability.h>
#include <map>
#include <optional>

namespace ayt::ability {

enum class FixedActionPhase : std::uint8_t { Idle, Windup, Active, Recovery };
enum class FixedActionCueKind : std::uint8_t {
    PhaseEntered, Marker, Finished, Cancelled, Interrupted
};

/// Marker offsets are zero-based fixed ticks from action start. An action
/// cannot repeat one marker ID at the same offset. Meaning belongs to authored
/// content and the host; the timeline never invokes gameplay code.
struct FixedActionMarker { std::uint32_t offset=0; Id id=0; };
/// [startOffset,endOffset) accepts only the listed next action IDs. The host
/// prechecks this window before charging the next action's external cost.
struct FixedActionCancelWindow {
    std::uint32_t startOffset=0, endOffset=0;
    std::vector<Id> nextActions;
};
/// [startOffset,endOffset) inside the action's Active phase. The stable ID
/// identifies one independent hit channel. A host using Combat's HitLedger
/// may use HitScope::Segment and this ID as the segment for per-channel dedupe.
struct FixedActionHitWindow {
    std::uint32_t startOffset=0, endOffset=0;
    std::uint32_t id=0;
};
struct FixedActionDefinition {
    Id id=0;
    std::uint32_t windupTicks=0, activeTicks=1, recoveryTicks=0;
    /// Final ticks of this action in which one next-action input may be stored.
    std::uint32_t inputBufferTicks=0;
    std::vector<FixedActionMarker> markers;
    std::vector<FixedActionCancelWindow> cancelWindows;
    std::vector<FixedActionHitWindow> hitWindows;
};
struct FixedActionCue {
    FixedActionCueKind kind=FixedActionCueKind::Marker;
    FixedActionPhase phase=FixedActionPhase::Idle;
    Id action=0, marker=0;
    std::uint64_t execution=0, tick=0;
};
struct FixedActionFrame {
    FixedActionPhase phase=FixedActionPhase::Idle;
    Id action=0;
    std::uint64_t execution=0;
    /// Ordered phase, authored marker, then finish cues for this fixed tick.
    std::vector<FixedActionCue> cues;
    /// Active authored hit channels with absolute half-open tick bounds.
    /// The host authorizes queries here and manages its hit ledger lifetime.
    struct ActiveHitWindow { std::uint32_t id=0; std::uint64_t beginTick=0, endTick=0; };
    std::vector<ActiveHitWindow> hitWindows;
    /// An input buffered during recovery is released when the action finishes.
    /// The host must validate/charge it before starting the next action.
    Id readyBufferedAction=0;
};

/// Pointer-free state. Restore checks definition fingerprint and all tick bounds,
/// including that a running action has not already passed its finish tick,
/// before replacing the live timeline.
struct FixedActionSnapshot {
    std::uint32_t version=1;
    std::uint64_t definitionFingerprint=0, lastTick=0;
    Id action=0, bufferedAction=0;
    std::uint64_t execution=0, startTick=0, bufferedTick=0;
    FixedActionPhase lastPhase=FixedActionPhase::Idle;
};

/// A single actor's data-only fixed-tick phase clock. It does not charge
/// resources, move an entity, dispatch a hit, or own an animation player.
class FixedActionTimeline {
public:
    static Result<FixedActionTimeline> compile(std::vector<FixedActionDefinition> definitions);
    /// Prevalidate the next start before charging an external Ability cost.
    /// The single-owner host must not mutate this timeline between check/start.
    Result<void> canStart(Id action, std::uint64_t tick) const;
    /// Start an already accepted Ability execution on the next unprocessed tick.
    Result<void> start(Id action, std::uint64_t execution, std::uint64_t tick);
    /// Store one next-action intent during the current action's final window.
    /// Input buffering does not precommit an Ability execution or its cost.
    Result<void> buffer(Id nextAction, std::uint64_t tick);
    /// Prevalidate one authored voluntary transition before external Ability
    /// cost is charged. The single-owner host must not mutate this timeline
    /// between precheck and transition.
    Result<void> canCancelInto(Id nextAction, std::uint64_t tick) const;
    /// Atomically replace the running action on its next unprocessed tick.
    /// Returns a cancellation cue for the old action; step(tick) then emits
    /// the new action's first frame. Invalid transitions leave state intact.
    Result<FixedActionCue> transition(Id nextAction, std::uint64_t execution,
                                      std::uint64_t tick);
    /// Compute this tick's cues and hit windows without advancing the clock.
    /// The single-owner host can prepare animation/physics from this frame,
    /// then call step or a combined Combat adapter at the commit boundary.
    /// No timeline mutation may intervene between preview and commit.
    Result<FixedActionFrame> previewStep(std::uint64_t tick) const;
    /// Advance exactly one fixed tick; skipped or repeated ticks are rejected.
    Result<FixedActionFrame> step(std::uint64_t tick);
    /// Stop before the next step. The returned cue lets the host stop animation
    /// and cancel owned hit delivery before any more movement is submitted.
    Result<FixedActionCue> cancel(std::uint64_t tick, bool interrupted=false);
    FixedActionSnapshot checkpoint() const noexcept;
    Result<void> restore(const FixedActionSnapshot& snapshot);
    bool running() const noexcept { return _action != 0; }
private:
    std::map<Id,FixedActionDefinition> _definitions;
    std::uint64_t _fingerprint=0, _lastTick=0, _execution=0, _startTick=0, _bufferedTick=0;
    Id _action=0, _bufferedAction=0;
    FixedActionPhase _lastPhase=FixedActionPhase::Idle;
};

} // namespace ayt::ability
