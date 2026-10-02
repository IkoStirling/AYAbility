#pragma once
#include <AYAbility/EntityIntegration.h>
#include <AYAbility/Authoring.h>
#include <AYCombat/Physics2DArea.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYMath/MathTypes.h>
#include <map>
#include <string>

namespace ayt::ability {
/// Owns movement and 2D physics queries; Ability retains costs, targeting and effects.
/// Install after Ability and keep PhysicsManager alive until this system is removed.
class Ability2DDeliverySystem final : public ayt::entity::ISystem {
public:
    const char* getName() const override { return "Ability2DDeliverySystem"; }
    void onUpdate(float fixedSeconds) override;
    Result<void> configure(Id action,Id stage,Delivery2DProfile profile);
    /// Replaces all stage profiles only after the authored document passes validation.
    Result<void> configure(const AuthoringDocument& document);
    void bind(ayt::entity::World& world,AbilitySystem& ability,
        ayt::physics::PhysicsManager& physics);
    /// Map a live Box2D collider to a Combat hit-part ID for contact stages.
    /// Zero removes the mapping; overlap stages still use whole-body part zero.
    void setHitPart(ayt::physics::ColliderHandle collider,std::uint32_t part);
    const std::vector<Diagnostic>& errors() const noexcept { return _errors; }
private:
    struct Projectile {
        EmissionCommand command;
        Delivery2DProfile profile;
        ayt::math::FVector3 position{};
        ayt::physics::BodyHandle sourceBody=ayt::physics::InvalidBodyHandle;
        std::uint32_t age=0;
    };
    ayt::entity::EntityHandle target(ayt::physics::BodyHandle body) const;
    ayt::entity::World* _world=nullptr;
    AbilitySystem* _ability=nullptr;
    ayt::physics::PhysicsManager* _physics=nullptr;
    std::map<std::pair<Id,Id>,Delivery2DProfile> _profiles;
    std::map<Identity,Projectile> _projectiles;
    std::map<ayt::physics::ColliderHandle,std::uint32_t> _hitParts;
    std::vector<Diagnostic> _errors;
};
/// Register one World-owned adapter in the Sim lane after AbilitySystem.
Result<Ability2DDeliverySystem*> installAbility2D(ayt::entity::World& world,
    AbilitySystem& ability,ayt::physics::PhysicsManager& physics);
/// Selection predicates are named by areaSkills2D.selectionKey and sampled each sweep.
using AreaSelectionBindings2D=std::map<std::string,ayt::combat::AreaSelection2D>;
/// Simple composition entry: preflight the full document and bindings, then install
/// Ability, Combat/Stats dependencies and the optional Box2D adapter together.
Result<Ability2DDeliverySystem*> installAbility2D(ayt::entity::World& world,
    ayt::physics::PhysicsManager& physics,const AuthoringDocument& document,
    const OwnedEffectBindings& effects,
    const AreaSelectionBindings2D& selections={});
/// Advanced entry for an already installed AbilitySystem with manually bound effects.
Result<Ability2DDeliverySystem*> installAbility2D(ayt::entity::World& world,
    AbilitySystem& ability,ayt::physics::PhysicsManager& physics,
    const AuthoringDocument& document,
    const AreaSelectionBindings2D& selections={});
/// Configure a source-following Box2D area once; gameplay still calls castArea(actor, skill).
/// Selection is sampled for each current geometric candidate on every Combat Sim sweep.
Result<void> configureAreaSkill2D(AbilitySystem& ability,ayt::entity::World& world,
    ayt::physics::PhysicsManager& physics,Id skill,
    ayt::combat::AreaDefinition definition,ayt::combat::AreaQuery2D query,
    ayt::combat::AreaSelection2D selection={});
} // namespace ayt::ability
