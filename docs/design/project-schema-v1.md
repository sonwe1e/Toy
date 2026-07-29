# Project Schema v1

> **Status: historical.** The `sources: { "a": {}, "b": {} }` shape predates the
> 2–3 source generalization planned in [../../USERPLAN.md](../../USERPLAN.md); the
> schema will gain a revision when Phase 2 lands.

`.dvsproj` files are UTF-8 JSON documents with `schemaVersion: 1`. They are written by
`dvs::persistence::ProjectJson`; repository code supplies the destination path so source paths
can be portable without storing the project path itself.

## Top-Level Shape

```json
{
  "schemaVersion": 1,
  "project": { "id": "uuid", "displayName": "Review" },
  "sources": { "a": {}, "b": {} },
  "canonicalTimeline": { "frameRate": { "numerator": 30000, "denominator": 1001 }, "frameCount": 120 },
  "clips": [], "exports": [],
  "marks": { "inFrame": null, "outFrame": null },
  "lastDisplayedFrame": 0,
  "workspace": {}
}
```

Each source contains `path`, `identity`, and `descriptor`. `path` is relative when it lies under
the `.dvsproj` directory and absolute otherwise. `identity` has non-zero `byteSize`, signed UTC
milliseconds, and a 64-character SHA-256 fingerprint of the whole file (at most 2 MiB) or its
first and last 1 MiB. `descriptor` stores extent, rational rate, per-source frame-count origin,
duration in microseconds, codec/pixel-format IDs, bit depth, normalized `color` metadata,
decode capabilities, and timing confidence. `color` records `bt601` or `bt709`, full or limited
range, and whether the matrix was inferred because the media declared none. Older schema-1 files
without `color` remain readable: the decoder infers BT.709 at heights of 720 or greater and
BT.601 below that, with limited range.

`canonicalTimeline` duplicates the validated pair rate and effective frame count. A decoder
rejects a mismatch rather than trusting either copy. Clips use inclusive `inFrame` and `outFrame`.
Exports store stable English state IDs, output reference, and an optional full error payload.
`Running` exports are restored as `Interrupted` by the domain aggregate.

## Validation and Compatibility

Schema versions other than 1, absent source identities, malformed numeric values, invalid stable
IDs, invalid workspace values, and invalid source-pair/project invariants fail with stable project
persistence errors. Workspace is a flat object of string values; unknown keys are preserved.
Project JSON does not verify file existence or fingerprints. Repository/FingerprintService owns
that I/O check. A structurally valid document still loads when source A or B is missing or has a
changed fingerprint: `ProjectLoaded` carries ordered A/B diagnostics and is followed by
`RequestSucceeded`, so the project remains editable. Other revalidation I/O failures and all
schema/aggregate failures remain request failures.

## Explicit Relink

`ProjectRelinkRequest` accepts only a user-confirmed replacement for source A or B. The project
repository performs its hashing on its bounded serial I/O actor and emits
`SourceRelinkPrepared` before its successful terminal event. The event contains only a
`SourceRelinkCandidate`: its A/B role, normalized absolute path, and complete identity. It never
contains a rebuilt project or claims the replacement is compatible media. M3 must freshly probe
the candidate, construct a new `ValidatedSourcePair`, and call `Project::replaceSources`; that
operation validates the existing editable state without applying persistence-only running-export
normalization.
