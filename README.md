# AYAbility

Local-only, no-remote action and skill runtime. Current delivery is the independent
Core definition resolver and headless Fire/Area behaviors. Fire uses distinct
equipment configurations; equipment instances own ammunition/cadence, while actor
skill grants own levels and skill cooldowns. All time inputs are caller-supplied
fixed Sim ticks. Author definitions are immutable after catalog compilation and
each accepted fire returns an owned resolved snapshot.

`Catalog::compile` validates typed numeric parameters, constant/table/curve
progression, feature unlocks and references. `Runtime::fire` can call an optional
pre-commit hook for one external atomic cost; failure leaves local ammunition,
cooldowns and execution identity unchanged. A successful shot consumes ammunition
even if a later target interaction misses. The hook must not reenter Runtime.

Optional World integration installs a World-owned AbilitySystem over Stats and
Combat. It charges active skill resource costs through Stats, rejects dead or
stunned actors before spending, and settles selected targets through Combat.
Use `registerAbilityGameplayComponents` before sealing the registry and
`installAbility(world, catalog)` after creating the World. The simple entry is
`AbilitySystem::fireAt`; target settlement errors are returned after the shot
has fired. Each targeted projectile uses one shared Combat attack with a distinct
segment/request identity. Combat retains the actor, equipment provider, ability
content and release instance in its source snapshot. `FireActionResult::hits`
keeps simple results, while `settlements` exposes the corresponding full outcomes.

An active character skill may instead use the Area behavior, with its own cost
and cooldown and no equipment requirement. The composition root registers its
Combat area definition and candidate-provider factory via `configureAreaSkill`.
Gameplay then calls `castArea(actor, skill)` or `stopArea(areaId)`. Combat owns
membership and effect lifetimes, including source-death cleanup. Normal Stats
capacity policies apply to life-capacity modifiers; they do not restore an exact
pre-effect HP snapshot.

Derived active skills declare bounded, named hit stages at composition time.
`castDerived(actor, skill)` freezes the skill level, resolved stage amounts,
optional equipped provider, source team and cast identity; it charges Stats once
and returns immutable spawn commands. A projectile or other delivery host owns
movement and calls `reportDerivedHit(emitter, target)` on contact. Ability uses
Combat to check target permission and deduplicate hits, and Stats for the chosen
recipe and HP mutation. Multiple emitters in one stage share a hit window, while
different stages can hit the same target. `finishDerivedEmitter` and
`cancelDerivedCast` close windows early. The default source-loss policy cancels
on death/destruction; `ContinueFromSnapshot` allows detached delayed hits.
Explicit Stats recipes that need live source attributes cannot run after the
source disappears unless the recipe supports snapshot-only inputs.

Loose data files, an editor, arbitrary behavior graphs, full area release
provenance, Combo, projectile movement/physics adapters and checkpointing are not implemented.
The [execution plan](../../AYDocs/AYABILITY-IMPLEMENTATION-PLAN.md) tracks those
stages. The module has no remote and does not require renderer, input devices or
physics to build its Core.
