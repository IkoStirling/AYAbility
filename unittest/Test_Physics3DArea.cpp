#include <AYTest.h>
#include <AYAbility/Physics3DIntegration.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYPhysics/PhysicsWorld3D.h>
#include <algorithm>

using namespace ayt::ability;
TEST_SUITE(AbilityPhysics3DAreaTests)
TEST_CASE(jolt_area_candidates_follow_source_and_combat_membership) {
    auto& world = ayt::entity::World::processWorld();
    ayt::entity::World::setActiveWorld(nullptr);
    world.initialize();
    auto& registry = ayt::entity::ComponentRegistry::instance();
    if (!registry.isSealed()) {
        (void)ayt::entity::registerEntityCoreComponents(registry);
        CHECK_TRUE(static_cast<bool>(ayt::entity::registerEntityPhysicsComponents(registry)));
        CHECK_TRUE(registerAbilityGameplayComponents(registry).has_value());
        registry.seal();
    }
    ayt::physics::PhysicsBackendDescriptor backend;
    backend.kind3D = ayt::physics::BackendKind::DefaultJolt;
    backend.kind2D = ayt::physics::BackendKind::Null;
    backend.maxBodies = 256;
    backend.maxBodyPairs = 1024;
    backend.maxContactConstraints = 1024;
    auto manager = ayt::physics::PhysicsManager::create(backend);
    CHECK_TRUE(manager && manager->world3D());
    auto* sourceEntity = world.createEntity();
    auto* targetEntity = world.createEntity();
    auto* emptyEntity = world.createEntity();
    auto source = world.getEntityHandle(sourceEntity->getId());
    auto target = world.getEntityHandle(targetEntity->getId());
    sourceEntity->addComponent<ayt::entity::Transform>()->setPosition(0, 0, 0);
    auto* sourceTransform = sourceEntity->getComponent<ayt::entity::Transform>();
    auto schema = ayt::stats::Schema::compile({
        {.name="health", .shape=ayt::stats::Shape::Pool, .current=100, .capacity=100}}).value();
    AuthoringDocument document;
    document.actions = {{.id=20, .behaviorId=2,
        .defaults={{.id=1, .unit=Unit::Ticks, .value=1}}}};
    document.skills = {{.id=303, .action=20, .kind=SkillKind::Active}};
    auto ability = installAbility(world, document).value();
    auto* combat = dynamic_cast<ayt::combat::CombatSystem*>(world.findSystemByName("CombatSystem"));
    CHECK_TRUE(combat != nullptr);
    ayt::combat::ActorProfile profile{schema, {ayt::stats::statId("health")}, 1};
    CHECK_TRUE(combat->configureActor(source, profile).has_value());
    profile.team = 2;
    CHECK_TRUE(combat->configureActor(target, profile).has_value());
    CHECK_TRUE(combat->setRelation(1, 2, ayt::combat::Relation::Friendly).has_value());
    CHECK_TRUE(ability->grant(source, 303, 1).has_value());
    auto addBody = [&](ayt::entity::Entity* entity, float x) {
        ayt::physics::RigidbodyDesc body{};
        body.type = ayt::physics::BodyType::Static;
        body.position = {x, 0, 0};
        ayt::physics::BodyHandle handle = 0;
        CHECK_TRUE(manager->world3D()->createRigidbody(body, handle) == ayt::physics::PhysResult::Ok);
        ayt::physics::ColliderDesc collider{};
        collider.body = handle;
        collider.shape = ayt::physics::ColliderShape::Sphere;
        collider.radius = 0.2f;
        ayt::physics::ColliderHandle shape = 0;
        CHECK_TRUE(manager->world3D()->createCollider(collider, shape) == ayt::physics::PhysResult::Ok);
        auto* rigid = entity->addComponent<ayt::entity::RigidBodyComponent>();
        rigid->setPhysicsDimension(ayt::entity::RigidBodyComponent::PhysicsDimension::ThreeD);
        rigid->setBodyHandle(handle);
        return handle;
    };
    addBody(sourceEntity, 0);
    const auto targetBody = addBody(targetEntity, 0.5f);
    addBody(emptyEntity, 0.6f);
    CHECK_TRUE(manager->stepAndWait(1.0f/60) == ayt::physics::PhysResult::Ok);
    ayt::combat::AreaDefinition area;
    area.effect.type = 901;
    area.effect.key = 901;
    area.effect.tags = {77};
    area.permission = {ayt::combat::InteractionIntent::Beneficial,
        ayt::combat::mask(ayt::combat::AllowedTarget::Friendly)};
    area.permission.requireSelected = true;
    ayt::combat::AreaQuery3D query;
    query.radius = 1;
    CHECK_FALSE(configureAreaSkill3D(*ability, world, *manager, 303, area, query).has_value());
    query.radius = -1;
    CHECK_FALSE(configureAreaSkill3D(*ability, world, *manager, 303, area, query,
        [](auto){ return true; }).has_value());
    query.radius = 1;
    CHECK_TRUE(configureAreaSkill3D(*ability, world, *manager, 303, area, query,
        [target](auto candidate){ return candidate == target; }).has_value());
    auto cast = ability->castArea(source, 303).value();
    world.fixedUpdate(1.0f/60);
    CHECK_TRUE(combat->hasTag(target, 77).value());
    CHECK_INT_EQ(combat->areaMemberCount(cast.area).value(), 1);
    CHECK_TRUE(manager->world3D()->setRigidbodyTransform(targetBody, {3,0,0}, {}) ==
        ayt::physics::PhysResult::Ok);
    CHECK_TRUE(manager->stepAndWait(1.0f/60) == ayt::physics::PhysResult::Ok);
    world.fixedUpdate(1.0f/60);
    CHECK_FALSE(combat->hasTag(target, 77).value());
    sourceTransform->setPosition(3, 0, 0);
    world.fixedUpdate(1.0f/60);
    CHECK_TRUE(combat->hasTag(target, 77).value());
    CHECK_TRUE(ability->stopArea(cast.area)->ended);
    CHECK_FALSE(combat->hasTag(target, 77).value());
    auto provider = ayt::combat::makeAreaCandidateProvider3D(world, *manager,
        source, query).value();
    auto* duplicate = world.createEntity();
    auto duplicateHandle = world.getEntityHandle(duplicate->getId());
    CHECK_TRUE(combat->configureActor(duplicateHandle, profile).has_value());
    auto* duplicateRigid = duplicate->addComponent<ayt::entity::RigidBodyComponent>();
    duplicateRigid->setPhysicsDimension(ayt::entity::RigidBodyComponent::PhysicsDimension::ThreeD);
    duplicateRigid->setBodyHandle(targetBody);
    CHECK_FALSE(provider().has_value());
    world.destroyEntity(duplicate);
    CHECK_TRUE(provider().has_value());
    world.destroyEntity(sourceEntity);
    CHECK_TRUE(provider()->empty());
    world.shutdown();
    manager->shutdown();
}
TEST_SUITE_END
