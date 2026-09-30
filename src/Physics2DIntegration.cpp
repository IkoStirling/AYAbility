#include <AYAbility/Physics2DIntegration.h>
#include <AYPhysics/PhysicsWorld2D.h>
#include <AYEntity.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace ayt::ability {
namespace {
template<class T> Result<T> invalid(std::string message) {
    return std::unexpected(Diagnostic{Error::Invalid,std::move(message),{}});
}
ayt::math::FVector3 vector(const std::array<float,3>& value) {
    return {value[0],value[1],value[2]};
}
}
Result<void> Ability2DDeliverySystem::configure(Id action,Id stage,Delivery2DProfile profile) {
    if(!action || !stage)return invalid<void>("2D stage identity missing");
    if(auto valid=validateDelivery2DProfile(profile);!valid)return valid;
    _profiles.insert_or_assign({action,stage},profile);
    return {};
}
Result<void> Ability2DDeliverySystem::configure(const AuthoringDocument& document) {
    if(auto valid=compileAuthoring(document);!valid)return std::unexpected(valid.error());
    std::map<std::pair<Id,Id>,Delivery2DProfile> profiles;
    for(const auto& binding:document.delivery2D)
        profiles.emplace(std::pair{binding.action,binding.stage},binding.profile);
    for(auto [fire,bindings]:{std::pair{true,&document.fireDeliveries},
                              std::pair{false,&document.derivedSkills}})
        for(const auto& binding:*bindings) {
            Id action=0;
            if(fire) {
                auto config=std::find_if(document.equipment.begin(),document.equipment.end(),
                    [&](const EquipmentActionConfig& item){return item.id==binding.owner;});
                action=config->action;
            } else {
                auto skill=std::find_if(document.skills.begin(),document.skills.end(),
                    [&](const SkillDefinition& item){return item.id==binding.owner;});
                action=skill->action;
            }
            for(const auto& stage:binding.definition.stages)
                if(!profiles.contains({action,stage.id}))
                    return invalid<void>("2D delivery profile missing for authored stage");
        }
    _profiles=std::move(profiles);
    return {};
}
void Ability2DDeliverySystem::bind(ayt::entity::World& world,AbilitySystem& ability,
    ayt::physics::PhysicsManager& physics) {
    _world=&world;_ability=&ability;_physics=&physics;
    _projectiles.clear();_errors.clear();
}
ayt::entity::EntityHandle Ability2DDeliverySystem::target(ayt::physics::BodyHandle body) const {
    for(auto* entity:_world->getAllEntities()) {
        auto* rigid=entity->getComponent<ayt::entity::RigidBodyComponent>();
        if(rigid && rigid->getPhysicsDimension()==
           ayt::entity::RigidBodyComponent::PhysicsDimension::TwoD &&
           rigid->getBodyHandle()==body)
            return _world->getEntityHandle(entity->getId());
    }
    return {};
}
void Ability2DDeliverySystem::onUpdate(float fixedSeconds) {
    if(!_world || !_ability || !_physics)return;
    auto* physics=_physics->world2D();
    if(!physics){_errors.push_back({Error::Stale,"2D physics world unavailable",{}});return;}
    std::set<Identity> advanced;
    for(std::size_t round=0;round<16;++round) {
        auto commands=_ability->takeEmissions();
        if(round && commands.empty())break;
        for(auto& command:commands) {
            auto profile=_profiles.find({command.source->action,command.stage});
            if(profile==_profiles.end()) {
                _errors.push_back({Error::Missing,"2D delivery profile missing",{}});
                (void)_ability->finishDerivedEmitter(command.emitter);
                continue;
            }
            ayt::math::FVector3 origin=vector(profile->second.spawnOffset);
            ayt::physics::BodyHandle sourceBody=ayt::physics::InvalidBodyHandle;
            if(command.origin)
                origin=origin+vector(*command.origin);
            for(auto* entity:_world->getAllEntities()) {
                const auto handle=_world->getEntityHandle(entity->getId());
                auto binding=ayt::combat::bindCombatant(*_world,handle);
                if(!binding || binding->ref.domain!=command.source->actor.domain ||
                   binding->ref.object!=command.source->actor.object ||
                   binding->ref.generation!=command.source->actor.generation)continue;
                if(!command.origin)
                    if(auto* transform=entity->getComponent<ayt::entity::Transform>())
                        origin=origin+transform->position;
                if(auto* rigid=entity->getComponent<ayt::entity::RigidBodyComponent>();
                   rigid && rigid->getPhysicsDimension()==
                       ayt::entity::RigidBodyComponent::PhysicsDimension::TwoD)
                    sourceBody=rigid->getBodyHandle();
                break;
            }
            _projectiles.insert_or_assign(command.emitter,
                Projectile{std::move(command),profile->second,origin,sourceBody,0});
        }
        for(auto it=_projectiles.begin();it!=_projectiles.end();) {
            if(!advanced.insert(it->first).second){++it;continue;}
            if(!_ability->emitterActive(it->first)){
                it=_projectiles.erase(it);continue;
            }
            auto& projectile=it->second;
            const auto kind=projectile.command.kind;
            bool finish=false;
            if(kind==DeliveryKind::Explosion || kind==DeliveryKind::Hazard) {
                std::vector<ayt::physics::BodyHandle> bodies;
                const auto status=physics->overlapSphereSync(projectile.position,
                    projectile.profile.radius,bodies,projectile.profile.layerMask);
                if(status!=ayt::physics::PhysResult::Ok)
                    _errors.push_back({Error::Invalid,"2D overlap query failed",{}});
                else for(auto body:bodies) {
                    if(projectile.profile.ignoreSource &&
                       body==projectile.sourceBody)continue;
                    auto candidate=target(body);
                    if(candidate.isValid() && ayt::combat::bindCombatant(*_world,candidate)) {
                        auto impact=_ability->reportDerivedHit(projectile.command.emitter,candidate,
                            0,projectile.age,{std::array<float,3>{projectile.position.x,
                                projectile.position.y,projectile.position.z}});
                        if(!impact && impact.error().code==Error::Stale)finish=true;
                        else if(!impact && impact.error().code!=Error::Duplicate)
                            _errors.push_back(impact.error());
                    }
                }
                finish=finish || kind==DeliveryKind::Explosion;
            } else {
                auto velocity=vector(projectile.profile.velocity);
                if(kind==DeliveryKind::Return && projectile.profile.reverseAfterTicks &&
                   projectile.age>=projectile.profile.reverseAfterTicks)
                    velocity=velocity*-1.0f;
                const float length=std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y)*fixedSeconds;
                if(length>0 && std::isfinite(length)) {
                    const ayt::math::FVector3 direction{
                        velocity.x/(length/fixedSeconds),
                        velocity.y/(length/fixedSeconds),0};
                    auto cursor=projectile.position;
                    float remaining=length;
                    for(std::uint32_t contacts=0;contacts<32 && remaining>0.001f;++contacts) {
                        ayt::math::Ray ray{};
                        ray.origin=cursor;ray.dir=direction;
                        ayt::physics::RaycastHit hit{};
                        const auto status=physics->raycastSync(ray,hit,projectile.profile.layerMask);
                        if(status!=ayt::physics::PhysResult::Ok) {
                            _errors.push_back({Error::Invalid,"2D raycast query failed",{}});
                            break;
                        }
                        if(!hit.hit || hit.distance>remaining+0.0001f)break;
                        const bool ownBody=projectile.profile.ignoreSource &&
                            hit.body==projectile.sourceBody;
                        auto candidate=target(hit.body);
                        const bool combatant=candidate.isValid() &&
                            static_cast<bool>(ayt::combat::bindCombatant(*_world,candidate));
                        if(!ownBody && combatant) {
                            auto impact=_ability->reportDerivedHit(projectile.command.emitter,candidate,
                                0,projectile.age,{std::array<float,3>{hit.point.x,
                                    hit.point.y,hit.point.z}});
                            if(!impact && impact.error().code==Error::Stale)finish=true;
                            else if(!impact && impact.error().code!=Error::Duplicate)
                                _errors.push_back(impact.error());
                            if(projectile.profile.target==CollisionResponse2D::Stop)
                                finish=true;
                        } else if(!ownBody &&
                            projectile.profile.obstacle==CollisionResponse2D::Stop)
                            finish=true;
                        if(finish)break;
                        const float step=std::max(hit.distance+0.001f,0.001f);
                        cursor=cursor+direction*step;
                        remaining-=step;
                        if(contacts==31)
                            _errors.push_back({Error::Limit,"2D contact traversal limit",{}});
                    }
                    if(!finish)projectile.position=projectile.position+velocity*fixedSeconds;
                }
            }
            ++projectile.age;
            if(finish || projectile.age>=projectile.command.lifetimeTicks) {
                (void)_ability->finishDerivedEmitter(projectile.command.emitter);
                it=_projectiles.erase(it);
            } else ++it;
        }
    }
}
Result<Ability2DDeliverySystem*> installAbility2D(ayt::entity::World& world,
    AbilitySystem& ability,ayt::physics::PhysicsManager& physics) {
    if(!physics.world2D())return invalid<Ability2DDeliverySystem*>("2D physics world unavailable");
    if(!world.findSystemByName("Ability2DDeliverySystem"))
        world.registerSystem<Ability2DDeliverySystem>(150,ayt::entity::SystemLane::Sim);
    auto* adapter=dynamic_cast<Ability2DDeliverySystem*>(
        world.findSystemByName("Ability2DDeliverySystem"));
    if(!adapter)return invalid<Ability2DDeliverySystem*>("2D delivery system name collision");
    adapter->bind(world,ability,physics);
    return adapter;
}
Result<Ability2DDeliverySystem*> installAbility2D(ayt::entity::World& world,
    AbilitySystem& ability,ayt::physics::PhysicsManager& physics,
    const AuthoringDocument& document) {
    Ability2DDeliverySystem checked;
    if(auto valid=checked.configure(document);!valid)
        return std::unexpected(valid.error());
    auto installed=installAbility2D(world,ability,physics);
    if(!installed)return installed;
    if(auto configured=(*installed)->configure(document);!configured)
        return std::unexpected(configured.error());
    return installed;
}
} // namespace ayt::ability
