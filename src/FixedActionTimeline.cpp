#include <AYAbility/FixedActionTimeline.h>
#include <algorithm>
#include <limits>
#include <set>

namespace ayt::ability {
namespace {
template<class T> Result<T> fail(Error code, const char* message) {
    return std::unexpected(Diagnostic{code,message,{}});
}
constexpr std::uint64_t MaxDuration = 1'000'000;
std::uint64_t duration(const FixedActionDefinition& definition) {
    return std::uint64_t(definition.windupTicks) + definition.activeTicks +
        definition.recoveryTicks;
}
FixedActionPhase phaseAt(const FixedActionDefinition& definition, std::uint64_t offset) {
    if (offset < definition.windupTicks) return FixedActionPhase::Windup;
    if (offset < std::uint64_t(definition.windupTicks) + definition.activeTicks)
        return FixedActionPhase::Active;
    return FixedActionPhase::Recovery;
}
void hash(std::uint64_t& fingerprint, std::uint64_t value) {
    for (unsigned i=0;i<8;++i) {
        fingerprint ^= static_cast<std::uint8_t>(value >> (i*8));
        fingerprint *= 1099511628211ull;
    }
}
}

Result<FixedActionTimeline> FixedActionTimeline::compile(
    std::vector<FixedActionDefinition> definitions) {
    FixedActionTimeline timeline;
    std::size_t totalWindows=0;
    for (auto& definition:definitions) {
        const auto total=duration(definition);
        if (!definition.id || !definition.activeTicks || !total || total>MaxDuration ||
            definition.inputBufferTicks>total)
            return fail<FixedActionTimeline>(Error::Invalid,"invalid fixed action duration");
        std::set<std::pair<std::uint32_t,Id>> uniqueMarkers;
        for (const auto& marker:definition.markers) {
            if (!marker.id || marker.offset>=total)
                return fail<FixedActionTimeline>(Error::Invalid,"invalid fixed action marker");
            if (!uniqueMarkers.emplace(marker.offset,marker.id).second)
                return fail<FixedActionTimeline>(Error::Duplicate,
                    "duplicate fixed action marker on one tick");
        }
        std::stable_sort(definition.markers.begin(),definition.markers.end(),
            [](const auto& a,const auto& b){return a.offset<b.offset;});
        totalWindows+=definition.cancelWindows.size();
        if (totalWindows>4096)
            return fail<FixedActionTimeline>(Error::Invalid,"fixed action cancel window budget exceeded");
        for (auto& window:definition.cancelWindows) {
            if (window.startOffset>=window.endOffset || window.endOffset>total ||
                window.nextActions.empty() || window.nextActions.size()>128)
                return fail<FixedActionTimeline>(Error::Invalid,"invalid fixed action cancel window");
            std::sort(window.nextActions.begin(),window.nextActions.end());
            if (window.nextActions.front()==0 ||
                std::adjacent_find(window.nextActions.begin(),window.nextActions.end())
                    !=window.nextActions.end())
                return fail<FixedActionTimeline>(Error::Duplicate,
                    "duplicate or zero fixed action cancel target");
        }
        std::sort(definition.cancelWindows.begin(),definition.cancelWindows.end(),
            [](const auto& a,const auto& b) {
                return a.startOffset!=b.startOffset ? a.startOffset<b.startOffset
                    : a.endOffset<b.endOffset;
            });
        for (std::size_t i=1;i<definition.cancelWindows.size();++i)
            if (definition.cancelWindows[i-1].startOffset==definition.cancelWindows[i].startOffset
                && definition.cancelWindows[i-1].endOffset==definition.cancelWindows[i].endOffset)
                return fail<FixedActionTimeline>(Error::Duplicate,
                    "duplicate fixed action cancel window");
        const Id id=definition.id;
        if (!timeline._definitions.emplace(id,std::move(definition)).second)
            return fail<FixedActionTimeline>(Error::Duplicate,"duplicate fixed action id");
    }
    if (timeline._definitions.empty())
        return fail<FixedActionTimeline>(Error::Invalid,"fixed action definitions empty");
    for (const auto& [id,definition]:timeline._definitions)
        for (const auto& window:definition.cancelWindows)
            for (const auto target:window.nextActions)
                if (!timeline._definitions.contains(target))
                    return fail<FixedActionTimeline>(Error::Missing,
                        "fixed action cancel target missing");
    std::uint64_t fingerprint=14695981039346656037ull;
    for (const auto& [id,definition]:timeline._definitions) {
        hash(fingerprint,id);
        hash(fingerprint,definition.windupTicks);
        hash(fingerprint,definition.activeTicks);
        hash(fingerprint,definition.recoveryTicks);
        hash(fingerprint,definition.inputBufferTicks);
        hash(fingerprint,definition.markers.size());
        for (const auto& marker:definition.markers) {
            hash(fingerprint,marker.offset);
            hash(fingerprint,marker.id);
        }
        // Preserve historical fingerprints for definitions without windows.
        if (!definition.cancelWindows.empty()) {
            hash(fingerprint,0x43414e43454c574eull);
            hash(fingerprint,definition.cancelWindows.size());
            for (const auto& window:definition.cancelWindows) {
                hash(fingerprint,window.startOffset);
                hash(fingerprint,window.endOffset);
                hash(fingerprint,window.nextActions.size());
                for (const auto target:window.nextActions) hash(fingerprint,target);
            }
        }
    }
    timeline._fingerprint=fingerprint;
    return timeline;
}

Result<void> FixedActionTimeline::canStart(Id action,std::uint64_t tick) const {
    if (_action) return fail<void>(Error::Busy,"fixed action already running");
    const auto found=_definitions.find(action);
    if (found==_definitions.end()) return fail<void>(Error::Missing,"fixed action missing");
    if (_lastTick==std::numeric_limits<std::uint64_t>::max() ||
        tick!=_lastTick+1 || tick>std::numeric_limits<std::uint64_t>::max()-duration(found->second))
        return fail<void>(Error::Invalid,"invalid fixed action start tick");
    return {};
}

Result<void> FixedActionTimeline::start(Id action,std::uint64_t execution,
                                      std::uint64_t tick) {
    if (auto ready=canStart(action,tick);!ready) return ready;
    if (!execution) return fail<void>(Error::Invalid,"invalid fixed action execution");
    _action=action;
    _execution=execution;
    _startTick=tick;
    _lastPhase=FixedActionPhase::Idle;
    return {};
}

Result<void> FixedActionTimeline::buffer(Id nextAction,std::uint64_t tick) {
    if (!_action) return fail<void>(Error::Missing,"fixed action not running");
    if (_bufferedAction) return fail<void>(Error::Busy,"fixed action input already buffered");
    if (!_definitions.contains(nextAction))
        return fail<void>(Error::Missing,"buffered fixed action missing");
    if (_lastTick==std::numeric_limits<std::uint64_t>::max() || tick!=_lastTick+1)
        return fail<void>(Error::Stale,"buffered fixed action tick out of order");
    const auto& current=_definitions.at(_action);
    const std::uint64_t end=_startTick+duration(current);
    if (tick<_startTick || tick>=end || end-tick>current.inputBufferTicks)
        return fail<void>(Error::Busy,"outside fixed action input buffer");
    _bufferedAction=nextAction;
    _bufferedTick=tick;
    return {};
}

Result<void> FixedActionTimeline::canCancelInto(Id nextAction,
                                               std::uint64_t tick) const {
    if (!_action) return fail<void>(Error::Missing,"fixed action not running");
    const auto target=_definitions.find(nextAction);
    if (target==_definitions.end())
        return fail<void>(Error::Missing,"fixed action cancel target missing");
    if (_lastTick==std::numeric_limits<std::uint64_t>::max() ||
        tick!=_lastTick+1 || tick<_startTick ||
        tick>std::numeric_limits<std::uint64_t>::max()-duration(target->second))
        return fail<void>(Error::Stale,"fixed action cancel tick out of order");
    const auto& current=_definitions.at(_action);
    const auto offset=tick-_startTick;
    if (offset>=duration(current))
        return fail<void>(Error::Busy,"fixed action already finished");
    for (const auto& window:current.cancelWindows)
        if (offset>=window.startOffset && offset<window.endOffset &&
            std::binary_search(window.nextActions.begin(),window.nextActions.end(),nextAction))
            return {};
    return fail<void>(Error::Busy,"outside authored fixed action cancel window");
}

Result<FixedActionCue> FixedActionTimeline::transition(
    Id nextAction,std::uint64_t execution,std::uint64_t tick) {
    if (auto ready=canCancelInto(nextAction,tick);!ready)
        return std::unexpected(ready.error());
    if (!execution) return fail<FixedActionCue>(Error::Invalid,
        "invalid fixed action execution");
    FixedActionCue cue{FixedActionCueKind::Cancelled,_lastPhase,_action,0,_execution,tick};
    _action=nextAction;
    _execution=execution;
    _startTick=tick;
    _bufferedAction=0;
    _bufferedTick=0;
    _lastPhase=FixedActionPhase::Idle;
    return cue;
}

Result<FixedActionFrame> FixedActionTimeline::step(std::uint64_t tick) {
    if (_lastTick==std::numeric_limits<std::uint64_t>::max() || tick!=_lastTick+1)
        return fail<FixedActionFrame>(Error::Stale,"fixed action tick out of order");
    FixedActionFrame frame;
    if (_action) {
        const auto& definition=_definitions.at(_action);
        const std::uint64_t total=duration(definition);
        if (tick<_startTick || tick-_startTick>=total)
            return fail<FixedActionFrame>(Error::Stale,"fixed action state outside duration");
        const std::uint64_t offset=tick-_startTick;
        frame.action=_action;
        frame.execution=_execution;
        frame.phase=phaseAt(definition,offset);
        if (frame.phase!=_lastPhase)
            frame.cues.push_back({FixedActionCueKind::PhaseEntered,frame.phase,
                _action,0,_execution,tick});
        for (const auto& marker:definition.markers)
            if (marker.offset==offset)
                frame.cues.push_back({FixedActionCueKind::Marker,frame.phase,
                    _action,marker.id,_execution,tick});
        _lastPhase=frame.phase;
        if (offset+1==total) {
            frame.cues.push_back({FixedActionCueKind::Finished,frame.phase,
                _action,0,_execution,tick});
            frame.readyBufferedAction=_bufferedAction;
            _action=0;
            _execution=0;
            _startTick=0;
            _bufferedAction=0;
            _bufferedTick=0;
            _lastPhase=FixedActionPhase::Idle;
        }
    }
    _lastTick=tick;
    return frame;
}

Result<FixedActionCue> FixedActionTimeline::cancel(std::uint64_t tick,bool interrupted) {
    if (!_action) return fail<FixedActionCue>(Error::Missing,"fixed action not running");
    if (_lastTick==std::numeric_limits<std::uint64_t>::max() || tick!=_lastTick+1)
        return fail<FixedActionCue>(Error::Stale,"fixed action cancel tick out of order");
    FixedActionCue cue{interrupted?FixedActionCueKind::Interrupted:
        FixedActionCueKind::Cancelled,_lastPhase,_action,0,_execution,tick};
    _action=0;
    _execution=0;
    _startTick=0;
    _bufferedAction=0;
    _bufferedTick=0;
    _lastPhase=FixedActionPhase::Idle;
    return cue;
}

FixedActionSnapshot FixedActionTimeline::checkpoint() const noexcept {
    return {1,_fingerprint,_lastTick,_action,_bufferedAction,_execution,
        _startTick,_bufferedTick,_lastPhase};
}

Result<void> FixedActionTimeline::restore(const FixedActionSnapshot& snapshot) {
    if (snapshot.version!=1 || snapshot.definitionFingerprint!=_fingerprint)
        return fail<void>(Error::Invalid,"fixed action definition mismatch");
    if (!snapshot.action) {
        if (snapshot.execution || snapshot.startTick || snapshot.bufferedAction ||
            snapshot.bufferedTick || snapshot.lastPhase!=FixedActionPhase::Idle)
            return fail<void>(Error::Invalid,"invalid idle fixed action snapshot");
    } else {
        const auto found=_definitions.find(snapshot.action);
        if (found==_definitions.end() || !snapshot.execution || !snapshot.startTick ||
            (snapshot.startTick>snapshot.lastTick &&
             (snapshot.lastTick==std::numeric_limits<std::uint64_t>::max() ||
              snapshot.startTick!=snapshot.lastTick+1)))
            return fail<void>(Error::Invalid,"invalid running fixed action snapshot");
        const auto total=duration(found->second);
        if (snapshot.startTick>std::numeric_limits<std::uint64_t>::max()-total ||
            snapshot.lastTick>=snapshot.startTick+total-1)
            return fail<void>(Error::Invalid,"fixed action snapshot past completion");
        const auto expected=snapshot.startTick>snapshot.lastTick
            ?FixedActionPhase::Idle:phaseAt(found->second,snapshot.lastTick-snapshot.startTick);
        if (snapshot.lastPhase!=expected)
            return fail<void>(Error::Invalid,"fixed action snapshot phase mismatch");
        if (snapshot.bufferedAction) {
            if (!_definitions.contains(snapshot.bufferedAction) ||
                snapshot.bufferedTick<snapshot.startTick ||
                snapshot.bufferedTick>snapshot.lastTick+1 ||
                snapshot.startTick+total-snapshot.bufferedTick>
                    found->second.inputBufferTicks)
                return fail<void>(Error::Invalid,"invalid buffered fixed action snapshot");
        } else if (snapshot.bufferedTick)
            return fail<void>(Error::Invalid,"invalid empty fixed action buffer");
    }
    _lastTick=snapshot.lastTick;
    _action=snapshot.action;
    _bufferedAction=snapshot.bufferedAction;
    _execution=snapshot.execution;
    _startTick=snapshot.startTick;
    _bufferedTick=snapshot.bufferedTick;
    _lastPhase=snapshot.lastPhase;
    return {};
}
} // namespace ayt::ability
