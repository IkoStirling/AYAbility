#include <AYAbility/EntityIntegration.h>
#include <AYEntity.h>
#include <array>
#include <exception>
#include <limits>

namespace ayt::ability {
namespace {
template<class T> Result<T> fail(Error code,std::string message) {
    return std::unexpected(Diagnostic{code,std::move(message),{}});
}
template<class T> Result<T> fromCombat(const ayt::combat::Diagnostic& error) {
    const auto code=error.code==ayt::combat::Error::AlreadyHit?Error::Duplicate:
        error.code==ayt::combat::Error::Closed?Error::Stale:Error::Invalid;
    return std::unexpected(Diagnostic{code,error.message,error.path});
}
Identity identity(const ayt::combat::EntityBinding& binding) {
    return {binding.ref.domain,binding.ref.object,binding.ref.generation};
}
ayt::combat::ActionResult gameplayResult(const ayt::combat::CombatOutcome& outcome) {
    ayt::combat::ActionResult result{outcome.kind,outcome.calculated,0,outcome.remaining,0,
        outcome.attack.id,outcome.decision,outcome.rule,outcome.life};
    if(!outcome.settlement.changes.empty()) {
        const auto& change=outcome.settlement.changes.front();
        result.actual=outcome.operation==ayt::combat::Operation::Damage?-change.actual:change.actual;
        result.excess=change.excess;
    }
    return result;
}
ayt::combat::HitScope hitScope(DerivedHitMode mode) {
    switch(mode) {
    case DerivedHitMode::Part:return ayt::combat::HitScope::Part;
    case DerivedHitMode::Segment:return ayt::combat::HitScope::Segment;
    case DerivedHitMode::Periodic:return ayt::combat::HitScope::Periodic;
    default:return ayt::combat::HitScope::Target;
    }
}
} // namespace

Result<void> AbilitySystem::bind(ayt::entity::World& world,ayt::combat::CombatSystem& combat,
        std::shared_ptr<const Catalog> catalog) {
    if(!catalog)return fail<void>(Error::Invalid,"Ability catalog missing");
    if(_world && (&world!=_world || _combat!=&combat || _catalog!=catalog))
        return fail<void>(Error::Stale,"AbilitySystem already bound to a different owner or catalog");
    _world=&world;_combat=&combat;
    if(!_runtime) {_catalog=std::move(catalog);_runtime.emplace(_catalog);}
    return {};
}
Result<Identity> AbilitySystem::actorIdentity(ayt::entity::EntityHandle handle) const {
    if(!_world || !_combat)return fail<Identity>(Error::Stale,"Ability World unavailable");
    auto binding=ayt::combat::bindCombatant(*_world,handle);
    if(!binding)return fromCombat<Identity>(binding.error());
    auto key=identity(*binding);
    if(auto record=_actors.find(key);record!=_actors.end()) {
        if(record->second.statsGeneration!=binding->statsGeneration ||
           record->second.combatGeneration!=binding->combatGeneration)
            return fail<Identity>(Error::Stale,"actor component replaced");
    }
    return key;
}
Identity AbilitySystem::equipmentIdentity(Id instance) const noexcept {
    return {_combat?_combat->worldToken():0,instance,1};
}
Result<void> AbilitySystem::addEquipment(ayt::entity::EntityHandle actor,Id equipmentInstance,Id configuration) {
    if(!_world || !_runtime)return fail<void>(Error::Stale,"Ability World unavailable");
    auto binding=ayt::combat::bindCombatant(*_world,actor);
    if(!binding)return fromCombat<void>(binding.error());
    auto key=identity(*binding);
    if(auto existing=_actors.find(key);existing!=_actors.end() &&
       (existing->second.statsGeneration!=binding->statsGeneration ||
        existing->second.combatGeneration!=binding->combatGeneration))
        return fail<void>(Error::Stale,"actor component replaced");
    auto added=_runtime->addEquipment(key,equipmentIdentity(equipmentInstance),configuration);
    if(!added)return added;
    _actors.try_emplace(key,ActorRecord{actor,binding->statsGeneration,binding->combatGeneration});
    return {};
}
Result<void> AbilitySystem::equip(ayt::entity::EntityHandle actor,Id equipmentInstance) {
    auto key=actorIdentity(actor);if(!key)return std::unexpected(key.error());
    return _runtime->equip(*key,equipmentIdentity(equipmentInstance));
}
Result<void> AbilitySystem::grant(ayt::entity::EntityHandle actor,Id skillId,std::uint32_t level) {
    if(!_world || !_runtime)return fail<void>(Error::Stale,"Ability World unavailable");
    auto binding=ayt::combat::bindCombatant(*_world,actor);
    if(!binding)return fromCombat<void>(binding.error());
    auto key=identity(*binding);
    if(auto existing=_actors.find(key);existing!=_actors.end() &&
       (existing->second.statsGeneration!=binding->statsGeneration ||
        existing->second.combatGeneration!=binding->combatGeneration))
        return fail<void>(Error::Stale,"actor component replaced");
    auto result=_runtime->grant(key,skillId,level);
    if(result)_actors.try_emplace(key,ActorRecord{actor,binding->statsGeneration,binding->combatGeneration});
    return result;
}
Result<void> AbilitySystem::setLevel(ayt::entity::EntityHandle actor,Id skillId,std::uint32_t level) {
    auto key=actorIdentity(actor);if(!key)return std::unexpected(key.error());
    return _runtime->setLevel(*key,skillId,level);
}
Result<void> AbilitySystem::beginReload(ayt::entity::EntityHandle actor) {
    auto key=actorIdentity(actor);if(!key)return std::unexpected(key.error());
    return _runtime->beginReload(*key,_combat->attributes().tick());
}
Result<void> AbilitySystem::chargeCost(ayt::entity::EntityHandle actor,
        const std::optional<std::pair<std::string,Value>>& cost) {
    if(!cost || cost->second==0)return {};
    auto* entity=_world->getEntityByHandle(actor);
    auto* stats=entity?entity->getComponent<ayt::stats::StatsComponent>():nullptr;
    if(!stats || !stats->state() || !stats->authority())return fail<void>(Error::Stale,"authoritative Stats missing");
    const auto stat=ayt::stats::statId(cost->first);
    auto row=stats->read(stat);
    if(!row)return fail<void>(Error::Missing,row.error().message);
    if(row->current<cost->second)return fail<void>(Error::Empty,"insufficient skill resource");
    const std::array mutation{ayt::stats::Mutation{stat,ayt::stats::MutationOp::Decrease,cost->second}};
    auto committed=stats->commit(mutation,stats->version(),ayt::stats::NotificationDelivery::NextTick);
    if(!committed)return fail<void>(Error::Invalid,committed.error().message);
    return {};
}
Result<FireActionResult> AbilitySystem::fireAt(ayt::entity::EntityHandle actor,
        std::optional<ayt::entity::EntityHandle> target,Id activeSkill,std::string_view recipe) {
    auto key=actorIdentity(actor);if(!key)return std::unexpected(key.error());
    auto* entity=_world->getEntityByHandle(actor);
    auto* component=entity?entity->getComponent<ayt::combat::CombatComponent>():nullptr;
    if(!component || !component->life() || component->life()->phase()!=ayt::combat::LifePhase::Alive)
        return fail<FireActionResult>(Error::Invalid,"actor is not alive");
    auto stunned=_combat->stunned(actor);
    if(!stunned)return fromCombat<FireActionResult>(stunned.error());
    if(*stunned)return fail<FireActionResult>(Error::Invalid,"actor is stunned");
    const auto tick=_combat->attributes().tick();
    auto fired=_runtime->fire({*key,tick,activeSkill},[&](const FireEvent& event)->Result<void>{
        return chargeCost(actor,event.resolved.cost);
    });
    if(!fired)return std::unexpected(fired.error());
    FireActionResult result{.shot=std::move(*fired)};
    if(!target)return result;
    auto binding=ayt::combat::bindCombatant(*_world,*target);
    if(!binding){result.combatError=binding.error();return result;}
    auto* targetEntity=_world->getEntityByHandle(*target);
    auto* targetStats=targetEntity?targetEntity->getComponent<ayt::stats::StatsComponent>():nullptr;
    auto* targetCombat=targetEntity?targetEntity->getComponent<ayt::combat::CombatComponent>():nullptr;
    if(!targetStats || !targetCombat || !targetCombat->life()) {
        result.combatError={ayt::combat::Error::Missing,"target life resource is not configured",{}};
        return result;
    }
    if(tick==std::numeric_limits<std::uint64_t>::max()) {
        result.combatError={ayt::combat::Error::Limit,"one-shot tick exhausted",{}};
        return result;
    }
    auto nextAttack=_combat->hits().nextAttackId(_combat->worldToken());
    if(!nextAttack){result.combatError=nextAttack.error();return result;}
    const ayt::combat::AttackRef attack{_combat->worldToken(),*nextAttack};
    auto opened=_combat->hits().open(attack,{ayt::combat::HitScope::Segment},tick,tick+1);
    if(!opened){result.combatError=opened.error();return result;}
    struct CloseAttack {
        ayt::combat::HitLedger& ledger;
        ayt::combat::AttackRef attack;
        ~CloseAttack(){if(!ledger.close(attack))std::terminate();}
    } close{_combat->hits(),attack};
    for(Value i=0;i<result.shot.projectiles;++i) {
        const auto resource=targetCombat->life()->policy().resource;
        auto prepared=_combat->attributes().prepareCalculation(*target,resource,
            ayt::stats::AttributeOp::Decrease,result.shot.amount,recipe,actor);
        if(!prepared) {
            result.combatError={ayt::combat::Error::NumericFailure,prepared.error().message,prepared.error().path};
            break;
        }
        ayt::combat::SettlementRequest request;
        request.hit={attack,static_cast<std::uint64_t>(i+1),binding->ref,tick,0,
            static_cast<std::uint32_t>(i+1)};
        request.source.instigator=ayt::combat::CombatantRef{key->domain,key->object,key->generation};
        request.source.directSource=ayt::combat::CombatantRef{
            result.shot.equipment.domain,result.shot.equipment.object,result.shot.equipment.generation};
        request.source.team=component->team;
        request.source.attribution=key->object;
        request.source.ability=result.shot.activeSkill?result.shot.activeSkill:result.shot.resolved.action;
        request.source.abilityInstance=result.shot.execution;
        request.filter={ayt::combat::Operation::Damage,ayt::combat::mask(ayt::combat::AllowedTarget::Hostile)};
        request.resource=resource;
        request.expectedVersion=targetStats->version();
        request.calculation=prepared->context;
        auto outcome=_combat->submit(*binding,std::move(request),*prepared->plan,prepared->inputs);
        if(!outcome){result.combatError=outcome.error();break;}
        result.hits.push_back(gameplayResult(*outcome));
        result.settlements.push_back(std::move(*outcome));
    }
    return result;
}
Result<void> AbilitySystem::configureAreaSkill(Id skill,ayt::combat::AreaDefinition definition,
        AreaProviderFactory provider) {
    if(!_catalog || !_runtime || !provider)return fail<void>(Error::Invalid,"area skill owner or provider missing");
    const auto* authored=_catalog->findSkill(skill);
    const auto* action=authored?_catalog->findAction(authored->action):nullptr;
    if(!authored || authored->kind!=SkillKind::Active || !action || action->behaviorId!=area::Behavior)
        return fail<void>(Error::Invalid,"skill is not an active area behavior");
    _areaSkills.insert_or_assign(skill,AreaSkill{std::move(definition),std::move(provider)});
    return {};
}
Result<AreaCastResult> AbilitySystem::castArea(ayt::entity::EntityHandle actor,Id skill) {
    auto key=actorIdentity(actor);if(!key)return std::unexpected(key.error());
    auto configured=_areaSkills.find(skill);
    if(configured==_areaSkills.end())return fail<AreaCastResult>(Error::Missing,"area skill is not configured");
    ayt::combat::Result<ayt::combat::AreaCandidateProvider> provider;
    try{provider=configured->second.provider(actor);}
    catch(...){return fail<AreaCastResult>(Error::Invalid,"area provider factory threw");}
    if(!provider)return fromCombat<AreaCastResult>(provider.error());
    auto started=_combat->startArea(actor,configured->second.definition,std::move(*provider));
    if(!started)return fromCombat<AreaCastResult>(started.error());
    auto activated=_runtime->activate({*key,_combat->attributes().tick(),skill},
        [&](const ActivationEvent& event){return chargeCost(actor,event.resolved.cost);});
    if(!activated) {
        auto rollback=_combat->stopArea(*started);
        if(!rollback || !rollback->ended)return fail<AreaCastResult>(Error::Invalid,"area activation rollback failed");
        return std::unexpected(activated.error());
    }
    return AreaCastResult{std::move(*activated),*started};
}
Result<ayt::combat::AreaSweep> AbilitySystem::stopArea(std::uint64_t area) {
    if(!_combat)return fail<ayt::combat::AreaSweep>(Error::Stale,"Combat World unavailable");
    auto stopped=_combat->stopArea(area);
    if(!stopped)return fromCombat<ayt::combat::AreaSweep>(stopped.error());
    return *stopped;
}
Result<void> AbilitySystem::configureDerivedSkill(Id skill,DerivedDefinition definition) {
    if(!_catalog || !_runtime)return fail<void>(Error::Stale,"Ability catalog unavailable");
    const auto* authored=_catalog->findSkill(skill);
    const auto* action=authored?_catalog->findAction(authored->action):nullptr;
    if(!authored || authored->kind!=SkillKind::Active || !action ||
       action->behaviorId!=derived::Behavior)
        return fail<void>(Error::Invalid,"skill is not an active derived behavior");
    if(auto valid=validateDerived(definition);!valid)return valid;
    for(const auto& stage:definition.stages)if(stage.amountParameter) {
        const auto parameter=std::find_if(action->defaults.begin(),action->defaults.end(),
            [&](const Parameter& value){return value.id==stage.amountParameter;});
        if(parameter==action->defaults.end() || parameter->unit!=Unit::Amount)
            return fail<void>(Error::Invalid,"derived stage amount parameter missing or wrong unit");
    }
    _derivedSkills.insert_or_assign(skill,std::move(definition));
    return {};
}
bool AbilitySystem::derivedSourceAvailable(const DerivedCastRecord& record) const {
    if(!_world || !_combat)return false;
    auto bound=ayt::combat::bindCombatant(*_world,record.actor);
    if(!bound || identity(*bound)!=record.source->actor ||
       bound->statsGeneration!=record.statsGeneration ||
       bound->combatGeneration!=record.combatGeneration)return false;
    auto* entity=_world->getEntityByHandle(record.actor);
    auto* component=entity?entity->getComponent<ayt::combat::CombatComponent>():nullptr;
    return component && component->life() &&
        component->life()->phase()==ayt::combat::LifePhase::Alive;
}
void AbilitySystem::closeDerived(DerivedCastRecord& record) {
    for(auto& [id,stage]:record.stages)if(stage.open) {
        (void)_combat->hits().close(stage.attack);
        stage.open=false;
    }
    for(auto& emission:record.emissions)emission.active=false;
}
Result<DerivedCastResult> AbilitySystem::castDerived(ayt::entity::EntityHandle actor,Id skillId) {
    auto key=actorIdentity(actor);if(!key)return std::unexpected(key.error());
    const auto configured=_derivedSkills.find(skillId);
    if(configured==_derivedSkills.end())
        return fail<DerivedCastResult>(Error::Missing,"derived skill is not configured");
    if(_derivedCasts.size()>=128)return fail<DerivedCastResult>(Error::Limit,"active derived cast limit");
    auto* entity=_world->getEntityByHandle(actor);
    auto* component=entity?entity->getComponent<ayt::combat::CombatComponent>():nullptr;
    if(!component || !component->life() || component->life()->phase()!=ayt::combat::LifePhase::Alive)
        return fail<DerivedCastResult>(Error::Invalid,"actor is not alive");
    auto stunned=_combat->stunned(actor);
    if(!stunned)return fromCombat<DerivedCastResult>(stunned.error());
    if(*stunned)return fail<DerivedCastResult>(Error::Invalid,"actor is stunned");
    auto level=_runtime->skill(*key,skillId);
    if(!level)return std::unexpected(level.error());
    auto equipment=_runtime->equipped(*key);
    if(!equipment)return std::unexpected(equipment.error());
    auto binding=ayt::combat::bindCombatant(*_world,actor);
    if(!binding)return fromCombat<DerivedCastResult>(binding.error());
    DerivedRelease release;
    auto activated=_runtime->activate({*key,_combat->attributes().tick(),skillId},
        [&](const ActivationEvent& event)->Result<void>{
            auto made=makeDerivedRelease(configured->second,event,*equipment,level->level,
                component->team,_combat->worldToken(),_nextEmitter);
            if(!made)return std::unexpected(made.error());
            release=std::move(*made);
            auto [at,inserted]=_derivedCasts.try_emplace(event.execution);
            if(!inserted)return fail<void>(Error::Duplicate,"derived cast already exists");
            auto rollback=[&](){closeDerived(at->second);_derivedCasts.erase(at);};
            try {
                auto& record=at->second;
                record.source=release.source;record.actor=actor;
                record.statsGeneration=binding->statsGeneration;
                record.combatGeneration=binding->combatGeneration;
                record.emissions.reserve(release.emissions.size());
                for(const auto& command:release.emissions)
                    record.emissions.push_back({command.emitter,command.stage,true});
                for(const auto& stage:release.stages)
                    record.stages.emplace(stage.id,DerivedStageRecord{stage,{},
                        event.tick+stage.lifetimeTicks,1,false});
                for(const auto& stage:release.stages) {
                    auto next=_combat->hits().nextAttackId(_combat->worldToken());
                    if(!next){auto error=next.error();rollback();return fromCombat<void>(error);}
                    ayt::combat::AttackRef attack{_combat->worldToken(),*next};
                    auto opened=_combat->hits().open(attack,
                        {hitScope(stage.hitMode),stage.intervalTicks},event.tick,
                        event.tick+stage.lifetimeTicks);
                    if(!opened){auto error=opened.error();rollback();return fromCombat<void>(error);}
                    auto& current=record.stages.at(stage.id);
                    current.attack=attack;current.open=true;
                }
                auto charged=chargeCost(actor,event.resolved.cost);
                if(!charged){rollback();return charged;}
                return {};
            } catch(...) {
                rollback();
                return fail<void>(Error::Invalid,"derived cast preparation threw");
            }
        });
    if(!activated)return std::unexpected(activated.error());
    _nextEmitter+=release.emissions.size();
    return DerivedCastResult{std::move(*activated),std::move(release.emissions)};
}
Result<DerivedImpactResult> AbilitySystem::reportDerivedHit(Identity emitter,
        ayt::entity::EntityHandle target,std::uint32_t part,std::uint32_t segment) {
    if(!_world || !_combat)return fail<DerivedImpactResult>(Error::Stale,"Ability World unavailable");
    DerivedCastRecord* record=nullptr;
    Id stageId=0;
    for(auto& [execution,cast]:_derivedCasts) {
        auto at=std::find_if(cast.emissions.begin(),cast.emissions.end(),
            [&](const DerivedEmissionRecord& item){return item.emitter==emitter;});
        if(at!=cast.emissions.end()) {
            if(!at->active)return fail<DerivedImpactResult>(Error::Stale,"emitter finished");
            record=&cast;stageId=at->stage;break;
        }
    }
    if(!record)return fail<DerivedImpactResult>(Error::Missing,"derived emitter missing");
    auto stage=record->stages.find(stageId);
    if(stage==record->stages.end() || !stage->second.open)
        return fail<DerivedImpactResult>(Error::Stale,"derived stage ended");
    const auto tick=_combat->attributes().tick();
    if(tick>=stage->second.endTick)
        return fail<DerivedImpactResult>(Error::Stale,"derived stage expired");
    const bool sourceLive=derivedSourceAvailable(*record);
    if(!sourceLive && record->source->sourceLoss==SourceLossPolicy::Cancel)
        return fail<DerivedImpactResult>(Error::Stale,"derived source unavailable");
    auto bound=ayt::combat::bindCombatant(*_world,target);
    if(!bound)return fromCombat<DerivedImpactResult>(bound.error());
    auto* entity=_world->getEntityByHandle(target);
    auto* stats=entity?entity->getComponent<ayt::stats::StatsComponent>():nullptr;
    auto* combat=entity?entity->getComponent<ayt::combat::CombatComponent>():nullptr;
    if(!stats || !combat || !combat->life())
        return fail<DerivedImpactResult>(Error::Missing,"target life resource unavailable");
    auto& current=stage->second;
    if(!current.nextRequest || current.nextRequest>4096)
        return fail<DerivedImpactResult>(Error::Limit,"derived stage report limit");
    const auto resource=combat->life()->policy().resource;
    std::optional<ayt::entity::EntityHandle> sourceHandle;
    if(sourceLive)sourceHandle=record->actor;
    auto prepared=_combat->attributes().prepareCalculation(target,resource,
        ayt::stats::AttributeOp::Decrease,current.stage.amount,current.stage.recipe,sourceHandle);
    if(!prepared)return fail<DerivedImpactResult>(Error::Invalid,prepared.error().message);
    ayt::combat::SettlementRequest request;
    request.hit={current.attack,current.nextRequest++,bound->ref,tick,part,segment};
    request.source.instigator=ayt::combat::CombatantRef{record->source->actor.domain,
        record->source->actor.object,record->source->actor.generation};
    request.source.directSource=ayt::combat::CombatantRef{
        emitter.domain,emitter.object,emitter.generation};
    request.source.team=record->source->team;
    request.source.attribution=record->source->actor.object;
    request.source.ability=record->source->skill;
    request.source.effect=stageId;
    request.source.abilityInstance=record->source->execution;
    request.filter={ayt::combat::Operation::Damage,
        ayt::combat::mask(ayt::combat::AllowedTarget::Hostile)};
    request.resource=resource;
    request.expectedVersion=stats->version();
    request.calculation=prepared->context;
    auto outcome=_combat->submit(*bound,std::move(request),*prepared->plan,prepared->inputs);
    if(!outcome)return fromCombat<DerivedImpactResult>(outcome.error());
    auto hit=gameplayResult(*outcome);
    return DerivedImpactResult{record->source,std::move(hit),std::move(*outcome)};
}
Result<void> AbilitySystem::finishDerivedEmitter(Identity emitter) {
    for(auto at=_derivedCasts.begin();at!=_derivedCasts.end();++at) {
        auto& record=at->second;
        auto found=std::find_if(record.emissions.begin(),record.emissions.end(),
            [&](const DerivedEmissionRecord& item){return item.emitter==emitter;});
        if(found==record.emissions.end())continue;
        if(!found->active)return fail<void>(Error::Stale,"emitter already finished");
        found->active=false;
        auto& stage=record.stages.at(found->stage);
        if(std::none_of(record.emissions.begin(),record.emissions.end(),
            [&](const DerivedEmissionRecord& item){return item.active && item.stage==found->stage;})) {
            if(stage.open){(void)_combat->hits().close(stage.attack);stage.open=false;}
        }
        if(std::none_of(record.emissions.begin(),record.emissions.end(),
            [](const DerivedEmissionRecord& item){return item.active;}))_derivedCasts.erase(at);
        return {};
    }
    return fail<void>(Error::Missing,"derived emitter missing");
}
Result<void> AbilitySystem::cancelDerivedCast(std::uint64_t execution) {
    auto at=_derivedCasts.find(execution);
    if(at==_derivedCasts.end())return fail<void>(Error::Missing,"derived cast missing");
    closeDerived(at->second);_derivedCasts.erase(at);
    return {};
}
Result<EquipmentState> AbilitySystem::equipment(Id instance) const {
    if(!_runtime)return fail<EquipmentState>(Error::Stale,"Ability runtime unavailable");
    return _runtime->equipment(equipmentIdentity(instance));
}
Result<GrantedSkill> AbilitySystem::skill(ayt::entity::EntityHandle actor,Id skillId) const {
    auto key=actorIdentity(actor);if(!key)return std::unexpected(key.error());
    return _runtime->skill(*key,skillId);
}
void AbilitySystem::onUpdate(float) {
    if(!_world || !_runtime)return;
    const auto tick=_combat->attributes().tick();
    for(auto at=_derivedCasts.begin();at!=_derivedCasts.end();) {
        auto& record=at->second;
        if(record.source->sourceLoss==SourceLossPolicy::Cancel && !derivedSourceAvailable(record)) {
            closeDerived(record);at=_derivedCasts.erase(at);continue;
        }
        for(auto& [id,stage]:record.stages)if(stage.open && tick>=stage.endTick) {
            (void)_combat->hits().close(stage.attack);stage.open=false;
            for(auto& emission:record.emissions)if(emission.stage==id)emission.active=false;
        }
        if(std::none_of(record.emissions.begin(),record.emissions.end(),
            [](const DerivedEmissionRecord& item){return item.active;}))at=_derivedCasts.erase(at);
        else ++at;
    }
    for(auto at=_actors.begin();at!=_actors.end();) {
        auto binding=ayt::combat::bindCombatant(*_world,at->second.handle);
        if(!binding || identity(*binding)!=at->first ||
           binding->statsGeneration!=at->second.statsGeneration ||
           binding->combatGeneration!=at->second.combatGeneration) {
            (void)_runtime->removeActor(at->first);
            at=_actors.erase(at);
        } else ++at;
    }
}
Result<void> registerAbilityGameplayComponents(ayt::entity::ComponentRegistry& registry) {
    auto result=ayt::combat::registerCombatGameplayComponents(registry);
    if(!result)return fromCombat<void>(result.error());
    return {};
}
Result<AbilitySystem*> installAbility(ayt::entity::World& world,std::shared_ptr<const Catalog> catalog) {
    if(!catalog)return fail<AbilitySystem*>(Error::Invalid,"Ability catalog missing");
    auto combat=ayt::combat::installCombat(world);
    if(!combat)return fromCombat<AbilitySystem*>(combat.error());
    if(!world.findSystemByName("AbilitySystem"))
        world.registerSystem<AbilitySystem>(140,ayt::entity::SystemLane::Sim);
    auto* system=dynamic_cast<AbilitySystem*>(world.findSystemByName("AbilitySystem"));
    if(!system)return fail<AbilitySystem*>(Error::Invalid,"AbilitySystem name collision");
    auto bound=system->bind(world,**combat,std::move(catalog));
    if(!bound)return std::unexpected(bound.error());
    return system;
}
} // namespace ayt::ability
