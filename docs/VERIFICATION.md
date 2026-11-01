# Verification

Results are updated by the developer after running the checks. Targets below are not claims that all hardware scenarios have passed.

## Local results — 2 November 2026

- Release x64 compiled with MSVC 19.51, static CRT; 4,821 core assertions and settings integration passed.
- All seven CTest suites passed in the release build and the code-only CI build: core, gesture/contact, platform, UI layout, graphics/audio smoke, pause animation, and tray lifecycle.
- The two-second graphics/audio smoke test passed on the second monitor, with the 768-pixel lash and XAudio2 initialized. Latest frame preparation p95: 0.810 ms; zero injected clicks in this test.
- The isolated native click test passed on the non-primary monitor: exact client coordinates and pointer restoration verified. It sends only after its own test window has foreground focus.
- The redesigned portrait panel was visually inspected on monitor two. EN/RU switching and localized labels were checked in the running app. Language persistence was verified in settings integration.
- The simplified Russian Manual was visually inspected on monitor two, with larger uniform text and bold final notes. It showed the complete text without scrolling, native title bars, quote bars, or a standard text field. DirectWrite layout checks also passed at 600, 720, 900, and 1080 DIP of available height.
- F8 was exercised in the safe preview. The resting circular coil was visually inspected on monitor two. The automated pause cycle rendered the transition and the moving roll, then resumed; zero clicks were injected. Core checks verify both cursor pins and free sag/inertia. The paused rendering branch contains no gesture recognition or input injection.

- The three embedded sounds were auditioned separately. XAudio2 recorded one submission and observed sample advancement for each buffer: impact and both alternating whooshes. This verifies buffer playback, not acoustic synchronization.
- The A/B/C vector comparison was exported with the app renderer. Selected C now uses a three-pixel grid and an integral handle 35% wider than the cable.

- 131 gesture/contact regression checks passed. Twenty representative strong strokes at 60, 90, 120, 144, and 240 Hz each produced one physical contact in Exact and Sadomaso. Weak strokes, folded/trailing tips, jitter, teleports, reused gesture IDs, held buttons, and stale candidates produced none. Slow motion with 16–96 ms delivery batches also produced no gestures. The visible contact point matched the click point.

These checks do not establish acoustic synchronization, clean-machine compatibility, hot-unplug behavior, or the full lifecycle matrix below.

## Automated checks

- Fast, slow, jittering, and held-button motion histories.
- Stable XPBD simulation, handle/tip constraints, and identical relative physics in both control modes.
- Physical-pixel mapping with negative virtual origins and mixed monitor layouts.
- Valid, truncated, unsupported, and mutated WAV buffers.
- First-run defaults, EN/RU and mode persistence, adjacent INI priority, and read-only settings.
- Continuous, finite pause/unpause geometry and a cursor-following roll with two pinned points and free mass motion.
- Full EN/RU Manual layout fitting, uniform body/bold notes, and settings control hit areas.
- Repeated notification-area activation events keep the panel visible; opening and closing Manual preserves the UI pause.
- A no-input graphics/audio smoke run. It never injects clicks or hides the system cursor.

## Hardware acceptance targets

| Check | Target | Status |
|---|---|---|
| Stop recognition to injected click | ≤ 80 ms | Pending real gesture recording |
| Audible attack vs visual contact, wired output | ≤ 20 ms | Pending loopback/video |
| Settled CPU, one-minute average | ≤ 1% | Pending measurement |
| Active frame preparation at 144 Hz | p95 ≤ 2 ms | Smoke p95 0.810 ms; 144 Hz live workload pending |
| GPU and memory | Record idle/active values | Pending measurement |
| Multi-monitor/mixed DPI | Correct clipping, position, and click | Pending hardware test |
| Audio hot-unplug/default-device change | Recovery without restarting app | Pending hardware test |
| Full-screen/Alt+Tab/manual pause | No strikes while paused | Pending interactive test |
| Explorer restart/sleep/lock/forced exit | Tray and cursor recover | Pending interactive test |
| Clean Windows 11 | Single EXE starts without dependencies | Pending clean-machine test |

Do not replace pending hardware results with passing claims based only on unit tests.
