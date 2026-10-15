# AYAbility integration contracts

The World owns the Ability runtime. Stats owns numerical state and Combat owns
target permission, life and effects. Ability owns equipment ammunition/cadence,
character skill levels/cooldowns and release timelines. A fixed Sim tick drives
all three; gameplay entries use Entity handles and resolve the current owner.

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
