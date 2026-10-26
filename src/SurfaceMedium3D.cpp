#include <AYAbility/SurfaceMedium3D.h>

#include <algorithm>
#include <cmath>

namespace ayt::ability {
Result<SurfacePenetration3D> planSurfacePenetration3D(
    std::array<float,3> travel, std::array<float,3> normal,
    const SurfaceMedium3D& medium, float availableBudget) {
    const auto invalid = [](const char* message) -> Result<SurfacePenetration3D> {
        return std::unexpected(Diagnostic{Error::Invalid,message,{}});
    };
    for (auto value : travel) if (!std::isfinite(value)) return invalid("nonfinite ballistic travel");
    for (auto value : normal) if (!std::isfinite(value)) return invalid("nonfinite surface normal");
    if (!std::isfinite(availableBudget) || availableBudget < 0 ||
        !std::isfinite(medium.thickness) || medium.thickness <= 0 || medium.thickness > 1000 ||
        !std::isfinite(medium.resistancePerUnit) || medium.resistancePerUnit < 0 ||
        medium.resistancePerUnit > 1'000'000 ||
        !std::isfinite(medium.baseResistance) || medium.baseResistance < 0 ||
        medium.baseResistance > 1'000'000 ||
        !std::isfinite(medium.minimumIncidenceCosine) ||
        medium.minimumIncidenceCosine < 0.01f || medium.minimumIncidenceCosine > 1)
        return invalid("invalid ballistic surface profile or budget");
    const double travelSq = double(travel[0])*travel[0] + double(travel[1])*travel[1]
        + double(travel[2])*travel[2];
    const double normalSq = double(normal[0])*normal[0] + double(normal[1])*normal[1]
        + double(normal[2])*normal[2];
    if (travelSq < 1e-12 || normalSq < 1e-12)
        return invalid("zero ballistic direction or normal");
    const double dot = double(travel[0])*normal[0] + double(travel[1])*normal[1]
        + double(travel[2])*normal[2];
    const double cosine = std::clamp(std::abs(dot) / std::sqrt(travelSq*normalSq),0.0,1.0);
    const double path = double(medium.thickness) /
        std::max(cosine,double(medium.minimumIncidenceCosine));
    const double cost = double(medium.baseResistance) +
        path * double(medium.resistancePerUnit);
    if (!std::isfinite(cost) || cost > 1.1e11)
        return invalid("ballistic surface cost out of range");
    const bool passes = cost <= availableBudget;
    return SurfacePenetration3D{float(cosine),float(path),float(cost),
        passes ? float(double(availableBudget)-cost) : availableBudget,passes};
}
} // namespace ayt::ability
