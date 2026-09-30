#include <AYTest.h>
#include <AYAbility/Physics2DIntegration.h>
#include <AYAbility/Authoring.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYPhysics/PhysicsWorld2D.h>
#include <fstream>
#include <iterator>

using namespace ayt::ability;
TEST_SUITE(AbilityPhysics2DTests)
TEST_CASE(authored_contact_rules_cancel_and_source_death) {
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
    backend.kind3D=ayt::physics::BackendKind::Null;
    backend.kind2D=ayt::physics::BackendKind::DefaultBox2D;
    auto manager=ayt::physics::PhysicsManager::create(backend);
    CHECK_TRUE(manager && manager->world2D());
    std::ifstream input(std::string(AY_ABILITY_EXAMPLE_DIR)+"/contact2d.ability.json");
    CHECK_TRUE(input.good());
    const std::string authored(std::istreambuf_iterator<char>{input},{});
    auto document=loadAuthoring(authored).value();
    auto* ability=installAbility(world,document).value();
    auto unsupported=document;unsupported.version=2;
    CHECK_FALSE(installAbility(world,unsupported).has_value());
    auto* combat=dynamic_cast<ayt::combat::CombatSystem*>(world.findSystemByName("CombatSystem"));
    auto* sourceEntity=world.createEntity();
    auto* targetEntity=world.createEntity();
    auto* sourceTransform=sourceEntity->addComponent<ayt::entity::Transform>();
    sourceTransform->setPosition(0.0f,0.0f,0.0f);
    auto source=world.getEntityHandle(sourceEntity->getId());
    auto target=world.getEntityHandle(targetEntity->getId());
    auto schema=ayt::stats::Schema::compile({
        {.name="health",.shape=ayt::stats::Shape::Pool,.current=100,.capacity=100},
        {.name="mana",.shape=ayt::stats::Shape::Pool,.current=80,.capacity=80}}).value();
    ayt::combat::ActorProfile profile{schema,{ayt::stats::statId("health")},1};
    CHECK_TRUE(combat->configureActor(source,profile).has_value());
    profile.team=2;
    CHECK_TRUE(combat->configureActor(target,profile).has_value());
    CHECK_TRUE(combat->setRelation(1,2,ayt::combat::Relation::Hostile).has_value());
    CHECK_TRUE(ability->grant(source,404,1).has_value());
    auto incomplete=document;incomplete.delivery2D.pop_back();
    CHECK_FALSE(installAbility2D(world,*ability,*manager,incomplete).has_value());
    CHECK_TRUE(world.findSystemByName("Ability2DDeliverySystem")==nullptr);
    auto* physics=installAbility2D(world,*ability,*manager,document).value();
    CHECK_FALSE(physics->configure(incomplete).has_value());
    ayt::physics::RigidbodyDesc sourceBody{};
    sourceBody.type=ayt::physics::BodyType::Static;
    ayt::physics::BodyHandle sourceHandle=0;
    CHECK_TRUE(manager->world2D()->createRigidbody(sourceBody,sourceHandle)==ayt::physics::PhysResult::Ok);
    ayt::physics::ColliderDesc sourceCollider{};
    sourceCollider.body=sourceHandle;
    sourceCollider.shape=ayt::physics::ColliderShape::Sphere;
    sourceCollider.radius=0.2f;
    ayt::physics::ColliderHandle sourceShape=0;
    CHECK_TRUE(manager->world2D()->createCollider(sourceCollider,sourceShape)==ayt::physics::PhysResult::Ok);
    auto* sourceRigid=sourceEntity->addComponent<ayt::entity::RigidBodyComponent>();
    sourceRigid->setPhysicsDimension(ayt::entity::RigidBodyComponent::PhysicsDimension::TwoD);
    sourceRigid->setBodyHandle(sourceHandle);
    ayt::physics::RigidbodyDesc obstacle{};
    obstacle.type=ayt::physics::BodyType::Static;
    obstacle.position={0.5f,0.0f,0.0f};
    ayt::physics::BodyHandle obstacleHandle=0;
    CHECK_TRUE(manager->world2D()->createRigidbody(obstacle,obstacleHandle)==ayt::physics::PhysResult::Ok);
    ayt::physics::ColliderDesc obstacleCollider{};
    obstacleCollider.body=obstacleHandle;
    obstacleCollider.shape=ayt::physics::ColliderShape::Sphere;
    obstacleCollider.radius=0.1f;
    ayt::physics::ColliderHandle obstacleShape=0;
    CHECK_TRUE(manager->world2D()->createCollider(obstacleCollider,obstacleShape)==ayt::physics::PhysResult::Ok);
    ayt::physics::RigidbodyDesc body{};
    body.type=ayt::physics::BodyType::Static;
    body.position={1.0f,0.0f,0.0f};
    ayt::physics::BodyHandle handle=0;
    CHECK_TRUE(manager->world2D()->createRigidbody(body,handle)==ayt::physics::PhysResult::Ok);
    ayt::physics::ColliderDesc collider{};
    collider.body=handle;
    collider.shape=ayt::physics::ColliderShape::Sphere;
    collider.radius=0.2f;
    ayt::physics::ColliderHandle shape=0;
    CHECK_TRUE(manager->world2D()->createCollider(collider,shape)==ayt::physics::PhysResult::Ok);
    CHECK_TRUE(manager->stepAndWait(1.0f/60)==ayt::physics::PhysResult::Ok);
    auto* rigid=targetEntity->addComponent<ayt::entity::RigidBodyComponent>();
    rigid->setPhysicsDimension(ayt::entity::RigidBodyComponent::PhysicsDimension::TwoD);
    rigid->setBodyHandle(handle);
    CHECK_TRUE(ability->castDerived(source,404).has_value());
    sourceTransform->setPosition(100.0f,0.0f,0.0f);
    world.fixedUpdate(1.0f/60);
    auto* stats=targetEntity->getComponent<ayt::stats::StatsComponent>();
    CHECK_TRUE(stats->read(ayt::stats::statId("health"))->current==85);
    CHECK_TRUE(physics->errors().empty());
    for(int i=0;i<6;++i)world.fixedUpdate(1.0f/60);
    sourceTransform->setPosition(0.0f,0.0f,0.0f);
    auto* secondEntity=world.createEntity();
    const auto second=world.getEntityHandle(secondEntity->getId());
    CHECK_TRUE(combat->configureActor(second,profile).has_value());
    ayt::physics::RigidbodyDesc secondBody{};
    secondBody.type=ayt::physics::BodyType::Static;
    secondBody.position={1.5f,0.0f,0.0f};
    ayt::physics::BodyHandle secondHandle=0;
    CHECK_TRUE(manager->world2D()->createRigidbody(secondBody,secondHandle)==ayt::physics::PhysResult::Ok);
    ayt::physics::ColliderDesc secondCollider{};
    secondCollider.body=secondHandle;
    secondCollider.shape=ayt::physics::ColliderShape::Sphere;
    secondCollider.radius=0.2f;
    ayt::physics::ColliderHandle secondShape=0;
    CHECK_TRUE(manager->world2D()->createCollider(secondCollider,secondShape)==ayt::physics::PhysResult::Ok);
    CHECK_TRUE(manager->stepAndWait(1.0f/60)==ayt::physics::PhysResult::Ok);
    auto* secondRigid=secondEntity->addComponent<ayt::entity::RigidBodyComponent>();
    secondRigid->setPhysicsDimension(ayt::entity::RigidBodyComponent::PhysicsDimension::TwoD);
    secondRigid->setBodyHandle(secondHandle);
    document.delivery2D[0].profile.target=CollisionResponse2D::PassThrough;
    CHECK_TRUE(physics->configure(document).has_value());
    CHECK_TRUE(ability->castDerived(source,404).has_value());
    world.fixedUpdate(1.0f/60);
    auto* secondStats=secondEntity->getComponent<ayt::stats::StatsComponent>();
    CHECK_INT_EQ(stats->read(ayt::stats::statId("health"))->current,70);
    CHECK_INT_EQ(secondStats->read(ayt::stats::statId("health"))->current,85);
    for(int i=0;i<6;++i)world.fixedUpdate(1.0f/60);
    document.delivery2D[0].profile.obstacle=CollisionResponse2D::Stop;
    CHECK_TRUE(physics->configure(document).has_value());
    CHECK_TRUE(ability->castDerived(source,404).has_value());
    world.fixedUpdate(1.0f/60);
    CHECK_TRUE(stats->read(ayt::stats::statId("health"))->current==70);
    world.fixedUpdate(1.0f/60);
    CHECK_TRUE(combat->hits().activeAttacks()==0);
    for(int i=0;i<6;++i)world.fixedUpdate(1.0f/60);
    document.delivery2D[0].profile.obstacle=CollisionResponse2D::PassThrough;
    document.delivery2D[0].profile.target=CollisionResponse2D::Stop;
    document.delivery2D[0].profile.ignoreSource=false;
    CHECK_TRUE(physics->configure(document).has_value());
    CHECK_TRUE(ability->castDerived(source,404).has_value());
    world.fixedUpdate(1.0f/60);
    CHECK_TRUE(stats->read(ayt::stats::statId("health"))->current==70);
    for(int i=0;i<6;++i)world.fixedUpdate(1.0f/60);
    document.delivery2D[0].profile.ignoreSource=true;
    CHECK_TRUE(physics->configure(document).has_value());
    auto cancelled=ability->castDerived(source,404).value();
    CHECK_TRUE(ability->cancelDerivedCast(cancelled.cast.execution).has_value());
    world.fixedUpdate(1.0f/60);
    CHECK_TRUE(stats->read(ayt::stats::statId("health"))->current==70);
    CHECK_TRUE(combat->hits().activeAttacks()==0);
    for(int i=0;i<6;++i)world.fixedUpdate(1.0f/60);
    CHECK_TRUE(ability->castDerived(source,404).has_value());
    const std::array death{ayt::stats::Mutation{ayt::stats::statId("health"),
        ayt::stats::MutationOp::Decrease,100}};
    auto* sourceStats=sourceEntity->getComponent<ayt::stats::StatsComponent>();
    CHECK_TRUE(sourceStats->commit(death,sourceStats->version()).has_value());
    world.fixedUpdate(1.0f/60);
    CHECK_TRUE(stats->read(ayt::stats::statId("health"))->current==70);
    CHECK_TRUE(combat->hits().activeAttacks()==0);
    CHECK_TRUE(physics->errors().empty());
    world.shutdown();
    manager->shutdown();
}
TEST_CASE(periodic_hazard_expires_and_ignores_destroyed_target) {
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
    backend.kind3D=ayt::physics::BackendKind::Null;
    backend.kind2D=ayt::physics::BackendKind::DefaultBox2D;
    auto manager=ayt::physics::PhysicsManager::create(backend);
    AuthoringDocument document;
    document.actions={{.id=30,.behaviorId=derived::Behavior,
        .defaults={{derived::CooldownTicks,Unit::Ticks,1}}}};
    document.skills={{.id=404,.action=30,.kind=SkillKind::Active}};
    document.derivedSkills={{404,{.stages={{.id=1,.kind=DeliveryKind::Hazard,
        .amount=5,.hitMode=DerivedHitMode::Periodic,.intervalTicks=1,.lifetimeTicks=4}}}}};
    document.delivery2D={{30,1,{.radius=1.0f}}};
    auto* ability=installAbility(world,document).value();
    auto* combat=dynamic_cast<ayt::combat::CombatSystem*>(world.findSystemByName("CombatSystem"));
    auto* sourceEntity=world.createEntity();
    auto* targetEntity=world.createEntity();
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
    auto* delivery=installAbility2D(world,*ability,*manager,document).value();
    ayt::physics::RigidbodyDesc body{};
    body.type=ayt::physics::BodyType::Static;
    body.position={0.5f,0.0f,0.0f};
    ayt::physics::BodyHandle handle=0;
    CHECK_TRUE(manager->world2D()->createRigidbody(body,handle)==ayt::physics::PhysResult::Ok);
    ayt::physics::ColliderDesc collider{};
    collider.body=handle;
    collider.shape=ayt::physics::ColliderShape::Sphere;
    collider.radius=0.2f;
    ayt::physics::ColliderHandle shape=0;
    CHECK_TRUE(manager->world2D()->createCollider(collider,shape)==ayt::physics::PhysResult::Ok);
    CHECK_TRUE(manager->stepAndWait(1.0f/60)==ayt::physics::PhysResult::Ok);
    auto* rigid=targetEntity->addComponent<ayt::entity::RigidBodyComponent>();
    rigid->setPhysicsDimension(ayt::entity::RigidBodyComponent::PhysicsDimension::TwoD);
    rigid->setBodyHandle(handle);
    CHECK_TRUE(ability->castDerived(source,404).has_value());
    for(int i=0;i<3;++i)world.fixedUpdate(1.0f/60);
    auto* targetStats=targetEntity->getComponent<ayt::stats::StatsComponent>();
    CHECK_INT_EQ(targetStats->read(ayt::stats::statId("health"))->current,85);
    world.fixedUpdate(1.0f/60);
    CHECK_TRUE(combat->hits().activeAttacks()==0);
    CHECK_TRUE(ability->castDerived(source,404).has_value());
    world.destroyEntity(targetEntity);
    for(int i=0;i<4;++i)world.fixedUpdate(1.0f/60);
    CHECK_TRUE(combat->hits().activeAttacks()==0);
    CHECK_TRUE(delivery->errors().empty());
    world.shutdown();
    manager->shutdown();
}
TEST_SUITE_END
