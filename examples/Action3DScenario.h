#pragma once

// Headless fixed-Sim host example. The engine owns each subsystem's authority;
// this small host chooses phase durations and connects their value APIs.
#include <AYAbility/Derived.h>
#include <AYAbility/FixedActionTimeline.h>
#include <AYAnimation/AnimationPlayer.h>
#include <AYCombat/StatsIntegration.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYPhysics/PhysicsWorld3D.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYResource/assetsImpl/Skeleton.h>
#include <AYStats/Stats.h>
#include <AYStats/Calculation.h>
#include <array>
#include <cmath>
#include <expected>
#include <memory>
#include <stdexcept>
#include <string>

namespace ayt::ability::action3d_example {

struct Result {
    std::uint64_t attackExecution = 0;
    std::uint64_t attackHitTick = 0;
    int manaAfterAttack = 0;
    int manaAfterDodge = 0;
    int healthAfterHit = 0;
    int secondHealthAfterHit = 0;
    float positionAfterAttack = 0;
    float positionAfterInterrupt = 0;
    bool swingNotify = false;
    bool visualEffectCue = false;
    bool exactCollider = false;
    bool boundaryRejected = false;
    bool cancelledDodgeStopped = false;
    bool deniedBufferedActionStayedIdle = false;
};

template<class T> T take(std::expected<T, ayt::ability::Diagnostic> value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}
inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
inline void accepted(ayt::physics::PhysResult result) {
    if (result != ayt::physics::PhysResult::Ok)
        throw std::runtime_error(ayt::physics::toString(result));
}

inline std::shared_ptr<const ayt::resource::ISkeleton> skeleton() {
    auto skel = std::make_shared<ayt::resource::Skeleton>();
    skel->setBoneCount(2);
    ayt::resource::Bone root;
    root.name = "Root";
    root.parentIndex = -1;
    root.localScale = {1,1,1};
    root.localRotation = ayt::math::FQuaternion::identity();
    root.inverseBindMatrix = ayt::math::Float4x4::identity();
    skel->setBone(0, root);
    auto hand = root;
    hand.name = "Hand";
    hand.parentIndex = 0;
    hand.localPosition = {0.5f, 0.9f, 0};
    skel->setBone(1, hand);
    return skel;
}

inline ayt::resource::Animation clip(float distance, const char* notify) {
    ayt::resource::Animation result;
    result.setTicksPerSecond(60);
    result.setDuration(4.0f/60.0f);
    ayt::resource::AnimTrack track;
    track.nodeName = "Root";
    track.property = "position";
    track.valueType = ayt::resource::AnimTrackType::Vector3;
    track.times = {0,4};
    track.values = {0,0,0,distance,0,0};
    result.addTrack(track);
    if (notify != nullptr) result.addNotify({notify,2.0f/60.0f,1});
    return result;
}

inline Result run(bool targetDisappearsBeforeHit = false,
                  bool denyBufferedDodge = false) {
    using namespace ayt::physics;
    using namespace ayt::combat;
    using namespace ayt::stats;
    constexpr float dt = 1.0f/60.0f;
    const Identity actor{1,10,1};
    const CombatantRef attacker{1,10,1}, target{1,20,1}, secondTarget{1,21,1};

    ActionDefinition slash{.id=30,.behaviorId=derived::Behavior,
        .defaults={{derived::CooldownTicks,Unit::Ticks,1},{2,Unit::Amount,30}}};
    ActionDefinition dodge{.id=20,.behaviorId=area::Behavior,
        .defaults={{area::CooldownTicks,Unit::Ticks,1}}};
    NumericTrack cost{.mode=TrackMode::Constant,.constant=10};
    SkillDefinition slashSkill{.id=404,.action=30,.kind=SkillKind::Active,
        .cost=SkillCost{"mana",cost}};
    SkillDefinition dodgeSkill{.id=303,.action=20,.kind=SkillKind::Active,
        .cost=SkillCost{"mana",cost}};
    auto catalog = take(Catalog::compile({slash,dodge},{},{slashSkill,dodgeSkill}));
    Runtime abilities(catalog);
    require(abilities.grant(actor,404,1).has_value(), "slash grant");
    require(abilities.grant(actor,303,1).has_value(), "dodge grant");
    int mana = 25;
    bool denyCost = false;
    auto charge = [&](const ActivationEvent& event) -> ayt::ability::Result<void> {
        const int amount = static_cast<int>(event.resolved.cost->second);
        if (denyCost || mana < amount)
            return std::unexpected(ayt::ability::Diagnostic{ayt::ability::Error::Empty,"mana",{}});
        mana -= amount;
        return {};
    };
    auto denied = abilities.activate({actor,1,404},
        [](const ActivationEvent&) -> ayt::ability::Result<void> {
            return std::unexpected(ayt::ability::Diagnostic{ayt::ability::Error::Empty,"denied",{}});
        });
    require(!denied && denied.error().code==ayt::ability::Error::Empty && mana==25,
        "rejected input changed resources");
    FixedActionDefinition slashPhases;
    slashPhases.id=30;
    slashPhases.windupTicks=1;
    slashPhases.activeTicks=1;
    slashPhases.recoveryTicks=1;
    slashPhases.inputBufferTicks=1;
    slashPhases.markers={{1,1},{1,2}}; // Hand query and presentation effect.
    FixedActionDefinition dodgePhases;
    dodgePhases.id=20;
    dodgePhases.activeTicks=3;
    auto timeline=take(FixedActionTimeline::compile({slashPhases,dodgePhases}));
    require(timeline.canStart(30,1).has_value(),"attack start precheck");
    const auto attack = take(abilities.activate({actor,1,404},charge));
    require(attack.execution==1 && mana==15, "attack precommit/cooldown");
    require(timeline.start(30,attack.execution,1).has_value(),"attack timeline start");
    DerivedDefinition delivery;
    delivery.stages = {{.id=1,.kind=DeliveryKind::Contact,.amountParameter=2,
        .lifetimeTicks=1,.delayTicks=1}};
    const auto release = take(makeDerivedRelease(delivery,attack,std::nullopt,1,1,1,1));
    require(release.emissions.size()==1 && release.emissions[0].tick==2,
        "attack active phase did not begin at fixed tick 2");

    PhysicsBackendDescriptor backend;
    backend.kind3D=BackendKind::DefaultJolt;
    backend.kind2D=BackendKind::Null;
    backend.maxBodies=128;
    backend.maxBodyPairs=512;
    backend.maxContactConstraints=512;
    auto manager=PhysicsManager::create(backend);
    require(manager && manager->world3D(),"Jolt manager unavailable");
    auto* world=manager->world3D();
    RigidbodyDesc body;
    body.type=BodyType::Static;
    body.position={1.75f,0.9f,0};
    body.layer=1;
    BodyHandle targetBody;
    accepted(world->createRigidbody(body,targetBody));
    ColliderDesc shape;
    shape.body=targetBody;
    shape.halfExtents={0.2f,0.5f,0.5f};
    ColliderHandle targetCollider;
    accepted(world->createCollider(shape,targetCollider));
    body.position={1.75f,0.9f,0.8f};
    BodyHandle secondBody;
    accepted(world->createRigidbody(body,secondBody));
    shape.body=secondBody;
    shape.halfExtents={0.2f,0.5f,0.2f};
    ColliderHandle secondCollider;
    accepted(world->createCollider(shape,secondCollider));
    CharacterController3DDesc characterDesc;
    characterDesc.layer=2;
    characterDesc.collideMask=1u<<0; // target hurtbox does not block actor movement
    CharacterHandle character;
    accepted(world->createCharacter(characterDesc,character));
    CharacterController3DInput vacuum;
    vacuum.useGravityOverride=true;
    accepted(world->setCharacterInput(character,vacuum));

    ayt::anim::AnimationPlayer animation;
    animation.setSkeleton(skeleton());
    auto attackClip=clip(1.0f,"Swing");
    auto dodgeClip=clip(-1.0f,nullptr);
    animation.play(&attackClip);
    animation.setLoop(false);
    require(animation.setRootMotionBone("Root"),"root motion track missing");
    HitLedger ledger;
    const AttackRef attackRef{1,attack.execution};
    require(ledger.open(attackRef,{},2,3).has_value(),"hit window open");

    // Fixed tick 1: windup, then physics and authoritative feet snapshot.
    const auto windup=take(timeline.step(1));
    require(windup.phase==FixedActionPhase::Windup,"attack windup");
    animation.tick(dt);
    animation.evaluate();
    const auto firstDelta=animation.consumeRootMotionTranslation();
    accepted(world->setCharacterStepDisplacement(character,firstDelta));
    accepted(manager->stepAndWait(dt));

    // Fixed tick 2: active window. Pose and hand attachment are presentation;
    // the controller's completed feet state anchors the authoritative query.
    const auto active=take(timeline.step(2));
    require(active.phase==FixedActionPhase::Active,"attack active phase");
    bool hitMarker=false, effectMarker=false;
    for (const auto& cue:active.cues)
        if (cue.kind==FixedActionCueKind::Marker) {
            if (cue.marker==1) hitMarker=true;
            if (cue.marker==2) effectMarker=true;
        }
    require(hitMarker,"attack hit marker");
    animation.tick(dt);
    animation.evaluate();
    const auto secondDelta=animation.consumeRootMotionTranslation();
    accepted(world->setCharacterStepDisplacement(character,secondDelta));
    accepted(manager->stepAndWait(dt));
    CharacterController3DState actorState;
    accepted(world->getCharacterState(character,actorState));
    const auto notifies=animation.consumePendingNotifiesMerged();
    bool swing=false;
    for (const auto& marker:notifies)
        if (marker.name && std::string(marker.name)=="Swing") swing=true;
    const auto actorWorld=ayt::math::Float4x4::translation(actorState.position);
    ayt::math::Float4x4 handWorld;
    require(animation.socketWorldTransform(1,actorWorld,
        ayt::math::Float4x4::identity(),handWorld),"hand socket unavailable");
    SphereSweep sweep;
    sweep.origin=handWorld.transformPoint({0,0,0});
    sweep.displacement={1.0f,0,0};
    sweep.radius=0.1f;
    sweep.layerMask=1u<<1;
    RaycastHit contact;
    accepted(world->sphereSweepSync(sweep,contact));
    require(contact.hit && contact.body==targetBody && contact.collider==targetCollider,
        "hand hit did not resolve exact collider");
    ayt::math::Float4x4 secondHandWorld;
    require(animation.socketWorldTransform(1,actorWorld,
        ayt::math::Float4x4::translation({0,0,0.8f}),secondHandWorld),
        "second hand socket unavailable");
    sweep.origin=secondHandWorld.transformPoint({0,0,0});
    RaycastHit secondContact;
    accepted(world->sphereSweepSync(sweep,secondContact));
    require(secondContact.hit && secondContact.body==secondBody
        && secondContact.collider==secondCollider,
        "second hand query did not resolve exact collider");

    auto schema = ayt::stats::Schema::compile({ayt::stats::Definition{
        .name="health",.shape=ayt::stats::Shape::Pool,.current=100,.capacity=100}});
    require(schema.has_value(),"health schema");
    auto health=ayt::stats::StatSet::create(*schema);
    require(health.has_value(),"health state");
    auto secondHealth=ayt::stats::StatSet::create(*schema);
    require(secondHealth.has_value(),"second health state");
    auto formula=ayt::stats::CalculationPlan::compile({
        {.id=1,.op=ayt::stats::Op::Input,.binding=0}},1,1);
    require(formula.has_value(),"damage formula");
    RelationTable relations;
    require(relations.set(1,2,Relation::Hostile).has_value(),"hostile relation");
    Result output;
    output.attackExecution=attack.execution;
    output.attackHitTick=2;
    output.manaAfterAttack=mana;
    output.positionAfterAttack=actorState.position.x;
    output.swingNotify=swing;
    output.visualEffectCue=effectMarker;
    output.exactCollider=true;
    if (!targetDisappearsBeforeHit) {
        SettlementRequest request{
            .hit={attackRef,1,target,2},
            .source={.instigator=attacker,.attribution=1,.team=1,
                .ability=404,.abilityInstance=attack.execution},
            .target={target,2,mask(Capability::Damageable)},
            .resource=statId("health"),.expectedVersion=health->version(),
            .calculation={.seed=123,.tick=2,.calculation=1,.hit=1}};
        const std::array<ayt::stats::Value,1> amount{release.stages[0].amount};
        auto settled=settle(request,relations,ledger,*health,*formula,amount);
        if (!settled) throw std::runtime_error("combat settlement: "+settled.error().message);
        require(settled->state==OutcomeState::Applied,
            "combat settlement rejected target");
        require(!settle(request,relations,ledger,*health,*formula,amount),"duplicate hit applied");
        request.hit={attackRef,2,secondTarget,2};
        request.target={secondTarget,2,mask(Capability::Damageable)};
        request.expectedVersion=secondHealth->version();
        request.calculation.hit=2;
        auto secondSettled=settle(request,relations,ledger,*secondHealth,*formula,amount);
        require(secondSettled && secondSettled->state==OutcomeState::Applied,
            "second target was not settled");
        require(!settle(request,relations,ledger,*secondHealth,*formula,amount),
            "second duplicate hit applied");
    }
    // A body may still be in the physics snapshot after its gameplay target
    // disappears. The host refuses to map it to a live CombatantView.
    auto current=health->read(statId("health"));
    require(current.has_value(),"health read");
    output.healthAfterHit=static_cast<int>(current->current);
    const auto secondCurrent=secondHealth->read(statId("health"));
    require(secondCurrent.has_value(),"second health read");
    output.secondHealthAfterHit=static_cast<int>(secondCurrent->current);
    output.boundaryRejected=!ledger.reserve({attackRef,2,target,3}).has_value();
    require(ledger.close(attackRef).has_value(),"hit window close");

    // Fixed tick 3: recovery has no hit allowance, but authored movement
    // still goes through the same collision-controlled submission path.
    require(timeline.buffer(20,3).has_value(),"dodge input buffer");
    const auto recovery=take(timeline.step(3));
    require(recovery.phase==FixedActionPhase::Recovery &&
        recovery.readyBufferedAction==20,"attack recovery/input buffer");
    animation.tick(dt);
    animation.evaluate();
    accepted(world->setCharacterStepDisplacement(character,
        animation.consumeRootMotionTranslation()));
    accepted(manager->stepAndWait(dt));

    // Fixed tick 4: dodge acceptance has its own cost/cooldown. Tick 5 stun
    // interrupts it before animation/physics submission; no motion is replayed.
    require(timeline.canStart(recovery.readyBufferedAction,4).has_value(),
        "buffered dodge start precheck");
    if (denyBufferedDodge) {
        denyCost = true;
        const auto deniedDodge=abilities.activate({actor,4,303},charge);
        require(!deniedDodge && deniedDodge.error().code==ayt::ability::Error::Empty
            && mana==15 && !timeline.running(),
            "denied buffered intent charged or started");
        animation.stop();
        require(take(timeline.step(4)).phase==FixedActionPhase::Idle,
            "denied buffered intent left action active");
        accepted(world->getCharacterState(character,actorState));
        const auto feet=actorState.position;
        accepted(manager->stepAndWait(dt));
        accepted(world->getCharacterState(character,actorState));
        output.deniedBufferedActionStayedIdle =
            animation.consumeRootMotionTranslation().lengthSq()==0 &&
            std::abs(actorState.position.x-feet.x)<0.06f &&
            timeline.canStart(20,5).has_value();
        return output;
    }
    const auto dodgeAccepted=take(abilities.activate({actor,4,303},charge));
    require(dodgeAccepted.execution==2 && mana==5,"dodge precommit");
    require(timeline.start(recovery.readyBufferedAction,dodgeAccepted.execution,4)
        .has_value(),"dodge timeline start");
    require(take(timeline.step(4)).phase==FixedActionPhase::Active,"dodge active phase");
    output.manaAfterDodge=mana;
    animation.play(&dodgeClip);
    require(animation.hasRootMotionTrack(),"dodge root track missing");
    animation.tick(dt);
    animation.evaluate();
    accepted(world->setCharacterStepDisplacement(character,
        animation.consumeRootMotionTranslation()));
    accepted(manager->stepAndWait(dt));
    const auto interruption=take(timeline.cancel(5,true));
    require(interruption.kind==FixedActionCueKind::Interrupted &&
        take(timeline.step(5)).phase==FixedActionPhase::Idle,"dodge interruption");
    animation.stop(); // stun/cancel at next fixed tick, before movement submit
    accepted(world->getCharacterState(character,actorState));
    output.positionAfterInterrupt=actorState.position.x;
    accepted(manager->stepAndWait(dt));
    CharacterController3DState afterInterrupt;
    accepted(world->getCharacterState(character,afterInterrupt));
    output.cancelledDodgeStopped=
        animation.consumeRootMotionTranslation().lengthSq()==0 &&
        std::abs(afterInterrupt.position.x-actorState.position.x)<0.06f;
    return output;
}
} // namespace ayt::ability::action3d_example
