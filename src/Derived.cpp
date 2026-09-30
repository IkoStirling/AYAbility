#include <AYAbility/Derived.h>
#include <limits>
#include <set>

namespace ayt::ability {
namespace {
template<class T> Result<T> fail(Error code,std::string message) {
    return std::unexpected(Diagnostic{code,std::move(message),{}});
}
constexpr Id EmitterNamespace=Id{1}<<63;
}
Result<void> validateDerived(const DerivedDefinition& definition) {
    if(definition.stages.empty() || definition.stages.size()>16)
        return fail<void>(Error::Limit,"derived stage count must be 1..16");
    std::set<Id> ids;
    std::size_t count=0;
    for(const auto& stage:definition.stages) {
        if(!stage.id || !ids.insert(stage.id).second || !stage.emitters || stage.emitters>32 ||
           stage.amount<0 || !stage.lifetimeTicks || stage.lifetimeTicks>100000 ||
           (stage.hitMode==DerivedHitMode::Periodic ? !stage.intervalTicks : stage.intervalTicks!=0))
            return fail<void>(Error::Invalid,"invalid derived stage");
        count+=stage.emitters;
        if(count>64)return fail<void>(Error::Limit,"derived emitter limit");
    }
    return {};
}
Result<DerivedRelease> makeDerivedRelease(const DerivedDefinition& definition,
        const ActivationEvent& event,std::optional<Identity> equipment,
        std::uint32_t skillLevel,std::uint64_t team,Id domain,Id firstEmitter) {
    if(auto valid=validateDerived(definition);!valid)return std::unexpected(valid.error());
    if(!event.actor.valid() || !event.execution || !event.activeSkill ||
       event.resolved.behaviorId!=derived::Behavior || !skillLevel ||
       domain!=event.actor.domain || !domain || !firstEmitter ||
       firstEmitter>std::numeric_limits<Id>::max()-EmitterNamespace-64 ||
       (equipment && (!equipment->valid() || equipment->domain!=domain)))
        return fail<DerivedRelease>(Error::Invalid,"invalid derived release identity");
    auto snapshot=std::make_shared<const ReleaseSnapshot>(ReleaseSnapshot{
        event.actor,equipment,event.activeSkill,skillLevel,event.execution,event.tick,team,
        definition.sourceLoss,event.resolved});
    DerivedRelease release;
    release.source=snapshot;
    release.stages=definition.stages;
    std::size_t count=0;
    for(auto& stage:release.stages) {
        if(stage.amountParameter) {
            auto amount=snapshot->resolved.value(stage.amountParameter,Unit::Amount);
            if(!amount)return std::unexpected(amount.error());
            if(*amount<0)return fail<DerivedRelease>(Error::Invalid,"negative derived stage amount");
            stage.amount=*amount;
        }
        if(stage.lifetimeTicks>std::numeric_limits<Id>::max()-event.tick)
            return fail<DerivedRelease>(Error::Overflow,"derived lifetime tick overflow");
        for(std::uint32_t i=0;i<stage.emitters;++i)
            release.emissions.push_back({{domain,EmitterNamespace+firstEmitter+count++,1},
                stage.id,stage.kind,snapshot});
    }
    return release;
}
} // namespace ayt::ability
