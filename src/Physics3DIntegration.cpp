#include <AYAbility/Physics3DIntegration.h>
#include <AYPhysics/PhysicsWorld3D.h>
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
Result<void> Ability3DDeliverySystem::configure(Id action,Id stage,Delivery3DProfile profile) {
    if(!action || !stage)return invalid<void>("3D stage identity missing");
    if(auto valid=validateDelivery3DProfile(profile);!valid)return valid;
    _profiles.insert_or_assign({action,stage},profile);
    return {};
}
Result<void> Ability3DDeliverySystem::configure(const AuthoringDocument& document) {
    if(auto valid=compileAuthoring(document);!valid)return std::unexpected(valid.error());
    std::map<std::pair<Id,Id>,Delivery3DProfile> profiles;
    for(const auto& binding:document.delivery3D)
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
                    return invalid<void>("3D delivery profile missing for authored stage");
        }
    _profiles=std::move(profiles);
    return {};
}
void Ability3DDeliverySystem::bind(ayt::entity::World& world,AbilitySystem& ability,
        ayt::physics::PhysicsManager& physics) {
    _world=&world;_ability=&ability;_physics=&physics;
    _projectiles.clear();_hitParts.clear();_surfaceResistance.clear();
    _surfaceMedia.clear();_errors.clear();
}
void Ability3DDeliverySystem::setHitPart(ayt::physics::ColliderHandle collider,std::uint32_t part) {
    if(!collider)return;
    if(part)_hitParts.insert_or_assign(collider,part);
    else _hitParts.erase(collider);
}
Result<void> Ability3DDeliverySystem::setSurfaceResistance(
        ayt::physics::ColliderHandle collider,float cost) {
    if(!collider || !std::isfinite(cost) || cost<0)
        return invalid<void>("invalid 3D surface resistance");
    if(cost==0)_surfaceResistance.erase(collider);
    else _surfaceResistance.insert_or_assign(collider,cost);
    _surfaceMedia.erase(collider);
    return {};
}
Result<void> Ability3DDeliverySystem::setSurfaceMedium(
        ayt::physics::ColliderHandle collider,const SurfaceMedium3D& medium) {
    if(!collider)return invalid<void>("invalid 3D surface collider");
    if(auto checked=planSurfacePenetration3D({1,0,0},{-1,0,0},medium,0);!checked)
        return std::unexpected(checked.error());
    _surfaceMedia.insert_or_assign(collider,medium);
    _surfaceResistance.erase(collider);
    return {};
}
void Ability3DDeliverySystem::clearSurfaceMedium(
        ayt::physics::ColliderHandle collider) {
    _surfaceMedia.erase(collider);
}
ayt::entity::EntityHandle Ability3DDeliverySystem::target(ayt::physics::BodyHandle body) const {
    for(auto* entity:_world->getAllEntities()) {
        auto* rigid=entity->getComponent<ayt::entity::RigidBodyComponent>();
        if(rigid && rigid->getPhysicsDimension()==
           ayt::entity::RigidBodyComponent::PhysicsDimension::ThreeD &&
           rigid->getBodyHandle()==body)
            return _world->getEntityHandle(entity->getId());
    }
    return {};
}
void Ability3DDeliverySystem::onUpdate(float fixedSeconds) {
    if(!_world || !_ability || !_physics)return;
    auto* physics=_physics->world3D();
    if(!physics){_errors.push_back({Error::Stale,"3D physics world unavailable",{}});return;}
    auto debugRegion=[&](ayt::math::FVector3 origin,ayt::math::FVector3 end,float radius,bool hit,ayt::physics::PhysResult status) {
        if(!_physics->debugDraw().options().enabled) return;
        ayt::physics::PhysicsDebugRecord record;record.category=ayt::physics::PhysicsDebugCategory::Ability;
        record.dimension=ayt::physics::PhysicsDimension::ThreeD;record.anchor=origin;record.hit=hit;record.status=status;
        record.label=radius>0?"Ability area test":"Ability swept segment";
        const uint32_t color=status!=ayt::physics::PhysResult::Ok?0xff00ffffu:hit?0xff0000ffu:0xff00ff00u;
        if(radius>0) {ayt::physics::ColliderDesc shape;shape.shape=ayt::physics::ColliderShape::Sphere;shape.radius=radius;
            record.lines=ayt::physics::PhysicsDebugDraw::colliderLines(shape,origin,ayt::math::FQuaternion::identity(),record.dimension,color);
        } else record.lines.push_back({origin,end,color});
        (void)_physics->debugDraw().recordGeometry(std::move(record));
    };
    auto debugImpact=[&](const auto& impact,ayt::entity::EntityHandle target,ayt::math::FVector3 point) {
        if(!_physics->debugDraw().options().enabled) return;
        const bool affected=impact && (impact->interaction?impact->interaction->decision.allowed:impact->settlement.state==ayt::combat::OutcomeState::Applied);
        ayt::physics::PhysicsDebugRecord record;record.category=ayt::physics::PhysicsDebugCategory::Ability;record.dimension=ayt::physics::PhysicsDimension::ThreeD;
        record.entityId=target.id;record.anchor=point;record.hit=affected;record.label=affected?"Ability affected target":"Ability target denied/duplicate/failed";
        const uint32_t color=affected?0xff0000ffu:0xff00ff00u;
        record.lines={{point-ayt::math::FVector3{.15f,0,0},point+ayt::math::FVector3{.15f,0,0},color},{point-ayt::math::FVector3{0,.15f,0},point+ayt::math::FVector3{0,.15f,0},color}};
        (void)_physics->debugDraw().recordGeometry(std::move(record));
    };
    std::set<Identity> advanced;
    for(std::size_t round=0;round<16;++round) {
        auto commands=_ability->takeEmissions();
        if(round && commands.empty())break;
        for(auto& command:commands) {
            auto profile=_profiles.find({command.source->action,command.stage});
            if(profile==_profiles.end()) {
                _errors.push_back({Error::Missing,"3D delivery profile missing",{}});
                (void)_ability->finishDerivedEmitter(command.emitter);
                continue;
            }
            ayt::math::FVector3 origin=vector(profile->second.spawnOffset);
            ayt::physics::BodyHandle sourceBody=ayt::physics::InvalidBodyHandle;
            if(command.origin)origin=origin+vector(*command.origin);
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
                       ayt::entity::RigidBodyComponent::PhysicsDimension::ThreeD)
                    sourceBody=rigid->getBodyHandle();
                break;
            }
            _projectiles.insert_or_assign(command.emitter,
                Projectile{std::move(command),profile->second,origin,sourceBody,0,
                    profile->second.penetrationBudget});
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
                debugRegion(projectile.position,projectile.position,projectile.profile.radius,!bodies.empty(),status);
                if(status!=ayt::physics::PhysResult::Ok)
                    _errors.push_back({Error::Invalid,"3D overlap query failed",{}});
                else for(auto body:bodies) {
                    if(projectile.profile.ignoreSource && body==projectile.sourceBody)continue;
                    auto candidate=target(body);
                    if(candidate.isValid() && ayt::combat::bindCombatant(*_world,candidate)) {
                        auto impact=_ability->reportDerivedHit(projectile.command.emitter,candidate,
                            0,projectile.age,{std::array<float,3>{projectile.position.x,
                                projectile.position.y,projectile.position.z}});
                        debugImpact(impact,candidate,projectile.position);
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
                const float speed=std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y+
                    velocity.z*velocity.z);
                const float length=speed*fixedSeconds;
                if(length>10000.0f || !std::isfinite(length))
                    _errors.push_back({Error::Limit,"3D swept segment exceeds query limit",{}});
                else if(length>0) {
                    ayt::physics::RaycastAll3D query;
                    query.origin=projectile.position;
                    query.displacement=velocity*fixedSeconds;
                    query.layerMask=projectile.profile.layerMask;
                    query.includeSensors=true; // retain legacy contact semantics
                    query.maxHits=32;
                    if(projectile.profile.ignoreSource && projectile.sourceBody)
                        query.ignoredBodies.push_back(projectile.sourceBody);
                    std::vector<ayt::physics::RaycastHit> hits;
                    const auto status=physics->raycastAll3DSync(query,hits);
                    debugRegion(query.origin,query.origin+query.displacement,0,
                        !hits.empty(),status);
                    if(status!=ayt::physics::PhysResult::Ok) {
                        _errors.push_back({status==ayt::physics::PhysResult::OutOfRange
                            ? Error::Limit : Error::Invalid,
                            "3D multi-hit raycast query failed",{}});
                    } else {
                        for(const auto& hit:hits) {
                            float resistance=_surfaceResistance.contains(hit.collider)
                                ? _surfaceResistance.at(hit.collider):0.0f;
                            if(projectile.profile.usePenetrationBudget) {
                                if(const auto medium=_surfaceMedia.find(hit.collider);
                                    medium!=_surfaceMedia.end()) {
                                    const auto planned=planSurfacePenetration3D(
                                        {query.displacement.x,query.displacement.y,query.displacement.z},
                                        {hit.normal.x,hit.normal.y,hit.normal.z},
                                        medium->second,projectile.remainingPenetration);
                                    if(!planned) {
                                        _errors.push_back(planned.error());
                                        finish=true;
                                        break;
                                    }
                                    resistance=planned->cost;
                                }
                            }
                            const bool resisted=projectile.profile.usePenetrationBudget
                                && resistance>projectile.remainingPenetration;
                            auto candidate=target(hit.body);
                            const bool combatant=candidate.isValid() &&
                                static_cast<bool>(ayt::combat::bindCombatant(*_world,candidate));
                            if(combatant) {
                                const auto part=_hitParts.contains(hit.collider)?_hitParts.at(hit.collider):0;
                                auto impact=_ability->reportDerivedHit(projectile.command.emitter,candidate,
                                    part,projectile.age,{std::array<float,3>{hit.point.x,
                                        hit.point.y,hit.point.z}});
                                debugImpact(impact,candidate,hit.point);
                                if(!impact && impact.error().code==Error::Stale)finish=true;
                                else if(!impact && impact.error().code!=Error::Duplicate)
                                    _errors.push_back(impact.error());
                                if(projectile.profile.target==CollisionResponse3D::Stop)finish=true;
                            } else if(projectile.profile.obstacle==CollisionResponse3D::Stop)
                                finish=true;
                            if(resisted)finish=true;
                            if(finish)break;
                            if(projectile.profile.usePenetrationBudget)
                                projectile.remainingPenetration-=resistance;
                        }
                        if(!finish)projectile.position=projectile.position+query.displacement;
                    }
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
Result<Ability3DDeliverySystem*> installAbility3D(ayt::entity::World& world,
        AbilitySystem& ability,ayt::physics::PhysicsManager& physics) {
    if(!physics.world3D())return invalid<Ability3DDeliverySystem*>("3D physics world unavailable");
    if(world.findSystemByName("Ability2DDeliverySystem"))
        return invalid<Ability3DDeliverySystem*>("2D delivery already owns emissions");
    if(!world.findSystemByName("Ability3DDeliverySystem"))
        world.registerSystem<Ability3DDeliverySystem>(150,ayt::entity::SystemLane::Sim);
    auto* adapter=dynamic_cast<Ability3DDeliverySystem*>(
        world.findSystemByName("Ability3DDeliverySystem"));
    if(!adapter)return invalid<Ability3DDeliverySystem*>("3D delivery system name collision");
    adapter->bind(world,ability,physics);
    return adapter;
}
Result<Ability3DDeliverySystem*> installAbility3D(ayt::entity::World& world,
        ayt::physics::PhysicsManager& physics,const AuthoringDocument& document,
        const OwnedEffectBindings& effects) {
    Ability3DDeliverySystem checked;
    if(auto valid=checked.configure(document);!valid)
        return std::unexpected(valid.error());
    if(auto valid=validateOwnedEffectBindings(document,effects);!valid)
        return std::unexpected(valid.error());
    if(!physics.world3D())return invalid<Ability3DDeliverySystem*>("3D physics world unavailable");
    if(world.findSystemByName("Ability2DDeliverySystem"))
        return invalid<Ability3DDeliverySystem*>("2D delivery already owns emissions");
    auto ability=installAbility(world,document,effects);
    if(!ability)return std::unexpected(ability.error());
    auto installed=installAbility3D(world,**ability,physics);
    if(!installed)return installed;
    if(auto configured=(*installed)->configure(document);!configured)
        return std::unexpected(configured.error());
    return installed;
}
Result<void> configureAreaSkill3D(AbilitySystem& ability, ayt::entity::World& world,
    ayt::physics::PhysicsManager& physics, Id skill,
    ayt::combat::AreaDefinition definition, ayt::combat::AreaQuery3D query,
    ayt::combat::AreaSelection3D selection) {
    if (auto valid = ayt::combat::validateAreaQuery3D(query); !valid)
        return invalid<void>(valid.error().message);
    if (!physics.world3D()) return invalid<void>("3D physics world unavailable");
    if (definition.permission.requireSelected && !selection)
        return invalid<void>("selected 3D area requires a selection predicate");
    return ability.configureAreaSkill(skill, std::move(definition),
        [&world, &physics, query, selection = std::move(selection)](ayt::entity::EntityHandle source)
            -> ayt::combat::Result<ayt::combat::AreaCandidateProvider> {
            return ayt::combat::makeAreaCandidateProvider3D(world, physics, source, query, selection);
        });
}
} // namespace ayt::ability
