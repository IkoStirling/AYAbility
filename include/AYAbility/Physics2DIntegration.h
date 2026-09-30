#pragma once
#include <AYAbility/EntityIntegration.h>
#include <AYAbility/Authoring.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYMath/MathTypes.h>
#include <map>

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
    std::vector<Diagnostic> _errors;
};
/// Register one World-owned adapter in the Sim lane after AbilitySystem.
Result<Ability2DDeliverySystem*> installAbility2D(ayt::entity::World& world,
    AbilitySystem& ability,ayt::physics::PhysicsManager& physics);
/// One-time composition entry: validates and binds every stage profile in the document.
Result<Ability2DDeliverySystem*> installAbility2D(ayt::entity::World& world,
    AbilitySystem& ability,ayt::physics::PhysicsManager& physics,
    const AuthoringDocument& document);
} // namespace ayt::ability
