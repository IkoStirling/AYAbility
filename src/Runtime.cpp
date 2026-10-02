#include <AYAbility/Runtime.h>
#include <limits>

namespace ayt::ability {
namespace {
template<class T> Result<T> fail(Error code,std::string message) {
    return std::unexpected(Diagnostic{code,std::move(message),{}});
}
Result<std::uint64_t> future(std::uint64_t tick,Value duration) {
    if(duration<0 || static_cast<std::uint64_t>(duration)>std::numeric_limits<std::uint64_t>::max()-tick)
        return fail<std::uint64_t>(Error::Overflow,"future tick overflow");
    return tick+static_cast<std::uint64_t>(duration);
}
Result<Value> parameter(const ResolvedAction& values,Id key,Unit unit) {return values.value(key,unit);}
Result<void> validateFire(const ResolvedAction& resolved) {
    if(resolved.behaviorId!=fire::Behavior)return fail<void>(Error::Unsupported,"not a fire behavior");
    const auto amount=parameter(resolved,fire::Amount,Unit::Amount);
    const auto interval=parameter(resolved,fire::IntervalTicks,Unit::Ticks);
    const auto minimum=parameter(resolved,fire::MinimumIntervalTicks,Unit::Ticks);
    const auto step=parameter(resolved,fire::IntervalStepTicks,Unit::Ticks);
    const auto reset=parameter(resolved,fire::StreakResetTicks,Unit::Ticks);
    const auto magazine=parameter(resolved,fire::MagazineCapacity,Unit::Count);
    const auto reload=parameter(resolved,fire::ReloadTicks,Unit::Ticks);
    const auto projectiles=parameter(resolved,fire::Projectiles,Unit::Count);
    const auto cooldown=parameter(resolved,fire::SkillCooldownTicks,Unit::Ticks);
    for(const auto* item:{&amount,&interval,&minimum,&step,&reset,&magazine,&reload,&projectiles,&cooldown})
        if(!*item)return std::unexpected(item->error());
    if(*amount<0 || *interval<1 || *minimum<1 || *minimum>*interval || *step<0 || *reset<0 ||
       *magazine<1 || *magazine>1000000 || *reload<1 || *projectiles<1 || *projectiles>1024 || *cooldown<0)
        return fail<void>(Error::Invalid,"invalid fire parameters");
    return {};
}
} // namespace

Result<void> Runtime::addEquipment(Identity actor,Identity equipment,Id configuration) {
    if(_committing)return fail<void>(Error::Busy,"runtime commit in progress");
    if(!_catalog || !actor.valid() || !equipment.valid() || actor.domain!=equipment.domain ||
       _equipment.contains(equipment))return fail<void>(Error::Invalid,"invalid or duplicate equipment");
    const auto* config=_catalog->findEquipment(configuration);
    if(!config)return fail<void>(Error::Missing,"equipment configuration missing");
    auto resolved=_catalog->resolve(config->action,configuration,{});
    if(!resolved)return std::unexpected(resolved.error());
    if(auto validated=validateFire(*resolved);!validated)return validated;
    auto magazine=resolved->value(fire::MagazineCapacity,Unit::Count);
    _equipment.emplace(equipment,EquipmentState{.owner=actor,.identity=equipment,
        .configuration=configuration,.ammunition=*magazine});
    _actors.try_emplace(actor);
    return {};
}
Result<void> Runtime::equip(Identity actor,Identity equipment) {
    if(_committing)return fail<void>(Error::Busy,"runtime commit in progress");
    auto owner=_actors.find(actor);
    auto item=_equipment.find(equipment);
    if(owner==_actors.end() || item==_equipment.end() || item->second.owner!=actor)
        return fail<void>(Error::Missing,"actor equipment missing");
    owner->second.equipped=equipment;
    return {};
}
Result<void> Runtime::grant(Identity actor,Id skillId,std::uint32_t level) {
    if(_committing)return fail<void>(Error::Busy,"runtime commit in progress");
    if(!actor.valid() || !_catalog)return fail<void>(Error::Invalid,"invalid actor or catalog");
    const auto* definition=_catalog->findSkill(skillId);
    if(!definition || !level || level>definition->maxLevel)return fail<void>(Error::Invalid,"invalid skill grant");
    auto& actorState=_actors[actor];
    if(!actorState.skills.emplace(skillId,GrantedSkill{skillId,level,0}).second)
        return fail<void>(Error::Duplicate,"skill already granted");
    return {};
}
Result<void> Runtime::setLevel(Identity actor,Id skillId,std::uint32_t level) {
    if(_committing)return fail<void>(Error::Busy,"runtime commit in progress");
    auto owner=_actors.find(actor);
    const auto* definition=_catalog?_catalog->findSkill(skillId):nullptr;
    if(owner==_actors.end() || !owner->second.skills.contains(skillId) || !definition)
        return fail<void>(Error::Missing,"skill grant missing");
    if(!level || level>definition->maxLevel)return fail<void>(Error::Invalid,"invalid skill level");
    owner->second.skills.at(skillId).level=level;
    return {};
}
Result<void> Runtime::beginReload(Identity actor,std::uint64_t tick) {
    if(_committing)return fail<void>(Error::Busy,"runtime commit in progress");
    auto owner=_actors.find(actor);
    if(owner==_actors.end() || !owner->second.equipped.valid())return fail<void>(Error::Missing,"equipped item missing");
    auto item=_equipment.find(owner->second.equipped);
    if(item==_equipment.end())return fail<void>(Error::Stale,"equipped item stale");
    auto& state=item->second;
    if(state.reloading && tick<state.reloadUntil)return fail<void>(Error::Busy,"already reloading");
    std::vector<SkillLevel> levels;
    for(const auto& [id,granted]:owner->second.skills)levels.push_back({id,granted.level});
    auto resolved=_catalog->resolve(_catalog->findEquipment(state.configuration)->action,state.configuration,levels);
    if(!resolved)return std::unexpected(resolved.error());
    if(auto validated=validateFire(*resolved);!validated)return validated;
    auto capacity=resolved->value(fire::MagazineCapacity,Unit::Count);
    auto duration=resolved->value(fire::ReloadTicks,Unit::Ticks);
    if(state.reloading && tick>=state.reloadUntil) {
        state.reloading=false;
        state.ammunition=*capacity;
    }
    if(state.ammunition>=*capacity)return fail<void>(Error::Invalid,"magazine already full");
    auto until=future(tick,*duration);if(!until)return std::unexpected(until.error());
    state.reloadUntil=*until;state.reloading=true;
    return {};
}
Result<FireEvent> Runtime::fire(FireRequest request,
        const std::function<Result<void>(const FireEvent&)>& preCommit) {
    if(_committing)return fail<FireEvent>(Error::Busy,"runtime commit in progress");
    if(!request.actor.valid() || !_catalog)return fail<FireEvent>(Error::Invalid,"invalid actor or catalog");
    auto owner=_actors.find(request.actor);
    if(owner==_actors.end() || !owner->second.equipped.valid())return fail<FireEvent>(Error::Missing,"equipped item missing");
    auto item=_equipment.find(owner->second.equipped);
    if(item==_equipment.end() || item->second.owner!=request.actor)return fail<FireEvent>(Error::Stale,"equipment stale");
    auto& state=item->second;
    if(state.streak && request.tick<state.lastFireTick)return fail<FireEvent>(Error::Stale,"simulation tick moved backwards");
    std::vector<SkillLevel> levels;
    for(const auto& [id,granted]:owner->second.skills)levels.push_back({id,granted.level});
    const auto* config=_catalog->findEquipment(state.configuration);
    if(!config)return fail<FireEvent>(Error::Stale,"configuration missing");
    auto resolved=_catalog->resolve(config->action,state.configuration,levels,request.activeSkill);
    if(!resolved)return std::unexpected(resolved.error());
    if(auto validated=validateFire(*resolved);!validated)return std::unexpected(validated.error());
    auto capacity=*resolved->value(fire::MagazineCapacity,Unit::Count);
    const bool reloadDone=state.reloading && request.tick>=state.reloadUntil;
    const auto available=reloadDone?capacity:state.ammunition;
    if(state.reloading && !reloadDone)return fail<FireEvent>(Error::Busy,"reloading");
    if(request.tick<state.nextFireTick)return fail<FireEvent>(Error::Cooldown,"equipment interval active");
    if(available<1)return fail<FireEvent>(Error::Empty,"magazine empty");
    GrantedSkill* active=nullptr;
    if(request.activeSkill) {
        auto at=owner->second.skills.find(request.activeSkill);
        if(at==owner->second.skills.end())return fail<FireEvent>(Error::Missing,"active skill not granted");
        active=&at->second;
        if(request.tick<active->nextReadyTick)return fail<FireEvent>(Error::Cooldown,"skill cooldown active");
    }
    auto reset=*resolved->value(fire::StreakResetTicks,Unit::Ticks);
    const auto priorStreak=state.streak && request.tick-state.lastFireTick<=static_cast<std::uint64_t>(reset)?state.streak:0;
    if(priorStreak==std::numeric_limits<std::uint32_t>::max())return fail<FireEvent>(Error::Limit,"streak limit");
    auto base=*resolved->value(fire::IntervalTicks,Unit::Ticks);
    auto minimum=*resolved->value(fire::MinimumIntervalTicks,Unit::Ticks);
    auto step=*resolved->value(fire::IntervalStepTicks,Unit::Ticks);
    const auto acceleration=priorStreak && step>0 &&
        static_cast<std::uint64_t>(priorStreak)>static_cast<std::uint64_t>((base-minimum)/step)
        ?base-minimum:step*static_cast<Value>(priorStreak);
    auto next=future(request.tick,std::max(minimum,base-acceleration));
    if(!next)return std::unexpected(next.error());
    auto skillCooldown=future(request.tick,*resolved->value(fire::SkillCooldownTicks,Unit::Ticks));
    if(!skillCooldown)return std::unexpected(skillCooldown.error());
    if(!_nextExecution || _nextExecution==std::numeric_limits<std::uint64_t>::max())
        return fail<FireEvent>(Error::Limit,"execution identity limit");
    FireEvent event{.execution=_nextExecution,.tick=request.tick,.nextFireTick=*next,
        .actor=request.actor,.equipment=state.identity,.activeSkill=request.activeSkill,
        .amount=*resolved->value(fire::Amount,Unit::Amount),
        .projectiles=*resolved->value(fire::Projectiles,Unit::Count),
        .ammunitionRemaining=available-1,.streak=priorStreak+1,.resolved=std::move(*resolved)};
    if(preCommit) {
        _committing=true;
        Result<void> charged;
        try { charged=preCommit(event); }
        catch(...) { _committing=false;return fail<FireEvent>(Error::Invalid,"pre-commit hook threw"); }
        _committing=false;
        if(!charged)return std::unexpected(charged.error());
    }
    state.ammunition=available-1;state.reloading=false;state.streak=event.streak;
    state.lastFireTick=request.tick;state.nextFireTick=*next;
    if(active)active->nextReadyTick=*skillCooldown;
    ++_nextExecution;
    return event;
}
Result<ActivationEvent> Runtime::activate(ActivationRequest request,
        const std::function<Result<void>(const ActivationEvent&)>& preCommit) {
    if(_committing)return fail<ActivationEvent>(Error::Busy,"runtime commit in progress");
    if(!request.actor.valid() || !request.activeSkill || !_catalog)
        return fail<ActivationEvent>(Error::Invalid,"invalid activation request");
    auto owner=_actors.find(request.actor);
    if(owner==_actors.end())return fail<ActivationEvent>(Error::Missing,"actor missing");
    auto granted=owner->second.skills.find(request.activeSkill);
    if(granted==owner->second.skills.end())return fail<ActivationEvent>(Error::Missing,"active skill not granted");
    const auto* skill=_catalog->findSkill(request.activeSkill);
    if(!skill || skill->kind!=SkillKind::Active)return fail<ActivationEvent>(Error::Invalid,"active skill definition missing");
    if(request.tick<granted->second.nextReadyTick)return fail<ActivationEvent>(Error::Cooldown,"skill cooldown active");
    std::vector<SkillLevel> levels;
    for(const auto& [id,state]:owner->second.skills)levels.push_back({id,state.level});
    auto resolved=_catalog->resolve(skill->action,0,levels,request.activeSkill);
    if(!resolved)return std::unexpected(resolved.error());
    if(resolved->behaviorId!=area::Behavior && resolved->behaviorId!=derived::Behavior)
        return fail<ActivationEvent>(Error::Unsupported,"not an active delivery behavior");
    const Id cooldownParameter=resolved->behaviorId==derived::Behavior
        ?derived::CooldownTicks:area::CooldownTicks;
    auto cooldown=resolved->value(cooldownParameter,Unit::Ticks);
    if(!cooldown || *cooldown<0)return fail<ActivationEvent>(Error::Invalid,"invalid activation cooldown");
    auto next=future(request.tick,*cooldown);if(!next)return std::unexpected(next.error());
    if(!_nextExecution || _nextExecution==std::numeric_limits<std::uint64_t>::max())
        return fail<ActivationEvent>(Error::Limit,"execution identity limit");
    ActivationEvent event{_nextExecution,request.tick,request.actor,request.activeSkill,std::move(*resolved)};
    if(preCommit) {
        _committing=true;
        Result<void> committed;
        try{committed=preCommit(event);}
        catch(...){_committing=false;return fail<ActivationEvent>(Error::Invalid,"pre-commit hook threw");}
        _committing=false;
        if(!committed)return std::unexpected(committed.error());
    }
    granted->second.nextReadyTick=*next;
    ++_nextExecution;
    return event;
}
Result<EquipmentState> Runtime::equipment(Identity identity) const {
    auto at=_equipment.find(identity);
    if(at==_equipment.end())return fail<EquipmentState>(Error::Missing,"equipment missing");
    return at->second;
}
Result<GrantedSkill> Runtime::skill(Identity actor,Id skillId) const {
    auto owner=_actors.find(actor);
    if(owner==_actors.end() || !owner->second.skills.contains(skillId))
        return fail<GrantedSkill>(Error::Missing,"skill missing");
    return owner->second.skills.at(skillId);
}
Result<std::optional<Identity>> Runtime::equipped(Identity actor) const {
    auto owner=_actors.find(actor);
    if(owner==_actors.end())return fail<std::optional<Identity>>(Error::Missing,"actor missing");
    if(!owner->second.equipped.valid())return std::optional<Identity>{};
    return owner->second.equipped;
}
Result<void> Runtime::removeActor(Identity actor) {
    if(_committing)return fail<void>(Error::Busy,"runtime commit in progress");
    if(!_actors.erase(actor))return fail<void>(Error::Missing,"actor missing");
    for(auto at=_equipment.begin();at!=_equipment.end();)
        if(at->second.owner==actor)at=_equipment.erase(at);else ++at;
    return {};
}
Result<RuntimeCheckpoint> Runtime::checkpoint() const {
    if(_committing)return fail<RuntimeCheckpoint>(Error::Busy,"runtime commit in progress");
    RuntimeCheckpoint snapshot;
    snapshot.nextExecution=_nextExecution;
    snapshot.actors.reserve(_actors.size());
    snapshot.equipment.reserve(_equipment.size());
    for(const auto& [id,state]:_actors) {
        RuntimeCheckpoint::Actor actor{.identity=id,.equipped=state.equipped};
        actor.skills.reserve(state.skills.size());
        for(const auto& [skill,grant]:state.skills)actor.skills.push_back(grant);
        snapshot.actors.push_back(std::move(actor));
    }
    for(const auto& [id,state]:_equipment)snapshot.equipment.push_back(state);
    return snapshot;
}
Result<void> Runtime::restoreCheckpoint(const RuntimeCheckpoint& snapshot) {
    if(_committing)return fail<void>(Error::Busy,"runtime commit in progress");
    if(!_catalog || snapshot.version!=1 || !snapshot.nextExecution)
        return fail<void>(Error::Invalid,"invalid runtime checkpoint header");
    Runtime restored(_catalog);
    restored._nextExecution=snapshot.nextExecution;
    for(const auto& source:snapshot.actors) {
        if(!source.identity.valid() ||
           (source.equipped.valid() && source.equipped.domain!=source.identity.domain))
            return fail<void>(Error::Invalid,"invalid checkpoint actor identity");
        ActorState state{.equipped=source.equipped};
        for(const auto& grant:source.skills) {
            const auto* definition=_catalog->findSkill(grant.skill);
            if(!definition || !grant.level || grant.level>definition->maxLevel ||
               !state.skills.emplace(grant.skill,grant).second)
                return fail<void>(Error::Invalid,"invalid checkpoint skill grant");
        }
        if(!restored._actors.emplace(source.identity,std::move(state)).second)
            return fail<void>(Error::Duplicate,"duplicate checkpoint actor");
    }
    for(const auto& source:snapshot.equipment) {
        if(!source.identity.valid() || !source.owner.valid() ||
           source.identity.domain!=source.owner.domain ||
           !restored._actors.contains(source.owner))
            return fail<void>(Error::Invalid,"invalid checkpoint equipment owner");
        const auto* config=_catalog->findEquipment(source.configuration);
        if(!config)return fail<void>(Error::Missing,"checkpoint equipment configuration missing");
        std::vector<SkillLevel> levels;
        for(const auto& [id,grant]:restored._actors.at(source.owner).skills)
            levels.push_back({id,grant.level});
        auto resolved=_catalog->resolve(config->action,source.configuration,levels);
        if(!resolved)return std::unexpected(resolved.error());
        if(auto valid=validateFire(*resolved);!valid)return valid;
        const auto capacity=resolved->value(fire::MagazineCapacity,Unit::Count);
        if(!capacity || source.ammunition<0 || source.ammunition>*capacity ||
           (source.reloading && !source.reloadUntil))
            return fail<void>(Error::Invalid,"invalid checkpoint ammunition or reload");
        if(!restored._equipment.emplace(source.identity,source).second)
            return fail<void>(Error::Duplicate,"duplicate checkpoint equipment");
    }
    for(const auto& [id,actor]:restored._actors)if(actor.equipped.valid()) {
        const auto item=restored._equipment.find(actor.equipped);
        if(item==restored._equipment.end() || item->second.owner!=id)
            return fail<void>(Error::Invalid,"checkpoint equipped slot does not belong to actor");
    }
    _actors.swap(restored._actors);
    _equipment.swap(restored._equipment);
    _nextExecution=restored._nextExecution;
    return {};
}
} // namespace ayt::ability
