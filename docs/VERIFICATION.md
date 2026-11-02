# Verification

Results are updated by the developer after running the checks. Targets below are not claims that all hardware scenarios have passed.

## Local results — 2 November 2026

- The current revision passes four headless suites: core, gesture/contact, platform/settings, and DirectWrite UI layout. No windows, overlays, input hooks, clicks, or sound playback are started by these suites.
- 4,820 core assertions and 136 gesture/contact regression checks passed. Strong strokes are replayed at 60–240 Hz in both modes. Clicks still require actual forward, outward tip motion; crossings alone cannot generate a click.
- A medium stroke that previously crossed the trigger threshold is now rejected. Peak and sustained mouse-speed thresholds are 3000 and 2500 px/s respectively.
- Segment self-collision was removed. Weak particle-clustering pressure and elastic recovery remain. The F8 simulation source was compared with the previous revision and is unchanged.
- Seven paragraphs of the new EN/RU Manual fit without scrolling at 600, 720, 900, and 1080 DIP of available height. Inline emphasis is measured with the same DirectWrite ranges used for painting; literal Markdown markers are removed.
- No visual or subjective animation review was performed for this revision, as requested. The earlier 0.810 ms smoke frame p95 and desktop lifecycle results describe the previous revision, not the current animation.

Earlier revision checks covered the second-monitor overlay, isolated native click coordinates and pointer restoration, tray/Manual activation, and separate playback of all three embedded samples. They have not been repeated on the desktop for this revision.

These checks do not establish acoustic synchronization, clean-machine compatibility, hot-unplug behavior, or the full lifecycle matrix below.

## Automated checks

- Fast, slow, jittering, and held-button motion histories.
- Stable XPBD simulation, handle/tip constraints, and identical relative physics in both control modes.
- Physical-pixel mapping with negative virtual origins and mixed monitor layouts.
- Valid, truncated, unsupported, and mutated WAV buffers.
- First-run defaults, EN/RU and mode persistence, adjacent INI priority, and read-only settings.
- Continuous, finite pause/unpause geometry and a cursor-following roll with two pinned points and free mass motion.
- Full EN/RU Manual layout fitting, inline emphasis/bold notes, and settings control hit areas.
- Repeated notification-area activation events keep the panel visible; opening and closing Manual preserves the UI pause.
- A no-input graphics/audio smoke run. It never injects clicks or hides the system cursor.

## Hardware acceptance targets

| Check | Target | Status |
|---|---|---|
| Stop recognition to injected click | ≤ 80 ms | Pending real gesture recording |
| Audible attack vs visual contact, wired output | ≤ 20 ms | Pending loopback/video |
| Settled CPU, one-minute average | ≤ 1% | Pending measurement |
| Active frame preparation at 144 Hz | p95 ≤ 2 ms | Previous revision smoke p95 0.810 ms; current live workload pending |
| GPU and memory | Record idle/active values | Pending measurement |
| Multi-monitor/mixed DPI | Correct clipping, position, and click | Pending hardware test |
| Audio hot-unplug/default-device change | Recovery without restarting app | Pending hardware test |
| Full-screen/Alt+Tab/manual pause | No strikes while paused | Pending interactive test |
| Explorer restart/sleep/lock/forced exit | Tray and cursor recover | Pending interactive test |
| Clean Windows 11 | Single EXE starts without dependencies | Pending clean-machine test |

Do not replace pending hardware results with passing claims based only on unit tests.
