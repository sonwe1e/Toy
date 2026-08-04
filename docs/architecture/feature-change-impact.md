# Feature change impact

Use this map to keep future feature work inside the smallest stable capability boundary.

| Feature | Primary implementation boundary | Presentation boundary | Runtime impact |
| --- | --- | --- | --- |
| Playback speed | Playback workflow and cadence | Playback capability | Add a clock policy with audio |
| Compare mode | Contract descriptor table and renderer | Comparison capability | None |
| Audio | Playback clock port | Playback capability | Owned audio service and shutdown step |
| Evidence export | Export use case and output port | Notification capability | Adapter owned by composition |
| Alignment algorithm | Alignment workflow and analysis port | Alignment capability | New worker only if required |
| Renderer | Render-channel port and render bridge | No UI-model dependency | Replace graphics composition |
| Setting | Typed value and repository mapping | Owning capability only | None |
| Media format | FFmpeg probe/decode/cache internals | Media information only | None |

Cross-cutting changes must retain complete FrameSet publication and the full
session/epoch/generation/request identity. A feature must not add another coordinator event loop
or let a UI model query an adapter worker directly.
