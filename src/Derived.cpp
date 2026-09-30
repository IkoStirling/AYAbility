#include <AYAbility/Derived.h>
#include <limits>
#include <set>

namespace ayt::ability {
namespace {
template<class T> Result<T> fail(Error code,std::string message) {
    return std::unexpected(Diagnostic{code,std::move(message),{}});
}
constexpr Id EmitterNamespace=Id{1}<<63;
Result<DerivedRelease> buildRelease(const DerivedDefinition& definition,
        ReleaseSnapshot source,Id domain,Id firstEmitter) {
    if(auto valid=validateDerived(definition);!valid)return std::unexpected(valid.error());
    if(!source.actor.valid() || !source.execution || !source.action ||
       domain!=source.actor.domain || !domain || !firstEmitter ||
       firstEmitter>std::numeric_limits<Id>::max()-EmitterNamespace-64 ||
       (source.equipment && (!source.equipment->valid() || source.equipment->domain!=domain)))
        return fail<DerivedRelease>(Error::Invalid,"invalid release identity");
    auto snapshot=std::make_shared<const ReleaseSnapshot>(std::move(source));
    DerivedRelease release;
    release.source=snapshot;
    release.stages=definition.stages;
    std::size_t count=0;
    std::uint64_t window=0;
    for(auto& stage:release.stages) {
        if(stage.amountParameter) {
            auto amount=snapshot->resolved.value(stage.amountParameter,Unit::Amount);
            if(!amount)return std::unexpected(amount.error());
            stage.amount=*amount;
        }
        if(stage.emittersParameter) {
            auto emitters=snapshot->resolved.value(stage.emittersParameter,Unit::Count);
            if(!emitters)return std::unexpected(emitters.error());
            if(*emitters<1 || *emitters>32)
                return fail<DerivedRelease>(Error::Limit,"resolved emitter count must be 1..32");
            stage.emitters=static_cast<std::uint32_t>(*emitters);
        }
        if(stage.operation!=StageOperation::SetAttribute && stage.amount<0)
            return fail<DerivedRelease>(Error::Invalid,"negative stage amount");
        if(count+stage.emitters>64)return fail<DerivedRelease>(Error::Limit,"release emitter limit");
        if(stage.delayTicks>std::numeric_limits<Id>::max()-window-stage.lifetimeTicks)
            return fail<DerivedRelease>(Error::Overflow,"release lifetime overflow");
        window+=stage.delayTicks+stage.lifetimeTicks;
        for(std::uint32_t i=0;i<stage.emitters;++i)
            release.emissions.push_back({{domain,EmitterNamespace+firstEmitter+count++,1},
                stage.id,stage.kind,snapshot,
                stage.trigger==StageTrigger::Release?snapshot->tick+stage.delayTicks:0,
                stage.lifetimeTicks});
    }
    if(window>std::numeric_limits<Id>::max()-snapshot->tick)
        return fail<DerivedRelease>(Error::Overflow,"release tick overflow");
    return release;
}
}
Result<void> validateDerived(const DerivedDefinition& definition) {
    if(definition.stages.empty() || definition.stages.size()>16)
        return fail<void>(Error::Limit,"derived stage count must be 1..16");
    if(definition.sourceLoss!=SourceLossPolicy::Cancel &&
       definition.sourceLoss!=SourceLossPolicy::ContinueFromSnapshot)
        return fail<void>(Error::Invalid,"invalid source-loss policy");
    std::set<Id> ids;
    std::size_t count=0;
    for(const auto& stage:definition.stages) {
        if(!stage.id || !ids.insert(stage.id).second || !stage.emitters || stage.emitters>32 ||
           (stage.operation!=StageOperation::SetAttribute && stage.amount<0) ||
           !stage.lifetimeTicks || stage.lifetimeTicks>100000 || stage.delayTicks>100000 ||
           (stage.hitMode==DerivedHitMode::Periodic ? !stage.intervalTicks : stage.intervalTicks!=0) ||
           stage.kind>DeliveryKind::Hazard || stage.hitMode>DerivedHitMode::Periodic ||
           stage.trigger>StageTrigger::StageFinished || stage.operation>StageOperation::OwnedEffect ||
           stage.intent>StageIntent::Beneficial || !stage.allowedTargets ||
           (stage.allowedTargets&~std::uint32_t{63}))
            return fail<void>(Error::Invalid,"invalid derived stage");
        if((stage.trigger==StageTrigger::Release && stage.predecessor) ||
           (stage.trigger!=StageTrigger::Release &&
            (!stage.predecessor || !ids.contains(stage.predecessor) || stage.predecessor==stage.id)))
            return fail<void>(Error::Invalid,"stage trigger must reference an earlier stage");
        if(((stage.operation==StageOperation::Damage || stage.operation==StageOperation::Heal) &&
            !stage.attribute.empty()) ||
           ((stage.operation==StageOperation::IncreaseAttribute ||
             stage.operation==StageOperation::DecreaseAttribute ||
             stage.operation==StageOperation::SetAttribute) && stage.attribute.empty()) ||
           (stage.operation==StageOperation::OwnedEffect &&
            (!stage.effect || !stage.attribute.empty() || stage.amountParameter || !stage.recipe.empty())) ||
           (stage.operation!=StageOperation::OwnedEffect && stage.effect))
            return fail<void>(Error::Invalid,"stage operation fields are inconsistent");
        count+=stage.emitters;
        if(count>64)return fail<void>(Error::Limit,"derived emitter limit");
    }
    return {};
}
Result<DerivedRelease> makeDerivedRelease(const DerivedDefinition& definition,
        const ActivationEvent& event,std::optional<Identity> equipment,
        std::uint32_t skillLevel,std::uint64_t team,Id domain,Id firstEmitter) {
    if(!event.activeSkill || event.resolved.behaviorId!=derived::Behavior || !skillLevel)
        return fail<DerivedRelease>(Error::Invalid,"invalid active derived release");
    return buildRelease(definition,ReleaseSnapshot{
        event.actor,equipment,event.activeSkill,skillLevel,event.execution,event.tick,team,
        definition.sourceLoss,event.resolved,event.resolved.action},domain,firstEmitter);
}
Result<DerivedRelease> makeDerivedRelease(const DerivedDefinition& definition,
        const FireEvent& event,std::uint32_t skillLevel,std::uint64_t team,
        Id domain,Id firstEmitter) {
    if(event.resolved.behaviorId!=fire::Behavior || (event.activeSkill && !skillLevel))
        return fail<DerivedRelease>(Error::Invalid,"invalid fire release");
    return buildRelease(definition,ReleaseSnapshot{
        event.actor,event.equipment,event.activeSkill,skillLevel,event.execution,event.tick,team,
        definition.sourceLoss,event.resolved,event.resolved.action},domain,firstEmitter);
}
} // namespace ayt::ability
