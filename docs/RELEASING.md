# Release

1. Review the author-provided English/Russian Manual text.
2. Run `./tools/build.ps1` with the complete local sound pack.
3. Complete the hardware acceptance checks in VERIFICATION.md.
4. Optionally sign `dist/Whip.exe` using a trusted Authenticode identity and SHA-256 timestamping. Never store certificates or passwords in Git.
5. After signing, regenerate `dist/SHA256SUMS` with `Get-FileHash -Algorithm SHA256`.
6. Publish the single executable and its hash. Separate audio sources, prepared WAVs, debug symbols, and development tools are not part of the portable app.

Code signing identifies the publisher; a new signed build may still have no SmartScreen reputation. No certificate purchase or signing identity is configured by this project.
