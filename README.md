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
With the optional 2D adapter, `configureAreaSkill2D` accepts a source-following
circle radius, XY offset, physics layer mask and optional dynamic selection
predicate. The source needs a Transform; targets need current 2D RigidBody
bindings. Box2D supplies complete overlap candidates on each fixed Sim step,
while Combat rechecks selection, relations and effects. If the permission requires
selection, a predicate must be configured. World and PhysicsManager must outlive
the area. Gameplay still calls only `castArea(actor, skill)`.

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

`AYAbility::Physics2D` is an optional real Box2D adapter. Install it after
Ability with `installAbility2D(world, ability, physicsManager, document)`;
the document supplies validated world-space stage velocity, radius, query layer
and physical collision responses. The advanced overload accepts manual profiles.
It drains Ability's
emissions during fixed Sim, raycasts moving contact/fragment/return stages,
overlaps explosion/hazard stages, maps 2D bodies to Entity targets, and reports
hits back to Ability. Keep the physics manager alive through World shutdown.
Moving stages can ignore their source body, stop at or pass through obstacles,
and stop at or pierce Combat targets. Cancelled/expired emitters are discarded
before further physics queries; hazard overlap stops when its stage ends.
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

An Actor class can author repeatable `AbilityEquipmentComponent` and
`AbilitySkillComponent` instances alongside Stats and Combat. Each equipment
instance names a configuration and one may set `equipped`; each skill instance
names a skill and level. `registerAbilityGameplayComponents` registers all four
component types. The first Ability action hydrates them immediately, including
after Actor/Scene loading before the first Sim tick. Changing an instance's
display name never changes gameplay identity. Equipment configuration and skill
grants must exist in the installed catalog; duplicate grants or multiple equipped
slots are rejected. These components are authoring defaults, not live ammo or
cooldown checkpoints.

An editor, arbitrary behavior graphs, full area release provenance, Combo,
3D delivery, exact HP snapshot restoration and checkpointing remain unimplemented.
The [execution plan](../../AYDocs/AYABILITY-IMPLEMENTATION-PLAN.md) tracks those
stages. The module is registered as a Git submodule and does not require renderer, input devices or
physics to build its Core.
