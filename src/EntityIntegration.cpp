#include <AYAbility/EntityIntegration.h>
#include <AYEntity.h>
#include <array>

namespace ayt::ability {
namespace {
template<class T> Result<T> fail(Error code,std::string message) {
    return std::unexpected(Diagnostic{code,std::move(message),{}});
}
template<class T> Result<T> fromCombat(const ayt::combat::Diagnostic& error) {
    return std::unexpected(Diagnostic{Error::Invalid,error.message,error.path});
}
Identity identity(const ayt::combat::EntityBinding& binding) {
    return {binding.ref.domain,binding.ref.object,binding.ref.generation};
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
        if(!event.resolved.cost || event.resolved.cost->second==0)return {};
        auto* entity=_world->getEntityByHandle(actor);
        auto* stats=entity?entity->getComponent<ayt::stats::StatsComponent>():nullptr;
        if(!stats || !stats->state() || !stats->authority())return fail<void>(Error::Stale,"authoritative Stats missing");
        const auto stat=ayt::stats::statId(event.resolved.cost->first);
        auto row=stats->read(stat);
        if(!row)return fail<void>(Error::Missing,row.error().message);
        if(row->current<event.resolved.cost->second)return fail<void>(Error::Empty,"insufficient skill resource");
        const std::array mutation{ayt::stats::Mutation{stat,ayt::stats::MutationOp::Decrease,event.resolved.cost->second}};
        auto committed=stats->commit(mutation,stats->version(),ayt::stats::NotificationDelivery::NextTick);
        if(!committed)return fail<void>(Error::Invalid,committed.error().message);
        return {};
    });
    if(!fired)return std::unexpected(fired.error());
    FireActionResult result{.shot=std::move(*fired)};
    if(!target)return result;
    for(Value i=0;i<result.shot.projectiles;++i) {
        auto hit=_combat->damage(actor,*target,result.shot.amount,recipe);
        if(!hit){result.combatError=hit.error();break;}
        result.hits.push_back(std::move(*hit));
    }
    return result;
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
