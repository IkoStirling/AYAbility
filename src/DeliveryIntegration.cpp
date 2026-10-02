#include <AYAbility/EntityIntegration.h>
#include <AYAbility/Authoring.h>
#include <AYEntity.h>
#include <AYEntity/components/TransformComponent.h>
#include <algorithm>
#include <limits>

namespace ayt::ability {
namespace {
template<class T> Result<T> fail(Error code,std::string message) {
    return std::unexpected(Diagnostic{code,std::move(message),{}});
}
template<class T> Result<T> fromCombat(const ayt::combat::Diagnostic& error) {
    return std::unexpected(Diagnostic{Error::Invalid,error.message,error.path});
}
ayt::combat::HitScope scope(DerivedHitMode mode) {
    switch(mode) {
    case DerivedHitMode::Part:return ayt::combat::HitScope::Part;
    case DerivedHitMode::Segment:return ayt::combat::HitScope::Segment;
    case DerivedHitMode::Periodic:return ayt::combat::HitScope::Periodic;
    default:return ayt::combat::HitScope::Target;
    }
}
}
Result<void> AbilitySystem::configureFireDelivery(Id configuration,DerivedDefinition definition) {
    if(!_catalog || !_runtime)return fail<void>(Error::Stale,"Ability catalog unavailable");
    const auto* configured=_catalog->findEquipment(configuration);
    const auto* action=configured?_catalog->findAction(configured->action):nullptr;
    if(!configured || !action || action->behaviorId!=fire::Behavior)
        return fail<void>(Error::Invalid,"configuration is not a fire behavior");
    if(auto valid=validateDerived(definition);!valid)return valid;
    for(const auto& stage:definition.stages)for(auto [parameter,unit]:
        {std::pair{stage.amountParameter,Unit::Amount},
         std::pair{stage.emittersParameter,Unit::Count}})if(parameter) {
        auto at=std::find_if(action->defaults.begin(),action->defaults.end(),
            [&](const Parameter& value){return value.id==parameter;});
        if(at==action->defaults.end() || at->unit!=unit)
            return fail<void>(Error::Invalid,"delivery parameter missing or wrong unit");
    }
    _fireDeliveries.insert_or_assign(configuration,std::move(definition));
    return {};
}
namespace {
Result<void> validateOwnedEffect(Id reference,const ayt::combat::EffectDefinition& definition) {
    if(!reference || !definition.type || !definition.key || !definition.maxStacks ||
       !definition.durationTicks ||
       (definition.modifiers.empty() && definition.tags.empty() &&
        definition.immuneEffects.empty() && definition.immuneDamageTypes.empty() &&
        !definition.stunned) ||
       std::any_of(definition.tags.begin(),definition.tags.end(),[](Id id){return !id;}) ||
       std::any_of(definition.immuneEffects.begin(),definition.immuneEffects.end(),
           [](Id id){return !id;}) ||
       std::any_of(definition.immuneDamageTypes.begin(),definition.immuneDamageTypes.end(),
           [](Id id){return !id;}))
        return fail<void>(Error::Invalid,"invalid Combat effect reference");
    return {};
}
}
Result<void> validateOwnedEffectBindings(const AuthoringDocument& document,
    const OwnedEffectBindings& effects) {
    if(auto valid=compileAuthoring(document);!valid)
        return std::unexpected(valid.error());
    if(effects.size()!=document.effectReferences.size())
        return fail<void>(Error::Invalid,"Combat effect binding count mismatch");
    for(const auto reference:document.effectReferences) {
        const auto found=effects.find(reference);
        if(found==effects.end())
            return fail<void>(Error::Missing,"Combat effect binding missing");
        if(auto valid=validateOwnedEffect(reference,found->second);!valid)return valid;
    }
    return {};
}
Result<void> AbilitySystem::configureOwnedEffect(Id reference,ayt::combat::EffectDefinition definition) {
    if(auto valid=validateOwnedEffect(reference,definition);!valid)return valid;
    _ownedEffects.insert_or_assign(reference,std::move(definition));
    return {};
}
Result<ayt::combat::EffectDefinition> AbilitySystem::ownedEffect(Id reference) const {
    const auto found=_ownedEffects.find(reference);
    if(found==_ownedEffects.end())
        return fail<ayt::combat::EffectDefinition>(Error::Missing,
            "Combat effect reference is not configured");
    return found->second;
}
Result<void> AbilitySystem::prepareDelivery(ayt::entity::EntityHandle actor,
        const ayt::combat::EntityBinding& binding,DerivedRelease& release,
        const std::optional<std::pair<std::string,Value>>& cost) {
    if(_derivedCasts.size()>=128)return fail<void>(Error::Limit,"active delivery limit");
    const auto execution=release.source->execution;
    auto [at,inserted]=_derivedCasts.try_emplace(execution);
    if(!inserted)return fail<void>(Error::Duplicate,"delivery execution already exists");
    auto rollback=[&](){closeDerived(at->second);_derivedCasts.erase(at);};
    try {
        auto& record=at->second;
        record.source=release.source;record.actor=actor;
        record.statsGeneration=binding.statsGeneration;
        record.combatGeneration=binding.combatGeneration;
        record.emissions.reserve(release.emissions.size());
        record.stageOrder.reserve(release.stages.size());
        _emissionQueue.reserve(_emissionQueue.size()+release.emissions.size());
        std::uint64_t window=0;
        for(const auto& stage:release.stages) {
            window+=stage.delayTicks+stage.lifetimeTicks;
            record.stageOrder.push_back(stage.id);
            DerivedStageRecord current;
            current.stage=stage;
            if(stage.operation==StageOperation::OwnedEffect) {
                auto effect=_ownedEffects.find(stage.effect);
                if(effect==_ownedEffects.end()) {
                    rollback();return fail<void>(Error::Missing,"delivery Combat effect missing");
                }
                current.effect=effect->second;
            }
            if(stage.trigger==StageTrigger::Release) {
                current.triggered=true;
                current.dueTick=release.source->tick+stage.delayTicks;
                auto* entity=_world->getEntityByHandle(actor);
                auto* transform=entity?entity->getComponent<ayt::entity::Transform>():nullptr;
                if(transform)current.origin=std::array<float,3>{
                    transform->position.x,transform->position.y,transform->position.z};
            }
            record.stages.emplace(stage.id,std::move(current));
        }
        for(const auto& command:release.emissions)
            record.emissions.push_back({command.emitter,command.stage,false});
        const auto attackEnd=release.source->tick+window;
        for(auto id:record.stageOrder) {
            auto& stage=record.stages.at(id);
            auto next=_combat->hits().nextAttackId(_combat->worldToken());
            if(!next){auto error=next.error();rollback();return fromCombat<void>(error);}
            ayt::combat::AttackRef attack{_combat->worldToken(),*next};
            auto opened=_combat->hits().open(attack,
                {scope(stage.stage.hitMode),stage.stage.intervalTicks},
                release.source->tick,attackEnd);
            if(!opened){auto error=opened.error();rollback();return fromCombat<void>(error);}
            stage.attack=attack;stage.open=true;
        }
        auto charged=chargeCost(actor,cost);
        if(!charged){rollback();return charged;}
        return {};
    } catch(...) {
        rollback();
        return fail<void>(Error::Invalid,"delivery preparation threw");
    }
}
void AbilitySystem::triggerDependents(DerivedCastRecord& record,Id predecessor,
        StageTrigger trigger,std::uint64_t tick,
        std::optional<std::array<float,3>> origin) {
    for(auto id:record.stageOrder) {
        auto& stage=record.stages.at(id);
        if(stage.ended || stage.triggered || stage.stage.trigger!=trigger ||
           stage.stage.predecessor!=predecessor)continue;
        stage.triggered=true;
        stage.dueTick=tick+stage.stage.delayTicks;
        stage.origin=origin;
    }
}
std::vector<EmissionCommand> AbilitySystem::advanceDelivery(DerivedCastRecord& record,
        std::uint64_t tick) {
    std::vector<EmissionCommand> emitted;
    for(auto id:record.stageOrder) {
        auto& stage=record.stages.at(id);
        if(stage.ended)continue;
        if(!stage.triggered && stage.stage.trigger==StageTrigger::FirstConfirmedHit) {
            const auto& parent=record.stages.at(stage.stage.predecessor);
            if(parent.ended && !parent.hit) {
                if(stage.open){(void)_combat->hits().close(stage.attack);stage.open=false;}
                stage.ended=true;
                triggerDependents(record,id,StageTrigger::StageFinished,tick);
                continue;
            }
        }
        if(stage.triggered && !stage.emitted && tick>=stage.dueTick) {
            stage.emitted=true;
            stage.endTick=tick+stage.stage.lifetimeTicks;
            for(auto& emission:record.emissions)if(emission.stage==id) {
                emission.active=true;
                EmissionCommand command{emission.emitter,id,stage.stage.kind,record.source,tick,
                    stage.stage.lifetimeTicks,stage.origin};
                emitted.push_back(command);
                _emissionQueue.push_back(std::move(command));
            }
        }
        if(stage.emitted && tick>=stage.endTick) {
            if(stage.open){(void)_combat->hits().close(stage.attack);stage.open=false;}
            stage.ended=true;
            for(auto& emission:record.emissions)if(emission.stage==id)emission.active=false;
            triggerDependents(record,id,StageTrigger::StageFinished,tick,stage.origin);
        }
    }
    return emitted;
}
std::vector<EmissionCommand> AbilitySystem::takeEmissions() {
    std::vector<EmissionCommand> ready;
    for(auto& command:_emissionQueue) {
        auto cast=_derivedCasts.find(command.source->execution);
        if(cast==_derivedCasts.end())continue;
        auto emission=std::find_if(cast->second.emissions.begin(),cast->second.emissions.end(),
            [&](const DerivedEmissionRecord& item){return item.emitter==command.emitter;});
        if(emission!=cast->second.emissions.end() && emission->active)
            ready.push_back(std::move(command));
    }
    _emissionQueue.clear();
    return ready;
}
bool AbilitySystem::emitterActive(Identity emitter) const noexcept {
    for(const auto& [execution,cast]:_derivedCasts)
        for(const auto& item:cast.emissions)
            if(item.emitter==emitter)return item.active;
    return false;
}
Result<FireDeliveryResult> AbilitySystem::fireDelivered(ayt::entity::EntityHandle actor,Id activeSkill) {
    // Actor identity also hydrates authored equipment and skills before a staged release.
    auto key=actorIdentity(actor);if(!key)return std::unexpected(key.error());
    auto equipped=_runtime->equipped(*key);
    if(!equipped)return std::unexpected(equipped.error());
    if(!*equipped)return fail<FireDeliveryResult>(Error::Missing,"equipped item missing");
    auto equipment=_runtime->equipment(**equipped);
    if(!equipment)return std::unexpected(equipment.error());
    auto configured=_fireDeliveries.find(equipment->configuration);
    if(configured==_fireDeliveries.end())
        return fail<FireDeliveryResult>(Error::Missing,"fire delivery is not configured");
    auto* entity=_world->getEntityByHandle(actor);
    auto* component=entity?entity->getComponent<ayt::combat::CombatComponent>():nullptr;
    if(!component || !component->life() || component->life()->phase()!=ayt::combat::LifePhase::Alive)
        return fail<FireDeliveryResult>(Error::Invalid,"actor is not alive");
    auto stunned=_combat->stunned(actor);
    if(!stunned)return fromCombat<FireDeliveryResult>(stunned.error());
    if(*stunned)return fail<FireDeliveryResult>(Error::Invalid,"actor is stunned");
    auto binding=ayt::combat::bindCombatant(*_world,actor);
    if(!binding)return fromCombat<FireDeliveryResult>(binding.error());
    std::uint32_t level=0;
    if(activeSkill){auto skill=_runtime->skill(*key,activeSkill);
        if(!skill)return std::unexpected(skill.error());level=skill->level;}
    DerivedRelease release;
    auto fired=_runtime->fire({*key,_combat->attributes().tick(),activeSkill},
        [&](const FireEvent& event)->Result<void>{
            auto made=makeDerivedRelease(configured->second,event,level,component->team,
                _combat->worldToken(),_nextEmitter);
            if(!made)return std::unexpected(made.error());
            release=std::move(*made);
            return prepareDelivery(actor,*binding,release,event.resolved.cost);
        });
    if(!fired)return std::unexpected(fired.error());
    _nextEmitter+=release.emissions.size();
    auto& record=_derivedCasts.at(fired->execution);
    return FireDeliveryResult{std::move(*fired),advanceDelivery(record,_combat->attributes().tick())};
}
Result<AbilitySystem*> installAbility(ayt::entity::World& world,
    const AuthoringDocument& document) {
    auto catalog=compileAuthoring(document);
    if(!catalog)return std::unexpected(catalog.error());
    auto installed=installAbility(world,*catalog);
    if(!installed)return installed;
    for(const auto& binding:document.fireDeliveries) {
        auto result=(*installed)->configureFireDelivery(binding.owner,binding.definition);
        if(!result)return std::unexpected(result.error());
    }
    for(const auto& binding:document.derivedSkills) {
        auto result=(*installed)->configureDerivedSkill(binding.owner,binding.definition);
        if(!result)return std::unexpected(result.error());
    }
    return installed;
}
Result<AbilitySystem*> installAbility(ayt::entity::World& world,
    const AuthoringDocument& document,const OwnedEffectBindings& effects) {
    if(auto valid=validateOwnedEffectBindings(document,effects);!valid)
        return std::unexpected(valid.error());
    auto installed=installAbility(world,document);
    if(!installed)return installed;
    for(const auto& [reference,definition]:effects) {
        auto bound=(*installed)->configureOwnedEffect(reference,definition);
        if(!bound)return std::unexpected(bound.error());
    }
    return installed;
}
} // namespace ayt::ability
