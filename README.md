# Windows Whip

A small Windows 11 app that turns a mouse gesture into a moving whip and a click.

**Development build.** Physics, a transparent overlay, sound, and two languages are included. Hardware acceptance results are listed in [verification](docs/VERIFICATION.md).

## Use

Run `Whip.exe`. No installer, downloads, or administrator access are needed.

- Make a strong directed stroke, then stop sharply. A click occurs when the moving tip flies ahead and the cable unfolds. One stroke makes at most one left click. Gentle motion and residual rope oscillation do not strike.
- **Sadomaso**: move the handle and learn to aim the free tip. This is the first-run mode.
- **Exact**: the tip is guided to the point where the mouse stopped.
- Click the tray icon to change the mode, choose **EN / RU**, or open **Manual**.
- The target icon opens [**Training**](https://test-whip.vercel.app/) in your default browser.
- Right-click the tray icon for **Pause / Resume** and **Exit**.
- **F8** rolls the whip and pauses strikes. The roll follows the mouse as a rigid ring swinging from the handle like a pendulum. Press F8 again to unfold and resume. **Escape** exits and is still passed to the active app.
- Mouse buttons, scrolling, and dragging keep working. Holding a mouse button prevents automatic strikes.

The app pauses while its dark Fluent-style panel or Manual is open, while Windows is locked, and while the active window is full-screen. A normal maximized window does not count as full-screen. A manual pause remains in effect after leaving full-screen. The lash is 768 physical pixels long, with a nearly uniform cable, a 67.6-pixel rigid handle widened a further 15% (55.25% wider than the cable), and a clean white outline. The selected C design uses a three-pixel grid with no grip rings, seams, or wraps.

The mode names sit inside one green segmented capsule. The enlarged Training target occupies the right side of the header. Both flyouts use a minimal portrait layout and Segoe UI Variable typography. Manual uses one larger body style, with bold mode names and hotkeys; the final two notes are bold. Its Russian title is «Мануал». Manual shows the complete text at once, without scrolling. Its measured layout adapts to the monitor's work area. See [UI design](docs/UI_DESIGN.md).

The free cable can cross itself. Its 49 rigid bars have a heavy belly, a much lighter tip, and a soft neck just after the handle. Length constraints and elastic bending produce the rolling loop and forward crack. Internal damping reduces rapid tip oscillations. The whole silhouette is twice as thick, and particle masses are doubled, with slower hand and wrist response for a heavier feel. The user's rigid-ring F8 simulation is preserved. See [physics](docs/PHYSICS.md) for the model and comparison method.

A click waits for physical contact. The current contact window is 180 ms after a confirmed stop; rollout can take roughly 100–180 ms. The previous 80 ms deadline is not a promise for this model. A folded or trailing tip still produces no click.

## Settings

If `Whip.ini` is next to the executable at startup, it is used for portable settings. Otherwise settings go to `%LOCALAPPDATA%\WindowsWhip\Whip.ini`. The selected location does not change during a session. A read-only settings file affects saving only.

```ini
[Whip]
Mode=Sadomaso
Language=EN
```

## Build

Install Visual Studio's **Desktop development with C++** workload, including MSVC x64, Windows SDK, CMake, and Ninja. Python 3 and FFmpeg are only needed to prepare the local sound pack.

Place one impact MP3 with `удар` in its name and two swing MP3s with `взмах` in their names in `Audio/`. The originals are preserved. Then run from PowerShell:

```powershell
./tools/build.ps1 -Headless
```

The result is `dist/Whip.exe` with all three edited sounds embedded. `dist/SHA256SUMS` contains its SHA-256. No FFmpeg, Python, external WAV, or extra DLL is shipped.

For a code-only CI build:

```powershell
./tools/build.ps1 -Preset ci -Headless
```

`-Headless` runs the core, gesture/contact, numerical physics, constraint energy, settings, and DirectWrite layout checks. It opens no windows and does not play sounds or install input hooks. Graphical diagnostics remain available separately.

The CI executable has no sound pack and is not a release build. Original audio files and separate prepared WAVs are local build inputs and are not committed. See [audio credits](AUDIO_CREDITS.md).

The physics core is portable C++20. To compile it and replay synthetic mouse paths without starting the application:

```sh
python tools/test_physics.py --check
```

The script uses MSVC on Windows or a GNU/Clang C++20 compiler on Linux. It writes source hashes, numerical metrics, and motion traces under `build/physics-headless/`. To select GCC 13 on Linux, use `CXX=g++-13 python3 tools/test_physics.py --check`.

For a Linux CMake build with GCC 13 or another C++20 compiler:

```sh
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release -DWHIP_WITH_AUDIO=OFF
cmake --build build/linux
ctest --test-dir build/linux --output-on-failure
```

These Linux targets contain no Win32 overlay or audio engine. Numerical checks cover stability and response; the user judges animation feel. Current measurements and their limits are recorded in [verification](docs/VERIFICATION.md).

## Development

- `src/`: simulation, gesture detection, Windows integration, vector rendering, audio.
- `resources/`: executable resources, manifest, and Manual text.
- `tools/`: reproducible audio preparation and build scripts.
- `tests/`: motion scenarios, physics, pause animation, WAV parsing, coordinate mapping, settings, and EN/RU text layout.
- `.agents/skills/`: project-local CMake and MSVC instructions.

`Whip.exe --preview` opens a persistent test panel on the second (non-primary) monitor without hiding the cursor, installing input hooks, or injecting clicks. `--preview-lash` shows only the animated lash preview. `Whip.exe --smoke-test` runs a two-second graphics/audio check on that monitor with no hooks or clicks, then writes `smoke-report.json` next to that test executable. On a single-monitor computer these modes fall back to its only screen.

`Whip.exe --animation-test` checks the coil, moving paused state, and unfold cycle on the same test monitor, without hooks or clicks. It writes a separate `animation-report.json`.

`whip_motion_tests.exe` checks strong and weak motion, delayed pointer delivery, and the physical contact gate in Exact and Sadomaso at different update rates. `whip_motion_tests.exe --report` prints measured contact reach and delay as CSV. The portable physics replay adds synthetic trajectories and numerical diagnostics. These tests never send desktop input.

`Whip.exe --tray-test` checks repeated tray notifications, panel visibility, and the Manual handoff on the test monitor, using the production activation code and no input hooks or clicks.

`Whip.exe --audio-test` plays both whooshes and the impact separately, then writes loaded/submitted/played buffer counts to `audio-report.json`. It requires the embedded sound pack and an audio output.

`Whip.exe --design-preview` opens a safe A/B/C comparison on the test monitor. Space plays or freezes the identical movement applied to each profile. The selected C profile is the default in the app. `--design-snapshot` exports `Whip-variants.png` next to the EXE without opening a window.

The author-provided Manual is kept in `resources/manual_en.txt` and `resources/manual_ru.txt` and embedded in the executable.

## Compatibility

Use on the normal Windows desktop. Windows can block clicks into elevated windows, secure desktops, or protected apps. A program may also ignore simulated input.

Full-screen auto-pause removes the low-level hooks, restores the cursor, and disables clicks and sound. Escape remains observable through Raw Input. Auto-pause is not an anti-cheat compatibility guarantee: close Windows Whip before running a protected game when in doubt. No anti-cheat bypass is included.

The cursor guard restores the system pointer if the app exits unexpectedly or becomes unresponsive. A second app instance opens the existing tray panel.

Unsigned downloads may trigger SmartScreen or Smart App Control. A valid Authenticode signature can help establish publisher identity, but does not guarantee immediate reputation. Signing is optional during development; [release instructions](docs/RELEASING.md) cover signing and hashes.

## License

Application code: [MIT](LICENSE). Audio: Pixabay Content License, separately credited. The two vendored development skills retain their upstream license; see [third-party notices](THIRD_PARTY_NOTICES.md).
