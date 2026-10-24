#include "Action3DScenario.h"
#include <AYAbility/FixedActionDocument.h>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    try {
        std::vector<ayt::ability::FixedActionDefinition> authored;
        if (argc > 1) {
            std::ifstream file(argv[1], std::ios::binary);
            if (!file) throw std::runtime_error("cannot open action timeline document");
            const std::string json(std::istreambuf_iterator<char>{file}, {});
            auto loaded = ayt::ability::loadFixedActionDocument(json);
            if (!loaded) throw std::runtime_error(loaded.error().message);
            authored = std::move(loaded->actions);
        }
        const auto result=ayt::ability::action3d_example::run(
            false, false, authored.empty() ? nullptr : &authored);
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
