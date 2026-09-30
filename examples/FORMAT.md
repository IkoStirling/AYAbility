# Ability authoring format, version 1

`contact2d.ability.json` is an executable sample: `Test_Physics2D.cpp` loads it,
installs Ability in a World, creates a real Box2D body, and verifies a contact
changes HP through Combat and Stats. Parse with `loadAuthoring`, optionally edit
the returned `AuthoringDocument`, then install with `installAbility(world, document)`.
`saveAuthoring` writes a normalized version of the same data. A failed load,
compile or save does not replace an existing World catalog.

Top-level arrays are `actions`, `equipment`, `skills`, `fireDeliveries`,
`derivedSkills`, and `effectReferences`. Each delivery binding has `owner` and
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

The remaining scalar fields match the public types in `Ability.h` and
`Derived.h`. Missing optional fields use their public defaults. The loader
validates schema version, catalog references, parameter units, stage order,
effect references and bounded stage/emitter counts before installation. 2D
velocity, radius, spawn offset and physics layer are composition settings on
`Ability2DDeliverySystem`; they are not authoritative ability definitions.
