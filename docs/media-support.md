# Media Support

Status: **current state + staged plan** (USERPLAN phase 6 for the expansion work).

## What opens today

The probe (`src/media_ffmpeg/src/MediaProbe.cpp`) accepts exactly:

| Dimension | Accepted | Rejected with |
|---|---|---|
| Codec | H.264, HEVC, MPEG-4 Part 2 (and a decoder must exist) | `kUnsupportedCodec` (L304-318) |
| Pixel format | 8-bit YUV 4:2:0 only | `kUnsupportedPixelFormat` (L195-206, L323-330) |
| Transfer | SDR only — PQ (SMPTE2084) and HLG are rejected | `kUnsupportedPixelFormat` (L160-166) |
| Color matrix | BT.709, BT.601, Unspecified (inferred from height) | rejected (L169-188) |
| Decode | Software only — `DecodeCapabilities::d3d11VaDecode` is hardcoded false (L549) | — |

`SoftwareDecoder` re-verifies the 8-bit 4:2:0 constraint on every decoded frame
(`SoftwareDecoder.cpp` L84-97, L440-446) to catch format changes between probe and
decode. Containers are whatever FFmpeg opens — MP4/MOV/MKV/AVI all work when the
stream inside passes the gates above. VFR, non-zero start PTS, and B-frame display
reordering are handled by the display-order PTS index.

"Encoding format is not fixed" cannot be solved by widening a file-extension filter;
the gates above are the contract, and widening them is staged.

## Capability layering (target)

Input support is split into three independently judged capabilities, replacing the
single whitelist:

1. **Container and decoder capability** — can FFmpeg demux and decode it at all.
2. **Pixel-format normalization capability** — can output be converted to the
   internal analysis formats (NV12 8-bit, P010 10-bit, and BGRA/RGBA for display):

   ```text
   Decoder output
     ├── NV12 8-bit
     ├── P010 10-bit
     ├── YUV422 / YUV444
     └── BGRA/RGBA
             ↓
   Normalized GPU frame
   ```

3. **Render and difference capability** — can color space, bit depth, rotation, and
   SAR be interpreted correctly for comparison and diff display.

A decoder succeeding at level 1 must not be read as a promise that the pair is
comparable; levels 2 and 3 decide that, and any resampling or color conversion is
stated in the UI (`Resampled comparison — not pixel-exact`).

## Stages

- **Stage 1 — correctness first (phase 6a):** keep software decode; formally cover
  H.264/HEVC/MPEG-4 Part 2, 8-bit YUV420, CFR/VFR, non-zero start PTS, B-frame
  ordering, MP4/MOV/MKV/AVI, rotation and SAR metadata, and differing resolutions.
- **Stage 2 — compatibility (phase 6b):** the normalization layer above (P010/10-bit,
  4:2:2/4:4:4, BGRA/RGBA), with decoder and renderer capabilities judged separately.
- **Stage 3 — hardware decode (phase 6c):** FFmpeg D3D11VA with one decode context
  per source sharing a single D3D11 device, NV12/P010 textures going straight to the
  renderer (no GPU→CPU→GPU), software fallback with the fallback state visible in the
  UI.

## Optional compatibility proxies

Proxies must never be a precondition for playback (DualVideoTool's forced-proxy chain
is retired under `legacy/`). A proxy may be generated only when the original cannot
seek stably, when a format cannot enter the render pipeline directly, or when the
user explicitly asks for a fast preview cache.
