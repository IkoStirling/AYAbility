#include "Action3DScenario.h"
#include <iostream>

int main() {
    try {
        const auto result=ayt::ability::action3d_example::run();
        std::cout << "attack execution=" << result.attackExecution
                  << " hit tick=" << result.attackHitTick
                  << " target health=" << result.healthAfterHit
                  << " mana=" << result.manaAfterDodge
                  << " feet x=" << result.positionAfterInterrupt << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
