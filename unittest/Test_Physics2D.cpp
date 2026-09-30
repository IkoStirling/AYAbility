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
TEST_CASE(contact_stage_raycast_hits_real_box2d_body) {
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
    auto* physics=installAbility2D(world,*ability,*manager).value();
    CHECK_TRUE(physics->configure(30,1,Delivery2DProfile{
        .velocity={120.0f,0.0f,0.0f}}).has_value());
    CHECK_TRUE(physics->configure(30,2,Delivery2DProfile{
        .radius=1.0f}).has_value());
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
    world.shutdown();
    manager->shutdown();
}
TEST_SUITE_END
