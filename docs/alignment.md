# Time Alignment

Status: **target design** (USERPLAN phase 4). Not implemented yet; the current build
maps frame *i* of source A to frame *i* of source B and refuses to open sources whose
frame counts differ.

Two comparison modes make sure automatic alignment can never mask inference errors:

- **Strict Index Mode** (default, for model-output review): canonical frame `i` maps
  to frame `i` of every source. A source without that frame shows `Missing frame` —
  no interpolation, no last-frame repetition, no silent trimming. Frame-count
  disagreement is itself a finding worth seeing.
- **Aligned Capture Mode** (for screen recordings, re-encodes, shifted videos):
  allows global offsets, extra/missing frames at the ends or in the middle, VFR, and
  manual anchors. Every automatic mapping states its status, e.g.
  `Prediction 2: Auto-aligned, offset +1, one missing frame at 824`.

## Level 1 — Metadata and strict index check (already available)

Opening a video produces the display-order frame count, a per-frame PTS index,
CFR/VFR classification, start time, frame-rate candidates, and
resolution/rotation/SAR/color metadata. The existing `MediaProbe` + `FrameTimeline`
machinery does this and normalizes VFR timelines to the first frame — it is kept.

Refinement: probing scans all timestamps today, so opening three long videos is
O(total frames). Plan: fast metadata probe first, first frame shown early when CFR
metadata is trustworthy, full index built in the background with per-source progress
on the source cards; a full index stays mandatory for VFR, unknown counts, or suspect
timestamps.

## Level 2 — Global offset estimation

For videos that differ by one or two frames or are shifted as a whole, search a small
offset range (≈ [−16, 16]) minimizing

    E(δ) = median over sampled frames S of D(R_i, P_{i+δ})

with images reduced to low-resolution luma and the distance mixing structure, edges,
and perceptual hash terms:

    D = α(1 − SSIM) + β‖∇Y_R − ∇Y_P‖₁ + γ·D_pHash

The median keeps a single broken interpolated frame or scene cut from dominating.
Auto-apply only when the best offset beats the runner-up by a clear margin; otherwise
ask the user.

## Level 3 — Dropped and duplicated frame detection

When no single offset explains the video, banded dynamic programming builds a
monotone mapping C(i, j) with transitions for match, reference-missing
(i−1, j), and prediction-missing/duplicate (i, j−1). Real cases differ by one or two
frames, so the search stays inside a band of width W ≈ 8–16 around the estimated
offset — O(N·W), not O(N²). The resulting `AlignmentMap` records exactly which
canonical frame maps to which source frame, and where frames are missing or repeated.

## Level 4 — Manual anchors

Automatic alignment is always overridable with anchors such as
`Reference frame 824 ↔ Prediction 2 frame 825`; between anchors the mapping is
piecewise monotone. The timeline shows the global offset, drop points, duplicate
points, manual anchors, and low-confidence regions — strictly more useful for
model-output review than a bare ± time-shift control.
