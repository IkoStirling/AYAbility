#pragma once
#include <AYAbility/EntityIntegration.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYMath/MathTypes.h>
#include <map>

namespace ayt::ability {
/// Fixed-tick 2D delivery recipe. The XY velocity is world-space units per second.
struct Delivery2DProfile {
    ayt::math::FVector3 velocity{};
    ayt::math::FVector3 spawnOffset{};
    float radius=0.0f;
    ayt::physics::PhysLayerMask layerMask=0xffffffffu;
    std::uint32_t reverseAfterTicks=0;
};
/// Owns movement and 2D physics queries; Ability retains costs, targeting and effects.
/// Install after Ability and keep PhysicsManager alive until this system is removed.
class Ability2DDeliverySystem final : public ayt::entity::ISystem {
public:
    const char* getName() const override { return "Ability2DDeliverySystem"; }
    void onUpdate(float fixedSeconds) override;
    Result<void> configure(Id action,Id stage,Delivery2DProfile profile);
    void bind(ayt::entity::World& world,AbilitySystem& ability,
        ayt::physics::PhysicsManager& physics);
    const std::vector<Diagnostic>& errors() const noexcept { return _errors; }
private:
    struct Projectile {
        EmissionCommand command;
        Delivery2DProfile profile;
        ayt::math::FVector3 position{};
        std::uint32_t age=0;
    };
    ayt::entity::EntityHandle target(ayt::physics::BodyHandle body) const;
    ayt::entity::World* _world=nullptr;
    AbilitySystem* _ability=nullptr;
    ayt::physics::PhysicsManager* _physics=nullptr;
    std::map<std::pair<Id,Id>,Delivery2DProfile> _profiles;
    std::map<Identity,Projectile> _projectiles;
    std::vector<Diagnostic> _errors;
};
/// Register one World-owned adapter in the Sim lane after AbilitySystem.
Result<Ability2DDeliverySystem*> installAbility2D(ayt::entity::World& world,
    AbilitySystem& ability,ayt::physics::PhysicsManager& physics);
} // namespace ayt::ability
