# Windows Whip

A small Windows 11 app that turns a mouse gesture into a moving whip and a click.

**Development build.** Physics, a transparent overlay, sound, and two languages are included. Hardware acceptance results are listed in [verification](docs/VERIFICATION.md).

## Use

Run `Whip.exe`. No installer, downloads, or administrator access are needed.

- Move quickly, then stop sharply to strike. One stroke makes one left click.
- **Sadomaso**: move the handle and learn to aim the free tip. This is the first-run mode.
- **Exact**: the tip is guided to the point where the mouse stopped.
- Click the tray icon to change the mode, choose **EN / RU**, or open **Manual**.
- The target icon opens [**Training**](https://test-whip.vercel.app/) in your default browser.
- Right-click the tray icon for **Pause / Resume** and **Exit**.
- **F8** coils the whip into a resting circle and pauses. Press it again to unfold and resume. **Escape** exits and is still passed to the active app.
- Mouse buttons, scrolling, and dragging keep working. Holding a mouse button prevents automatic strikes.

The app pauses while its dark Fluent-style panel or Manual is open, while Windows is locked, and while the active window is full-screen. A normal maximized window does not count as full-screen. A manual pause remains in effect after leaving full-screen. The lash is 960 physical pixels long, with a tapered double contour and a distinct wrapped grip.

Both flyouts use a portrait layout, vector icons, and Segoe UI Variable typography. Manual shows the complete text at once, without scrolling. Its measured layout adapts to the monitor's work area. See [UI design](docs/UI_DESIGN.md).

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
./tools/build.ps1
```

The result is `dist/Whip.exe` with all three edited sounds embedded. `dist/SHA256SUMS` contains its SHA-256. No FFmpeg, Python, external WAV, or extra DLL is shipped.

For a code-only CI build:

```powershell
./tools/build.ps1 -Preset ci
```

The CI executable has no sound pack and is not a release build. Original audio files and separate prepared WAVs are local build inputs and are not committed. See [audio credits](AUDIO_CREDITS.md).

## Development

- `src/`: simulation, gesture detection, Windows integration, vector rendering, audio.
- `resources/`: executable resources, manifest, and Manual text.
- `tools/`: reproducible audio preparation and build scripts.
- `tests/`: motion scenarios, physics, pause animation, WAV parsing, coordinate mapping, settings, and EN/RU text layout.
- `.agents/skills/`: project-local CMake and MSVC instructions.

`Whip.exe --preview` opens a persistent test panel on the second (non-primary) monitor without hiding the cursor, installing input hooks, or injecting clicks. `--preview-lash` shows only the animated lash preview. `Whip.exe --smoke-test` runs a two-second graphics/audio check on that monitor with no hooks or clicks, then writes `smoke-report.json` next to that test executable. On a single-monitor computer these modes fall back to its only screen.

`Whip.exe --animation-test` checks the coil, resting state, and unfold cycle on the same test monitor, without hooks or clicks. It writes a separate `animation-report.json`.

The author-provided Manual is kept in `resources/manual_en.txt` and `resources/manual_ru.txt` and embedded in the executable.

## Compatibility

Use on the normal Windows desktop. Windows can block clicks into elevated windows, secure desktops, or protected apps. A program may also ignore simulated input.

Full-screen auto-pause removes the low-level hooks, restores the cursor, and disables clicks and sound. Escape remains observable through Raw Input. Auto-pause is not an anti-cheat compatibility guarantee: close Windows Whip before running a protected game when in doubt. No anti-cheat bypass is included.

The cursor guard restores the system pointer if the app exits unexpectedly or becomes unresponsive. A second app instance opens the existing tray panel.

Unsigned downloads may trigger SmartScreen or Smart App Control. A valid Authenticode signature can help establish publisher identity, but does not guarantee immediate reputation. Signing is optional during development; [release instructions](docs/RELEASING.md) cover signing and hashes.

## License

Application code: [MIT](LICENSE). Audio: Pixabay Content License, separately credited. The two vendored development skills retain their upstream license; see [third-party notices](THIRD_PARTY_NOTICES.md).
