#pragma once
#include <AYAbility/EntityIntegration.h>
#include <AYAbility/Authoring.h>
#include <AYAbility/SurfaceMedium3D.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYCombat/Physics3DArea.h>
#include <AYMath/MathTypes.h>
#include <map>

namespace ayt::ability {
/// Optional fixed-Sim Jolt adapter. Moving stages consume one bounded multi-hit
/// segment per step; Ability retains release, hit and effect authority.
class Ability3DDeliverySystem final : public ayt::entity::ISystem {
public:
    const char* getName() const override { return "Ability3DDeliverySystem"; }
    void onUpdate(float fixedSeconds) override;
    Result<void> configure(Id action,Id stage,Delivery3DProfile profile);
    /// Replaces all authored profiles only after every emitted stage has a 3D binding.
    Result<void> configure(const AuthoringDocument& document);
    void bind(ayt::entity::World& world,AbilitySystem& ability,
        ayt::physics::PhysicsManager& physics);
    /// Map an exact Jolt collider to a Combat hit-part ID for contact stages.
    /// Zero removes the mapping. Overlap stages retain whole-body part zero.
    /// Clear/rebind mappings when collider handles or the physics world change.
    void setHitPart(ayt::physics::ColliderHandle collider,std::uint32_t part);
    /// Bind a finite nonnegative cost for passing one collider surface. Zero
    /// removes the mapping; bindings are cleared on World/physics rebind.
    Result<void> setSurfaceResistance(ayt::physics::ColliderHandle collider,float cost);
    /// Bind an authored thickness and resistance to an exact collider. The
    /// current hit normal estimates the travel path; no exit geometry is inferred.
    /// A successful bind replaces any fixed resistance for that collider.
    Result<void> setSurfaceMedium(ayt::physics::ColliderHandle collider,
        const SurfaceMedium3D& medium);
    void clearSurfaceMedium(ayt::physics::ColliderHandle collider);
    const std::vector<Diagnostic>& errors() const noexcept { return _errors; }
private:
    struct Projectile {
        EmissionCommand command;
        Delivery3DProfile profile;
        ayt::math::FVector3 position{};
        ayt::physics::BodyHandle sourceBody=ayt::physics::InvalidBodyHandle;
        std::uint32_t age=0;
        float remainingPenetration=0.0f;
    };
    ayt::entity::EntityHandle target(ayt::physics::BodyHandle body) const;
    ayt::entity::World* _world=nullptr;
    AbilitySystem* _ability=nullptr;
    ayt::physics::PhysicsManager* _physics=nullptr;
    std::map<std::pair<Id,Id>,Delivery3DProfile> _profiles;
    std::map<Identity,Projectile> _projectiles;
    std::map<ayt::physics::ColliderHandle,std::uint32_t> _hitParts;
    std::map<ayt::physics::ColliderHandle,float> _surfaceResistance;
    std::map<ayt::physics::ColliderHandle,SurfaceMedium3D> _surfaceMedia;
    std::vector<Diagnostic> _errors;
};
/// Install one delivery dimension per World. Keep PhysicsManager alive through shutdown.
Result<Ability3DDeliverySystem*> installAbility3D(ayt::entity::World& world,
    AbilitySystem& ability,ayt::physics::PhysicsManager& physics);
/// Simple document composition entry; Combat-owned effect definitions are supplied by ID.
Result<Ability3DDeliverySystem*> installAbility3D(ayt::entity::World& world,
    ayt::physics::PhysicsManager& physics,const AuthoringDocument& document,
    const OwnedEffectBindings& effects={});
/// Bind an active area skill to a source-following 3D sphere or oriented-box query.
/// Selection remains a pure game-supplied fact; Combat owns membership and effects.
Result<void> configureAreaSkill3D(AbilitySystem& ability, ayt::entity::World& world,
    ayt::physics::PhysicsManager& physics, Id skill,
    ayt::combat::AreaDefinition definition, ayt::combat::AreaQuery3D query,
    ayt::combat::AreaSelection3D selection = {});
} // namespace ayt::ability
