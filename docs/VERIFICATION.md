# Verification

Results are updated by the developer after running the checks. Targets below are not claims that all hardware scenarios have passed.

## Local results — 1 November 2026

- Release x64 compiled with MSVC 19.51, static CRT; 4,676 core assertions and settings integration passed.
- The two-second graphics/audio smoke test passed on the second monitor, with the 960-pixel lash and XAudio2 initialized. Latest frame preparation p95: 0.844 ms; zero injected clicks in this test.
- The isolated native click test passed on the non-primary monitor: exact client coordinates and pointer restoration verified. It sends only after its own test window has foreground focus.
- The Fluent-style panel was visually inspected on monitor two. EN/RU switching, localized labels, and language persistence were checked in the running app.
- The author-provided English Manual was inspected in the running app, including its scrollable text and dark colors.

These checks do not establish acoustic synchronization, clean-machine compatibility, hot-unplug behavior, or the full lifecycle matrix below.

## Automated checks

- Fast, slow, jittering, and held-button motion histories.
- Stable XPBD simulation, handle/tip constraints, and non-mutating forecast.
- Physical-pixel mapping with negative virtual origins and mixed monitor layouts.
- Valid, truncated, unsupported, and mutated WAV buffers.
- First-run defaults, EN/RU and mode persistence, adjacent INI priority, and read-only settings.
- A no-input graphics/audio smoke run. It never injects clicks or hides the system cursor.

## Hardware acceptance targets

| Check | Target | Status |
|---|---|---|
| Stop recognition to injected click | ≤ 80 ms | Pending real gesture recording |
| Audible attack vs visual contact, wired output | ≤ 20 ms | Pending loopback/video |
| Settled CPU, one-minute average | ≤ 1% | Pending measurement |
| Active frame preparation at 144 Hz | p95 ≤ 2 ms | Smoke p95 0.844 ms; 144 Hz live workload pending |
| GPU and memory | Record idle/active values | Pending measurement |
| Multi-monitor/mixed DPI | Correct clipping, position, and click | Pending hardware test |
| Audio hot-unplug/default-device change | Recovery without restarting app | Pending hardware test |
| Full-screen/Alt+Tab/manual pause | No strikes while paused | Pending interactive test |
| Explorer restart/sleep/lock/forced exit | Tray and cursor recover | Pending interactive test |
| Clean Windows 11 | Single EXE starts without dependencies | Pending clean-machine test |

Do not replace pending hardware results with passing claims based only on unit tests.
