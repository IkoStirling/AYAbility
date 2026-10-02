#include <AYTest.h>
#include <AYAbility/Physics3DIntegration.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYPhysics/PhysicsWorld3D.h>
#include "Fixtures.h"

using namespace ayt::ability;
TEST_SUITE(AbilityPhysics3DTests)
TEST_CASE(jolt_contact_reports_to_ability_and_combat) {
    auto& world=ayt::entity::World::processWorld();
    ayt::entity::World::setActiveWorld(nullptr);
    world.initialize();
    auto& registry=ayt::entity::ComponentRegistry::instance();
    if(!registry.isSealed()) {
        (void)ayt::entity::registerEntityCoreComponents(registry);
        CHECK_TRUE(static_cast<bool>(ayt::entity::registerEntityPhysicsComponents(registry)));
        CHECK_TRUE(registerAbilityGameplayComponents(registry).has_value());
        registry.seal();
    }
    ayt::physics::PhysicsBackendDescriptor backend;
    backend.kind3D=ayt::physics::BackendKind::DefaultJolt;
    backend.kind2D=ayt::physics::BackendKind::Null;
    backend.maxBodies=256;
    backend.maxBodyPairs=1024;
    backend.maxContactConstraints=1024;
    auto manager=ayt::physics::PhysicsManager::create(backend);
    CHECK_TRUE(manager && manager->world3D());
    AuthoringDocument document;
    document.actions={{.id=30,.behaviorId=derived::Behavior,
        .defaults={{derived::CooldownTicks,Unit::Ticks,1}}}};
    document.skills={{.id=404,.action=30,.kind=SkillKind::Active}};
    document.derivedSkills={{404,{.stages={{.id=1,.kind=DeliveryKind::Contact,
        .amount=15,.lifetimeTicks=4}}}}};
    document.delivery3D={{30,1,{.velocity={120,0,0}}}};
    auto encoded=saveAuthoring(document).value();
    document=loadAuthoring(encoded).value();
    auto incomplete=document;incomplete.delivery3D.clear();
    CHECK_FALSE(installAbility3D(world,*manager,incomplete).has_value());
    CHECK_TRUE(world.findSystemByName("AbilitySystem")==nullptr);
    auto* delivery=installAbility3D(world,*manager,document).value();
    auto* ability=dynamic_cast<AbilitySystem*>(world.findSystemByName("AbilitySystem"));
    auto* combat=dynamic_cast<ayt::combat::CombatSystem*>(world.findSystemByName("CombatSystem"));
    auto* sourceEntity=world.createEntity();
    auto* targetEntity=world.createEntity();
    sourceEntity->addComponent<ayt::entity::Transform>()->setPosition(0,0,0);
    const auto source=world.getEntityHandle(sourceEntity->getId());
    const auto target=world.getEntityHandle(targetEntity->getId());
    auto schema=ayt::stats::Schema::compile({
        {.name="health",.shape=ayt::stats::Shape::Pool,.current=100,.capacity=100}}).value();
    ayt::combat::ActorProfile profile{schema,{ayt::stats::statId("health")},1};
    CHECK_TRUE(combat->configureActor(source,profile).has_value());
    profile.team=2;
    CHECK_TRUE(combat->configureActor(target,profile).has_value());
    CHECK_TRUE(combat->setRelation(1,2,ayt::combat::Relation::Hostile).has_value());
    CHECK_TRUE(ability->grant(source,404,1).has_value());
    Delivery3DProfile deliveryProfile;
    deliveryProfile.velocity={120,0,0};
    CHECK_TRUE(delivery->configure(30,1,deliveryProfile).has_value());
    deliveryProfile.radius=-1;
    CHECK_FALSE(delivery->configure(30,1,deliveryProfile).has_value());
    ayt::physics::RigidbodyDesc body{};
    body.type=ayt::physics::BodyType::Dynamic;
    body.position={1,0,0};
    body.gravityScale=0;
    ayt::physics::BodyHandle handle=0;
    CHECK_TRUE(manager->world3D()->createRigidbody(body,handle)==ayt::physics::PhysResult::Ok);
    ayt::physics::ColliderDesc collider{};
    collider.body=handle;
    collider.shape=ayt::physics::ColliderShape::Sphere;
    collider.radius=0.2f;
    ayt::physics::ColliderHandle shape=0;
    CHECK_TRUE(manager->world3D()->createCollider(collider,shape)==ayt::physics::PhysResult::Ok);
    for(int i=0;i<5;++i)
        CHECK_TRUE(manager->stepAndWait(1.0f/60)==ayt::physics::PhysResult::Ok);
    ayt::math::Ray probe{};probe.origin={0,0,0};probe.dir={1,0,0};
    ayt::physics::RaycastHit probeHit{};
    CHECK_TRUE(manager->world3D()->raycastSync(probe,probeHit)==ayt::physics::PhysResult::Ok);
    CHECK_TRUE(probeHit.hit);
    CHECK_TRUE(probeHit.body==handle);
    auto* rigid=targetEntity->addComponent<ayt::entity::RigidBodyComponent>();
    rigid->setPhysicsDimension(ayt::entity::RigidBodyComponent::PhysicsDimension::ThreeD);
    rigid->setBodyHandle(handle);
    CHECK_TRUE(ability->castDerived(source,404).has_value());
    world.fixedUpdate(1.0f/60);
    auto* stats=targetEntity->getComponent<ayt::stats::StatsComponent>();
    CHECK_INT_EQ(stats->read(ayt::stats::statId("health"))->current,85);
    CHECK_TRUE(delivery->errors().empty());
    world.shutdown();
    manager->shutdown();
}
TEST_SUITE_END
