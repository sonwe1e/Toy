# Project Schema v3

> **Status: historical migration input.** VCStation 1.2.0 writes Schema v4. See
> [project-schema-v4.md](project-schema-v4.md) for the active contract.

`.dvsproj` files are UTF-8 JSON documents with `schemaVersion: 3`, written by
`dvs::persistence::ProjectJson`. Schema-2 documents are migrated with strict-index alignment and
the default side-by-side view. Schema-1 and future versions are rejected.

A document carries the project identity, 2–3 comparison sources, and review state:

```json
{
  "schemaVersion": 3,
  "project": { "id": "project-1", "displayName": "Session" },
  "sources": [
    {
      "id": 0,
      "role": "reference",
      "displayName": "reference.mp4",
      "path": "sources/reference.mp4",
      "identity": {
        "byteSize": 1048576,
        "modifiedUtcMilliseconds": 1753000000000,
        "fingerprintSha256": "<64 hex>"
      },
      "descriptor": {
        "extent": { "width": 1920, "height": 1080 },
        "frameRate": { "numerator": 30, "denominator": 1 },
        "frameCount": { "value": 90, "origin": "reported" },
        "durationMicroseconds": 3000000,
        "codecId": "h264",
        "pixelFormatId": "yuv420p",
        "bitDepth": 8,
        "rotationDegrees": 0,
        "sampleAspectRatio": { "numerator": 1, "denominator": 1 },
        "color": {
          "matrix": "bt709",
          "range": "limited",
          "transfer": "bt709",
          "matrixInferred": false,
          "transferInferred": false
        },
        "decodeCapabilities": { "softwareDecode": true, "d3d11VaDecode": false },
        "timingConfidence": "declared-cfr"
      }
    },
    { "id": 1, "role": "prediction", "displayName": "model-a.mp4", "...": "..." }
  ],
  "referenceSourceId": 0,
  "marks": { "inFrame": 1, "outFrame": 89 },
  "lastDisplayedFrame": 0,
  "workspace": { "inspector-visible": "true" },
  "alignment": {
    "mode": "manual-anchors",
    "offsets": [{ "sourceId": 1, "frames": 1 }],
    "anchors": [
      { "sourceId": 1, "canonicalFrame": 40, "sourceFrame": 41 }
    ],
    "analysisCacheKey": null
  },
  "view": {
    "layout": "reference-focus",
    "differenceEdge": [0, 1],
    "differenceMetric": "heatmap",
    "gain": 4
  }
}
```

Rules enforced at decode:

- `sources` must contain 2–3 entries with unique `id` values; each descriptor must be
  individually valid, and at most one source may carry `role: "reference"`.
- `referenceSourceId` is `null` or must name one of the entries; when null, the
  canonical source is the first entry in load order.
- Entries are re-validated through `ComparisonValidator`, so a loaded project shares
  the exact admissibility rules of a freshly opened session. Cross-source
  compatibility findings (counts, rates, durations, resolutions, color metadata) are
  re-derived at session open rather than persisted.
- `frameRate` is `null` for VFR sources; `frameCount.origin` is `reported`,
  `estimated`, or `indexed`; `timingConfidence` distinguishes declared and verified
  CFR from VFR.
- `rotationDegrees` is one of 0, 90, 180, or 270. `sampleAspectRatio` must be
  positive. Color transfer is `bt709`, `srgb`, or `linear`; older schema-2/3 documents
  lacking the new optional metadata migrate to rotation 0, SAR 1:1, and inferred BT.709.
- Alignment modes are `strict-index`, `global-offsets`, `manual-anchors`, and
  `automatic-sequence`. Offsets and anchors are source-ID based and must stay valid and monotone
  for the current canonical timeline.
- Full sequence maps are never embedded in the project. Automatic sequence alignment stores only
  `analysisCacheKey`; the repository loads a separate derived cache only when its source
  fingerprints, frame counts, algorithm version, and content-derived key all match.
- View layouts are `side-by-side`, `three-up`, `reference-focus`, and `difference`;
  difference metrics include `rgb-absolute`, `luma`, `chroma`, `heatmap`, and
  `exact-planes`; gains are limited to 1, 2, 4, 8, or 16.

Project, settings, and derived-cache files are written through temporary files, flushed, and
atomically published. A missing or invalid derived cache degrades the project to dirty
strict-index state instead of trusting stale mappings.
