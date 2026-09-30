#include <AYAbility/Physics2DIntegration.h>
#include <AYPhysics/PhysicsWorld2D.h>
#include <AYEntity.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <cmath>
#include <set>

namespace ayt::ability {
namespace {
template<class T> Result<T> invalid(std::string message) {
    return std::unexpected(Diagnostic{Error::Invalid,std::move(message),{}});
}
bool finite(const ayt::math::FVector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}
Result<void> Ability2DDeliverySystem::configure(Id action,Id stage,Delivery2DProfile profile) {
    if(!action || !stage || !finite(profile.velocity) || !finite(profile.spawnOffset) ||
       !std::isfinite(profile.radius) || profile.radius<0 || !profile.layerMask)
        return invalid<void>("invalid 2D delivery profile");
    _profiles.insert_or_assign({action,stage},profile);
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
            ayt::math::FVector3 origin=profile->second.spawnOffset;
            if(command.origin)
                origin=origin+ayt::math::FVector3{(*command.origin)[0],
                    (*command.origin)[1],(*command.origin)[2]};
            else {
                for(auto* entity:_world->getAllEntities()) {
                    const auto handle=_world->getEntityHandle(entity->getId());
                    auto binding=ayt::combat::bindCombatant(*_world,handle);
                    if(!binding || binding->ref.domain!=command.source->actor.domain ||
                       binding->ref.object!=command.source->actor.object ||
                       binding->ref.generation!=command.source->actor.generation)continue;
                    if(auto* transform=entity->getComponent<ayt::entity::Transform>())
                        origin=origin+transform->position;
                    break;
                }
            }
            _projectiles.insert_or_assign(command.emitter,
                Projectile{std::move(command),profile->second,origin,0});
        }
        for(auto it=_projectiles.begin();it!=_projectiles.end();) {
            if(!advanced.insert(it->first).second){++it;continue;}
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
                    auto candidate=target(body);
                    if(candidate.isValid()) {
                        auto impact=_ability->reportDerivedHit(projectile.command.emitter,candidate,
                            0,projectile.age,{std::array<float,3>{projectile.position.x,
                                projectile.position.y,projectile.position.z}});
                        if(!impact && impact.error().code!=Error::Duplicate)
                            _errors.push_back(impact.error());
                    }
                }
                finish=kind==DeliveryKind::Explosion;
            } else {
                auto velocity=projectile.profile.velocity;
                if(kind==DeliveryKind::Return && projectile.profile.reverseAfterTicks &&
                   projectile.age>=projectile.profile.reverseAfterTicks)
                    velocity=velocity*-1.0f;
                const float length=std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y)*fixedSeconds;
                if(length>0 && std::isfinite(length)) {
                    ayt::math::Ray ray{};
                    ray.origin=projectile.position;
                    ray.dir={velocity.x/length*fixedSeconds,velocity.y/length*fixedSeconds,0};
                    ayt::physics::RaycastHit hit{};
                    const auto status=physics->raycastSync(ray,hit,projectile.profile.layerMask);
                    if(status!=ayt::physics::PhysResult::Ok)
                        _errors.push_back({Error::Invalid,"2D raycast query failed",{}});
                    else if(hit.hit && hit.distance<=length+0.0001f) {
                        auto candidate=target(hit.body);
                        if(candidate.isValid()) {
                            auto impact=_ability->reportDerivedHit(projectile.command.emitter,candidate,
                                0,projectile.age,{std::array<float,3>{hit.point.x,
                                    hit.point.y,hit.point.z}});
                            if(!impact && impact.error().code!=Error::Duplicate)
                                _errors.push_back(impact.error());
                        }
                        finish=true;
                    }
                    projectile.position=projectile.position+velocity*fixedSeconds;
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
} // namespace ayt::ability
