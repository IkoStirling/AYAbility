# AYAbility integration contracts

The World owns the Ability runtime. Stats owns numerical state and Combat owns
target permission, life and effects. Ability owns equipment ammunition/cadence,
character skill levels/cooldowns and release timelines. A fixed Sim tick drives
all three; gameplay entries use Entity handles and resolve the current owner.

Equipment removal is explicit: `unequip` clears selection while keeping the
owned instance; `removeEquipment` validates the owner, clears selection if
necessary, and erases only that instance. Both reject calls from a resource
precommit callback (`Busy`). A removed instance is absent from checkpoints;
already released casts retain their snapshots and source-loss policy. A later
`addEquipment` creates fresh ammo/reload/cadence state, so a game needing item
state preservation can use `transferEquipment` within the same runtime. That
operation checks source ownership and destination selection before mutation,
changes the instance's owner, preserves its numeric state, clears source
selection and optionally selects it at the destination. An occupied
destination selection rejects `equipOnArrival` without moving the item.
Inventory ownership and Ability ownership are distinct commit domains; the
game transaction owner must roll back an external inventory transfer if the
Ability operation rejects. Released casts retain their prior source snapshot
and lifetime policy.

`examples/Action3DScenario.h` is a host composition example, not a gameplay
rule installed by Ability. Its fixed ticks order input acceptance and resource
precommit, action phase selection, Animation root-motion extraction, Physics
controller sweep, completed-state/evaluated-socket queries, two-target Combat hit settlement,
and animation Notify consumption. It exercises attack, dodge, interruption,
missing target mapping and a half-open same-tick hit window. The host owns
Entity mapping; Ability's Derived stage retains the fixed delay and release
snapshot.
Its denied-buffered-dodge branch verifies that a failed resource precommit
leaves the timeline idle, preserves mana and submits no root displacement.

`FixedActionTimeline` is the independent data clock used by that host. Compile
validates nonzero active duration, bounded total duration, unique action IDs,
marker offsets and duplicate marker IDs on one tick; definitions are sorted
and fingerprinted. The host calls `canStart` before Ability resource precommit,
then `start` binds its accepted
execution at the next unprocessed tick without an intervening timeline mutation.
Each `step` requires
the next contiguous tick, emits phase-entry and authored marker cues in order,
then a finish cue on the action's final tick. A one-slot input intent may be
buffered only in the current action's final authored window; completion releases
its action ID for a fresh Ability activation and resource precommit. `cancel`
or interruption before the next step discards both the running action and input
buffer, returning a cue so the host can stop root motion and pending deliveries.

`FixedActionDocument` is the optional authoring edge for the same data clock:
version-1 JSON contains bounded numeric action IDs, durations, input-buffer
ticks and zero-based marker offsets. Loading and saving compile a canonical
ID/tick-ordered value first, so malformed content never replaces a running
timeline. A sample document can drive the headless 3D scenario; the host still
binds marker IDs to gameplay/animation/Combat behavior and owns checkpoint
publication. Documents are capped at 1 MiB, 128 actions and 4096 markers.
The pointer-free snapshot captures the current tick, execution, phase and
buffered input; restore validates definition fingerprint and all tick/phase
bounds before changing live state. The timeline does not dispatch hits, own
entities, choose combat rules or advance animation itself.
In particular, a running snapshot at its final tick is invalid because that
tick would already have emitted Finished. Cancel and replacement start can
occur before the same unprocessed tick; only one `step` advances that tick.

`AbilityEquipmentComponent` and `AbilitySkillComponent` are repeatable Actor/Scene
authoring slots. Equipment slots contain catalog configuration IDs and one
optional equipped selection. Skill slots contain catalog skill IDs and levels.
The Entity component instance ID persists through authoring and its display name
is presentation only. Runtime equipment IDs are allocated per World and are not
derived from names. The first Ability access validates the slots and hydrates
the actor; it can happen before the first fixed tick. Duplicate skill grants,
invalid catalog references and multiple equipped slots fail before hydration.
Changing authoring slots after first access does not rewrite live ammunition or
cooldowns; explicit runtime operations own subsequent changes.

Actor defaults and Scene data do not include active cooldowns, ammunition,
release timelines, live Combat effects or Stats current-state checkpoints.
Runtime save/restore needs a separate coordinated checkpoint contract.

The optional Inventory effects adapter copies the headless Runtime, validates
equipment changes there, then calls `swapState` to publish at a quiescent Sim
boundary. The swap exchanges catalog, actor/equipment state and execution
sequence without allocation; an active fire pre-commit callback refuses it.
This gives the adapter a no-throw final publication step alongside Stats' own
no-throw state swap. Game code still owns inventory transaction ordering.

The Core `RuntimeCheckpoint` is that Ability-owned portion: a versioned owned
snapshot of actors, selected equipment, ammunition, reload/cadence, skill levels,
cooldowns and next execution ID. Restore builds and validates a replacement
against the installed catalog before publishing it, so invalid input leaves the
old runtime intact. JSON encoding lives in Authoring. World/Entity and active
delivery lifetimes are outside this Core snapshot.

For 2D contact stages, Box2D returns an exact collider handle and the delivery
adapter maps it to an authored Combat part ID. Part-policy deduplication uses
the reported part. Body-only overlaps have no shape identity and use part zero.

The independent ComboTracker matches accepted release tokens against ordered
fixed-tick gaps. AbilitySystem records a token after each successful release and
returns matched events without coupling combo content to hit/damage policy.

Physics3D uses Jolt raycasts for moving stages and sphere overlaps for explosion
and hazard stages. Version-1 authored 3D profiles undergo full stage coverage
preflight before the simple install mutates World. A World can own only one
physics delivery adapter because emissions are a destructive queue. Jolt rays
now resolve exact generation-checked ColliderHandles through compound shapes.
The 3D adapter maps them through `setHitPart(collider, part)` before reporting
the contact to Combat, matching the 2D path. Zero erases a mapping; `bind`
clears it, and a newly generated collider does not inherit an old mapping.
Body-only overlaps and unmapped contacts still use part zero. The real Jolt
integration test hits two different shapes on one body and checks parts 7/8,
then removes the first mapping and checks part zero and resulting health.

## Editor diagnostic observations (2026-10-15)

The optional Physics2D/Physics3D delivery adapters record finite swept segments, area regions and actual settlement observations through PhysicsManager::debugDraw. A red target marker means Applied/allowed; denied, duplicate or failed reports remain distinct. This never changes hit reservation, permission, simulation cadence or gameplay callbacks.
Capture follows the shared debug lifetime, freeze and budget; disabled capture
has no retained history. Diagnostics are not checkpointed or serialized.

## Debug content extension (2026-10-15)

AbilitySystem::debugRecords copies up to512 equipment/ammo/reload/fire/skill cooldown and delivery-stage rows from existing runtime state. Stage observations include trigger/predecessor/due/end/progress and hit/open/emitted flags. This const diagnostic entry never hydrates authored grants, drains emissions, advances ticks or changes costs. The optional editor Combat/stages category consumes these owned values.

Module-local regression sources were updated. Runtime tests and interactive editor acceptance are deferred by the current task request; previous validation reports cover the earlier baseline. These changes remain uncommitted until focused runtime checks are requested and pass.
