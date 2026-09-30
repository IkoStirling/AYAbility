#pragma once
#include <AYAbility/Ability.h>
#include <functional>
#include <map>

namespace ayt::ability {
/// Parameter identities understood by the optional reusable fire behavior.
namespace fire {
inline constexpr Id Amount=1, IntervalTicks=2, MinimumIntervalTicks=3, IntervalStepTicks=4;
inline constexpr Id StreakResetTicks=5, MagazineCapacity=6, ReloadTicks=7, Projectiles=8, SkillCooldownTicks=9;
inline constexpr Id Behavior=1;
}

struct EquipmentState {
    Identity owner, identity;
    Id configuration=0;
    Value ammunition=0;
    std::uint32_t streak=0;
    std::uint64_t nextFireTick=0, lastFireTick=0, reloadUntil=0;
    bool reloading=false;
};
struct GrantedSkill { Id skill=0; std::uint32_t level=0; std::uint64_t nextReadyTick=0; };
struct FireEvent {
    std::uint64_t execution=0, tick=0, nextFireTick=0;
    Identity actor, equipment;
    Id activeSkill=0;
    Value amount=0, projectiles=1, ammunitionRemaining=0;
    std::uint32_t streak=0;
    ResolvedAction resolved;
};
struct FireRequest { Identity actor; std::uint64_t tick=0; Id activeSkill=0; };

/// Single-owner headless runtime. The caller supplies the fixed Sim tick.
/// A pre-commit hook can atomically charge an external resource before local state changes.
class Runtime {
public:
    explicit Runtime(std::shared_ptr<const Catalog> catalog) : _catalog(std::move(catalog)) {}
    Result<void> addEquipment(Identity actor, Identity equipment, Id configuration);
    Result<void> equip(Identity actor, Identity equipment);
    Result<void> grant(Identity actor, Id skill, std::uint32_t level);
    Result<void> setLevel(Identity actor, Id skill, std::uint32_t level);
    Result<void> beginReload(Identity actor, std::uint64_t tick);
    Result<FireEvent> fire(FireRequest request,
        const std::function<Result<void>(const FireEvent&)>& preCommit={});
    Result<EquipmentState> equipment(Identity identity) const;
    Result<GrantedSkill> skill(Identity actor, Id skill) const;
    Result<void> removeActor(Identity actor);
private:
    struct ActorState {
        Identity equipped;
        std::map<Id,GrantedSkill> skills;
    };
    std::shared_ptr<const Catalog> _catalog;
    std::map<Identity,ActorState> _actors;
    std::map<Identity,EquipmentState> _equipment;
    std::uint64_t _nextExecution=1;
    bool _committing=false;
};
} // namespace ayt::ability
