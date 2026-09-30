#pragma once
#include <AYAbility/Runtime.h>
#include <array>

namespace ayt::ability {
/// Delivery is a gameplay declaration. A physics host owns movement and reports contacts.
enum class DeliveryKind { Contact, Explosion, Fragment, Return, Hazard };
enum class SourceLossPolicy { Cancel, ContinueFromSnapshot };
enum class DerivedHitMode { Target, Part, Segment, Periodic };
enum class StageTrigger { Release, FirstConfirmedHit, StageFinished };
enum class StageOperation { Damage, Heal, IncreaseAttribute, DecreaseAttribute, SetAttribute, OwnedEffect };
enum class StageIntent { Harmful, Beneficial };

struct DerivedStage {
    Id id=0;
    DeliveryKind kind=DeliveryKind::Contact;
    std::uint32_t emitters=1;
    /// Zero selects literal amount; otherwise read a Unit::Amount parameter from the cast snapshot.
    Id amountParameter=0;
    Value amount=0;
    std::string recipe;
    DerivedHitMode hitMode=DerivedHitMode::Target;
    std::uint64_t intervalTicks=0, lifetimeTicks=1;
    /// A later stage may wait for its predecessor; all delays use the fixed Sim tick.
    StageTrigger trigger=StageTrigger::Release;
    Id predecessor=0;
    std::uint64_t delayTicks=0;
    /// Optional Unit::Count parameter, e.g. fire::Projectiles after a skill upgrade.
    Id emittersParameter=0;
    StageOperation operation=StageOperation::Damage;
    StageIntent intent=StageIntent::Harmful;
    /// Empty selects the target's mapped life pool. Other names address a Stats attribute.
    std::string attribute;
    /// Same flags as Combat AllowedTarget, validated here without a Combat dependency.
    std::uint32_t allowedTargets=8;
    /// Required for OwnedEffect; the Entity adapter binds an authored Combat effect to it.
    Id effect=0;
};
struct DerivedDefinition {
    SourceLossPolicy sourceLoss=SourceLossPolicy::Cancel;
    std::vector<DerivedStage> stages;
};
/// Owned cast-time values; no Entity or World pointer survives the release.
struct ReleaseSnapshot {
    Identity actor;
    std::optional<Identity> equipment;
    Id skill=0;
    std::uint32_t skillLevel=0;
    std::uint64_t execution=0,tick=0,team=0;
    SourceLossPolicy sourceLoss=SourceLossPolicy::Cancel;
    ResolvedAction resolved;
    Id action=0;
};
struct EmissionCommand {
    Identity emitter;
    Id stage=0;
    DeliveryKind kind=DeliveryKind::Contact;
    std::shared_ptr<const ReleaseSnapshot> source;
    std::uint64_t tick=0;
    std::uint64_t lifetimeTicks=1;
    /// Contact origin carried into dependent stages; absent for root releases.
    std::optional<std::array<float,3>> origin;
};
struct DerivedRelease {
    std::shared_ptr<const ReleaseSnapshot> source;
    std::vector<DerivedStage> stages;
    std::vector<EmissionCommand> emissions;
};

/// Validate a bounded authored delivery graph before gameplay; no projectile simulation lives here.
Result<void> validateDerived(const DerivedDefinition& definition);
/// Freeze per-cast parameters and allocate host-domain emitter identities from a monotonic range.
Result<DerivedRelease> makeDerivedRelease(const DerivedDefinition& definition,
    const ActivationEvent& event,std::optional<Identity> equipment,
    std::uint32_t skillLevel,std::uint64_t team,Id domain,Id firstEmitter);
/// Fire and active skills produce the same release/stage/emitter contract.
Result<DerivedRelease> makeDerivedRelease(const DerivedDefinition& definition,
    const FireEvent& event,std::uint32_t skillLevel,std::uint64_t team,
    Id domain,Id firstEmitter);
} // namespace ayt::ability
