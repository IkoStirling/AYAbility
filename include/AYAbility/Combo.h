#pragma once
#include <AYAbility/Runtime.h>
#include <deque>

namespace ayt::ability {
/// A content token is the active skill ID, or the resolved action ID for an unskilled release.
struct ComboStep { Id token=0; std::uint64_t maxGapTicks=0; };
struct ComboDefinition { Id id=0; std::vector<ComboStep> steps; };
struct ComboEvent { Id combo=0; Identity actor; std::uint64_t tick=0; };

/// Independent fixed-tick sequence matcher. It consumes accepted releases only;
/// Ability/Combat decide whether a release was legal before calling accept.
class ComboTracker {
public:
    static Result<ComboTracker> compile(std::vector<ComboDefinition> definitions);
    std::vector<ComboEvent> accept(Identity actor,Id token,std::uint64_t tick);
    void removeActor(Identity actor) { _history.erase(actor); }
    void clear() { _history.clear(); }
private:
    struct Entry { Id token;std::uint64_t tick; };
    std::vector<ComboDefinition> _definitions;
    std::map<Identity,std::deque<Entry>> _history;
    std::size_t _maxSteps=0;
};
} // namespace ayt::ability
