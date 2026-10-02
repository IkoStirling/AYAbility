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
