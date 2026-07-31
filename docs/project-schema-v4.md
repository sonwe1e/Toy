# Project Schema v4

VCStation 1.2.0 writes Schema v4 transactionally. Readers accept v2 and v3 as migration inputs,
materialize the new view defaults, validate the complete domain model, and only then publish a
project snapshot. Future schema versions fail closed.

---

## Source and layout contract

`sources` contains one to three unique, fully validated media descriptors. A single-source project
must use layout `single` and must store `differenceEdge` as `null`. Multi-source projects may use
`side-by-side`, `three-up`, `reference-focus`, `difference`, `analysis-grid`, or `wipe`;
`three-up` and `analysis-grid` require three sources. Pairwise layouts require a valid two-source
`differenceEdge`.

The `view` object persists:

- `layout`, `differenceEdge`, `differenceMetric`, `differenceFilter`, and gain;
- the surface-normalized `wipePosition`;
- threshold enabled state and normalized threshold value;
- viewport center and scale;
- an optional normalized ROI rectangle.

All finite numeric fields are range checked. Invalid layout/source combinations, duplicate IDs,
unknown enum strings, reversed ROI bounds, and non-finite transforms reject the document without
partially mutating the active workspace.

---

## Migration behavior

Schema v3 documents retain their original layout, difference edge, metric, and gain. The v4-only
fields receive deterministic defaults: bilinear filtering, wipe position `0.5`, disabled threshold,
centered 1× viewport, and no ROI. Schema v2 first receives the existing v3 alignment/view defaults
and then the same v4 additions.

Paths remain project-relative when possible and external absolute paths remain absolute. Source
fingerprints, last displayed frame, alignment offsets, automatic-analysis identity, and manual
anchors keep their existing validation rules.
