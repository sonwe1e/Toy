# ADR 0001: Canonical presentation contract

Status: accepted

## Decision

Comparison modes, difference configuration, viewport state, and mode capabilities live in the
framework-neutral `dvs_presentation_contract` target. Numeric enum values remain compatible with
the existing QML and settings representation.

## Consequences

- Renderer and UI capability logic consume the same definitions.
- Adding a mode requires one descriptor plus its implementation and tests, not parallel enums.
- Qt-specific exposure is an adapter concern and may not redefine presentation semantics.
