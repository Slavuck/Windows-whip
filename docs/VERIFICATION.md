# Verification

Results are updated by the developer after running the checks. Targets below are not claims that all hardware scenarios have passed.

## Local results — 2 November 2026

Thickness/mass follow-up: all six headless suites and the synthetic replay pass again with doubled silhouette widths and particle masses. The rebuilt EXE includes all three sounds. The original passive-model figures below remain historical comparisons; the newer replay and hashes are in [heavy.csv](physics/heavy.csv) and [heavy-sources.json](physics/heavy-sources.json). No desktop animation review was performed.

- The Release build passes six headless suites: core, gesture/contact, numerical physics, constraint energy, platform/settings, and DirectWrite UI layout. No windows, overlays, input hooks, clicks, or sound playback are started by these suites. The executable was rebuilt with all three embedded sounds.
- 4,856 core assertions, 216 gesture/contact checks, and 230 algebraic constraint checks passed. Strong 480/640 px strokes are replayed at 60–240 Hz in both modes. Clicks require actual forward, outward tip motion; recognition alone cannot create contact.
- The author's 49-bar, 960 Hz model replaces the earlier six-link solver. Its mass taper, gravity and air drag remain. Artificial lateral impulses and powered cursor-heading wrist targets are removed; a softer neck, mass-aware implicit angular damping and final velocity projection are added. Details, primary sources, measurements and limitations are in [physics](PHYSICS.md).
- Eleven numerical replay rows cover rest, slow motion, drift, jitter, a small pulse, a smooth strong stroke, the exact 260 px / 100 ms reference input, and 60/90/144/240 Hz or variable frame timing. Fixed-960-Hz relative tail-joint HF RMS falls by 65.6% for the smooth stroke and 16.9% for the reference stroke; forward reach also becomes shorter. CSV data and source hashes are preserved in [physics/](physics/).
- Peak and sustained recognition thresholds stay at 3000 and 2500 px/s. The user's 180 ms physical contact window is preserved. Horizontal 480 px / 120 ms strokes contact in about 117–133 ms after confirmation; 640 px strokes take about 58–67 ms. The original blanket 80 ms target is not met by the smaller stroke. A recognised 320 px stroke that remains folded produces no click.
- There is no self-collision force. The author's F8 rigid ring/pendulum simulation was compared byte-for-byte as text with the supplied source region and is unchanged.
- Local numerical runs use MSVC 19.51 x64 with C++20 and Release optimisation. No Linux distribution is installed locally. The workflow also runs the same portable suites on Ubuntu 24.04 with GCC 13 and exports replay CSVs; its per-commit results are available in [GitHub Actions](https://github.com/Slavuck/Windows-whip/actions).
- Seven paragraphs of the new EN/RU Manual fit without scrolling at 600, 720, 900, and 1080 DIP of available height. Inline emphasis is measured with the same DirectWrite ranges used for painting; literal Markdown markers are removed.
- No visual or subjective animation review was performed for this revision, as requested. The earlier 0.810 ms smoke frame p95 and desktop lifecycle results describe the previous revision, not the current animation.

Earlier revision checks covered the second-monitor overlay, isolated native click coordinates and pointer restoration, tray/Manual activation, and separate playback of all three embedded samples. They have not been repeated on the desktop for this revision.

These checks do not establish acoustic synchronization, clean-machine compatibility, hot-unplug behavior, or the full lifecycle matrix below.

## Automated checks

- Fast, slow, jittering, and held-button motion histories.
- Inextensible 49-bar simulation, passive constraint projection, handle/tip constraints, deterministic input replay, and identical relative physics in both control modes.
- Physical-pixel mapping with negative virtual origins and mixed monitor layouts.
- Valid, truncated, unsupported, and mutated WAV buffers.
- First-run defaults, EN/RU and mode persistence, adjacent INI priority, and read-only settings.
- Continuous, finite pause/unpause geometry and a cursor-following rigid pendulum roll.
- Full EN/RU Manual layout fitting, inline emphasis/bold notes, and settings control hit areas.
- Repeated notification-area activation events keep the panel visible; opening and closing Manual preserves the UI pause.
- A no-input graphics/audio smoke run. It never injects clicks or hides the system cursor.

## Hardware acceptance targets

| Check | Target | Status |
|---|---|---|
| Stop recognition to injected click | Original target ≤ 80 ms | Not met for all synthetic strokes: 480 px needs 117–133 ms; current physical gate permits 180 ms. Desktop recording pending. |
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
