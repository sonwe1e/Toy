# Project Schema v2

`.dvsproj` files are UTF-8 JSON documents with `schemaVersion: 2`, written by
`dvs::persistence::ProjectJson`. Schema-1 documents (the legacy fixed A/B shape) are
rejected at load with `unsupported-project-schema`; they are not migrated.

A document carries the project identity, 2–3 comparison sources, and review state:

```json
{
  "schemaVersion": 2,
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
        "color": { "matrix": "bt709", "range": "limited", "matrixInferred": false },
        "decodeCapabilities": { "softwareDecode": true, "d3d11VaDecode": false },
        "timingConfidence": "declared-cfr"
      }
    },
    { "id": 1, "role": "prediction", "displayName": "model-a.mp4", "...": "..." }
  ],
  "referenceSourceId": 0,
  "marks": { "inFrame": 1, "outFrame": 89 },
  "lastDisplayedFrame": 0,
  "workspace": { "inspector-visible": "true" }
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

Unknown schema versions (including future ones) are rejected with
`unsupported-project-schema`.
