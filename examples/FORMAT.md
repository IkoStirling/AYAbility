# Ability authoring format, version 1

`contact2d.ability.json` is an executable sample: `Test_Physics2D.cpp` loads it,
installs Ability in a World, creates a real Box2D body, and verifies a contact
changes HP through Combat and Stats. Parse with `loadAuthoring`, optionally edit
the returned `AuthoringDocument`, then install with `installAbility(world, document)`.
`saveAuthoring` writes a normalized version of the same data. A failed load,
compile or save does not replace an existing World catalog.
`selected-area2d.ability.json` is the corresponding Area example; its Box2D
membership and dynamic selection are exercised in `Test_Physics2D.cpp`.

Top-level arrays are `actions`, `equipment`, `skills`, `fireDeliveries`,
`derivedSkills`, `delivery2D`, `areaSkills2D`, and `effectReferences`. Each stage binding has `owner` and
`definition`. For fire, owner is an equipment configuration ID; for Derived it
is an active skill ID. Every stage ID is unique within its definition. Stage
`predecessor` must name an earlier stage; `trigger` chooses release (0), first
confirmed hit (1), or predecessor finish (2). Delays and lifetimes use fixed Sim
ticks. The `effectReferences` array lists Combat effect IDs used by `OwnedEffect`
stages or 2D Area skills. Provide their `EffectDefinition` values as
`OwnedEffectBindings` when installing. The strict overload requires every listed
reference exactly once, rejects extras and malformed definitions before installing
systems, and keeps effect semantics owned by Combat. Modifier targets still need
the eventual target's Stats schema and are checked when the effect is applied.
Advanced hosts may instead
bind individual references with `configureOwnedEffect`.

Enums are stored as integers in version 1:

| Field | Values |
| --- | --- |
| `unit` | Amount 0, Ticks 1, Count 2, BasisPoints 3 |
| `mode` | Constant 0, Table 1, Curve 2 |
| `operation` on a parameter patch | Replace 0, Add 1, MultiplyBasisPoints 2 |
| `kind` on a skill | Passive 0, Active 1 |
| `kind` on a stage | Contact 0, Explosion 1, Fragment 2, Return 3, Hazard 4 |
| `hitMode` | Target 0, Part 1, Segment 2, Periodic 3 |
| `sourceLoss` | Cancel 0, ContinueFromSnapshot 1 |
| `operation` on a stage | Damage 0, Heal 1, IncreaseAttribute 2, DecreaseAttribute 3, SetAttribute 4, OwnedEffect 5 |
| `intent` | Harmful 0, Beneficial 1 |
| `intent` in `areaSkills2D` | Beneficial 0, Harmful 1, Neutral 2 |
| `obstacle` / `target` in `delivery2D.profile` | Stop 0, PassThrough 1 |

The remaining scalar fields match the public types in `Ability.h` and
`Derived.h`. Missing optional fields use their public defaults. The loader
validates schema version, catalog references, parameter units, stage order,
effect references, 2D profile references and bounded stage/emitter counts.
Each `delivery2D` binding uses `{ "action": id, "stage": id, "profile": { ... } }`.
`velocity` and `spawnOffset` are three-number arrays with zero Z; `radius`,
`layerMask`, `reverseAfterTicks`, `ignoreSource`, `obstacle`, and `target` are
optional profile fields. `installAbility2D(world, ability, manager, document)`
checks that every authored stage has a profile and installs them together. The
simple entry `installAbility2D(world, manager, document, effects, selections)`
preflights the document, effects, stage profiles and dynamic selections, then
installs Ability plus its Stats/Combat dependencies and 2D adapter in one call.
For a headless document with effects, use `installAbility(world, document, effects)`.
An `areaSkills2D` entry declares an active Area `skill`, an `effect` reference,
source-following `radius` and XY `offset`, `layerMask`, `includeSource`, Combat
`intent` and `allowed` target bitmask, and optional `requiredTag`, candidate/member
limits and `requireTargetAlive`. The effect reference must be listed in
`effectReferences` and supplied before 2D installation. For dynamic
selection, set `requireSelected: true` and a nonempty `selectionKey`, then pass
`AreaSelectionBindings2D` to `installAbility2D`. Every named predicate must be
present at install time and remain valid while its areas are active. Geometry,
selection, and Combat permission are rechecked on each fixed Sim sweep; gameplay
only calls `castArea(actor, skill)`. Invalid area bindings or missing effect and
selection references reject installation before changing the 2D adapter.
Obstacles are physics bodies without a configured Combat target. Source-body
exclusion is physical; Combat still decides whether any other entity may be
affected. A root stage released at tick N is first queried on the next World
fixed update, so a periodic stage needs `lifetimeTicks` greater than the number
of desired future query ticks.
