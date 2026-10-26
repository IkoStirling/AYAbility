#include <AYTest.h>
#include <AYAbility/SurfaceMedium3D.h>
#include <cmath>

using namespace ayt::ability;

TEST_SUITE(SurfaceMedium3DTests)
TEST_CASE(head_on_and_grazing_paths_consume_authored_budget) {
    const SurfaceMedium3D medium{2,3,1,0.25f};
    const auto head=planSurfacePenetration3D({1,0,0},{-1,0,0},medium,10);
    CHECK_TRUE(head.has_value());
    CHECK_TRUE(std::abs(head->cost-7)<0.0001f);
    CHECK_TRUE(head->passes && std::abs(head->remainingBudget-3)<0.0001f);
    const auto grazing=planSurfacePenetration3D({1,0,0},{0,1,0},medium,30);
    CHECK_TRUE(grazing.has_value());
    CHECK_TRUE(std::abs(grazing->effectivePath-8)<0.0001f);
    CHECK_TRUE(std::abs(grazing->cost-25)<0.0001f);
    CHECK_TRUE(grazing->passes && std::abs(grazing->remainingBudget-5)<0.0001f);
    const auto blocked=planSurfacePenetration3D({1,0,0},{0,1,0},medium,20);
    CHECK_TRUE(blocked.has_value());
    CHECK_FALSE(blocked->passes);
    CHECK_TRUE(blocked->remainingBudget==20);
}
TEST_CASE(invalid_geometry_and_profiles_fail_without_budget_change) {
    CHECK_FALSE(planSurfacePenetration3D({0,0,0},{1,0,0},SurfaceMedium3D{1,1},10).has_value());
    CHECK_FALSE(planSurfacePenetration3D({1,0,0},{0,0,0},SurfaceMedium3D{1,1},10).has_value());
    CHECK_FALSE(planSurfacePenetration3D({1,0,0},{1,0,0},SurfaceMedium3D{-1,1},10).has_value());
    CHECK_FALSE(planSurfacePenetration3D({1,0,0},{1,0,0},SurfaceMedium3D{1,1},-1).has_value());
}
TEST_SUITE_END
