#include <AYAbility/Combo.h>
#include <algorithm>

namespace ayt::ability {
Result<ComboTracker> ComboTracker::compile(std::vector<ComboDefinition> definitions) {
    if(definitions.size()>256)
        return std::unexpected(Diagnostic{Error::Limit,"combo definition limit",{}});
    ComboTracker tracker;
    for(const auto& definition:definitions) {
        if(!definition.id || definition.steps.size()<2 || definition.steps.size()>16)
            return std::unexpected(Diagnostic{Error::Invalid,"invalid combo identity or length",{}});
        for(std::size_t i=0;i<definition.steps.size();++i)
            if(!definition.steps[i].token || (i && !definition.steps[i].maxGapTicks))
                return std::unexpected(Diagnostic{Error::Invalid,"invalid combo step",{}});
        tracker._maxSteps=std::max(tracker._maxSteps,definition.steps.size());
    }
    std::sort(definitions.begin(),definitions.end(),
        [](const auto& a,const auto& b){return a.id<b.id;});
    if(std::adjacent_find(definitions.begin(),definitions.end(),
        [](const auto& a,const auto& b){return a.id==b.id;})!=definitions.end())
        return std::unexpected(Diagnostic{Error::Duplicate,"duplicate combo identity",{}});
    tracker._definitions=std::move(definitions);
    return tracker;
}
std::vector<ComboEvent> ComboTracker::accept(Identity actor,Id token,std::uint64_t tick) {
    if(!actor.valid() || !token || _definitions.empty())return {};
    auto& history=_history[actor];
    if(!history.empty() && tick<history.back().tick)history.clear();
    history.push_back({token,tick});
    while(history.size()>_maxSteps)history.pop_front();
    std::vector<ComboEvent> completed;
    for(const auto& definition:_definitions) {
        const auto count=definition.steps.size();
        if(history.size()<count)continue;
        const auto offset=history.size()-count;
        bool matches=true;
        for(std::size_t i=0;i<count;++i) {
            const auto& entry=history[offset+i];
            if(entry.token!=definition.steps[i].token ||
               (i && entry.tick-history[offset+i-1].tick>
                   definition.steps[i].maxGapTicks)) {matches=false;break;}
        }
        if(matches)completed.push_back({definition.id,actor,tick});
    }
    return completed;
}
} // namespace ayt::ability
