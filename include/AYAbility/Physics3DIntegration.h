#pragma once
#include <AYAbility/EntityIntegration.h>
#include <AYAbility/Authoring.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYMath/MathTypes.h>
#include <map>

namespace ayt::ability {
/// Optional fixed-Sim Jolt adapter. Ability retains release, hit and effect authority.
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
    const std::vector<Diagnostic>& errors() const noexcept { return _errors; }
private:
    struct Projectile {
        EmissionCommand command;
        Delivery3DProfile profile;
        ayt::math::FVector3 position{};
        ayt::physics::BodyHandle sourceBody=ayt::physics::InvalidBodyHandle;
        std::uint32_t age=0;
    };
    ayt::entity::EntityHandle target(ayt::physics::BodyHandle body) const;
    ayt::entity::World* _world=nullptr;
    AbilitySystem* _ability=nullptr;
    ayt::physics::PhysicsManager* _physics=nullptr;
    std::map<std::pair<Id,Id>,Delivery3DProfile> _profiles;
    std::map<Identity,Projectile> _projectiles;
    std::map<ayt::physics::ColliderHandle,std::uint32_t> _hitParts;
    std::vector<Diagnostic> _errors;
};
/// Install one delivery dimension per World. Keep PhysicsManager alive through shutdown.
Result<Ability3DDeliverySystem*> installAbility3D(ayt::entity::World& world,
    AbilitySystem& ability,ayt::physics::PhysicsManager& physics);
/// Simple document composition entry; Combat-owned effect definitions are supplied by ID.
Result<Ability3DDeliverySystem*> installAbility3D(ayt::entity::World& world,
    ayt::physics::PhysicsManager& physics,const AuthoringDocument& document,
    const OwnedEffectBindings& effects={});
} // namespace ayt::ability
