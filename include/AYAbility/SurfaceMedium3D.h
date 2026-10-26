#pragma once

#include <AYAbility/Ability.h>
#include <array>

namespace ayt::ability {

/// Host authored material/shape approximation for one exact collider. Jolt
/// supplies an entry normal, but not a reliable physical exit thickness.
struct SurfaceMedium3D {
    float thickness = 0;             // positive authored world units
    float resistancePerUnit = 0;     // nonnegative penetration budget per unit
    float baseResistance = 0;        // nonnegative fixed contact cost
    float minimumIncidenceCosine = 0.05f; // clamps grazing path amplification
};
struct SurfacePenetration3D {
    float incidenceCosine = 0;
    float effectivePath = 0;
    float cost = 0;
    float remainingBudget = 0;
    bool passes = false;
};

/// Pure, bounded contact calculation. Uses |dot(unit travel, unit normal)|
/// so entry/exit normal orientation does not affect authored cost. Failure
/// leaves gameplay state to the caller; it never changes a projectile itself.
Result<SurfacePenetration3D> planSurfacePenetration3D(
    std::array<float,3> travel, std::array<float,3> surfaceNormal,
    const SurfaceMedium3D& medium, float availableBudget);

} // namespace ayt::ability
