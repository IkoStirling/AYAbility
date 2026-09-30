# AYAbility

Local-only, no-remote action and skill runtime. Core resolves definitions and
executes headless Fire, Area and Derived behaviors. Fire uses distinct
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

Fire delivery and Derived active skills share one bounded stage timeline.
Configure a fire equipment configuration with `configureFireDelivery`, then call
`fireDelivered(actor, optionalSkill)`; an active Derived skill instead calls
`castDerived(actor, skill)`. Both freeze the resolved values, source and emitter
identities, charge Stats once, and open Combat hit windows before publishing
commands. Stages can start on release, first confirmed hit or predecessor end,
with fixed-tick delays. Contact positions propagate into dependent stages, so
an explosion can originate at the actual impact. Damage, heal, non-HP attribute
increase/decrease/set and Combat-owned effects use explicit target masks.
Multiple emitters in one stage share a hit window; each stage has its own.
`finishDerivedEmitter` and `cancelDerivedCast` close windows early.

`AYAbility::Physics2D` is an optional real Box2D adapter. Install it once after
Ability with `installAbility2D(world, ability, physicsManager)` and configure
world-space stage velocity, radius and query layer once. It drains Ability's
emissions during fixed Sim, raycasts moving contact/fragment/return stages,
overlaps explosion/hazard stages, maps 2D bodies to Entity targets, and reports
hits back to Ability. Keep the physics manager alive through World shutdown.
Register Entity physics components before sealing the component registry and
attach each target's 2D `RigidBodyComponent` to its actual Box2D body handle.
The default source-loss policy cancels on death/destruction;
`ContinueFromSnapshot` allows detached numeric hits. Explicit Stats recipes
that need live source attributes, attribute Set and Combat-owned effect stages
require a live source.

`AYAbility::Authoring` loads and saves a version-1 loose JSON document containing
actions, equipment configurations, skill growth tracks and stage definitions.
`compileAuthoring` validates references and units before installation;
`installAbility(world, document)` installs the catalog and delivery definitions
together. Combat effect references are declared in the document and bound to
actual Combat effect definitions at composition time. A failed parse or compile
does not mutate an already installed catalog.

An editor, arbitrary behavior graphs, full area release provenance, Combo,
3D delivery, exact HP snapshot restoration and checkpointing remain unimplemented.
The [execution plan](../../AYDocs/AYABILITY-IMPLEMENTATION-PLAN.md) tracks those
stages. The module has no remote and does not require renderer, input devices or
physics to build its Core.
