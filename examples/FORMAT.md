# Ability authoring format, version 1

`contact2d.ability.json` is an executable sample: `Test_Physics2D.cpp` loads it,
installs Ability in a World, creates a real Box2D body, and verifies a contact
changes HP through Combat and Stats. Parse with `loadAuthoring`, optionally edit
the returned `AuthoringDocument`, then install with `installAbility(world, document)`.
`saveAuthoring` writes a normalized version of the same data. A failed load,
compile or save does not replace an existing World catalog.

Top-level arrays are `actions`, `equipment`, `skills`, `fireDeliveries`,
`derivedSkills`, `delivery2D`, and `effectReferences`. Each stage binding has `owner` and
`definition`. For fire, owner is an equipment configuration ID; for Derived it
is an active skill ID. Every stage ID is unique within its definition. Stage
`predecessor` must name an earlier stage; `trigger` chooses release (0), first
confirmed hit (1), or predecessor finish (2). Delays and lifetimes use fixed Sim
ticks. The `effectReferences` array lists Combat effect IDs used by `OwnedEffect`
stages; bind actual `EffectDefinition` values with `configureOwnedEffect`.

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
| `obstacle` / `target` in `delivery2D.profile` | Stop 0, PassThrough 1 |

The remaining scalar fields match the public types in `Ability.h` and
`Derived.h`. Missing optional fields use their public defaults. The loader
validates schema version, catalog references, parameter units, stage order,
effect references, 2D profile references and bounded stage/emitter counts.
Each `delivery2D` binding uses `{ "action": id, "stage": id, "profile": { ... } }`.
`velocity` and `spawnOffset` are three-number arrays with zero Z; `radius`,
`layerMask`, `reverseAfterTicks`, `ignoreSource`, `obstacle`, and `target` are
optional profile fields. `installAbility2D(world, ability, manager, document)`
checks that every authored stage has a profile and installs them together.
Obstacles are physics bodies without a configured Combat target. Source-body
exclusion is physical; Combat still decides whether any other entity may be
affected. A root stage released at tick N is first queried on the next World
fixed update, so a periodic stage needs `lifetimeTicks` greater than the number
of desired future query ticks.
