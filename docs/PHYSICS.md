# Physics changes and numerical evidence

## Thickness and mass update

The entire silhouette is now twice as wide, including the outline and rigid handle; length and handle-to-cable width ratio stay the same. Drawing buffers use width-dependent padding to avoid clipping the thicker grip. All particle masses are doubled while bending stiffness stays fixed. Hand and wrist frequencies are divided by sqrt(2), giving a slower inertial response. They are now 155.56 and 5.66 rad/s respectively. Internal damping remains mass-aware. All six local headless suites and the numerical replay pass. F8 uses the same doubled silhouette.

The detailed before/after measurements below and `physics/current.csv` record the preceding passive-wrist revision, before this thickness/mass update. The latest heavier-model replay is [heavy.csv](physics/heavy.csv), with source hashes in [heavy-sources.json](physics/heavy-sources.json). Subjective perceived weight has not been measured.

The [author-supplied 49-bar model](https://github.com/Slavuck/Windows-whip/commit/e15996b8e68d8d3ec348ee3633d44ac0e131a2d6) is the baseline for this change. The older six-link XPBD implementation is not restored. Its coarse links and driven handle could resemble a rod after a stop. The author's replacement introduced a much more useful rolling loop: a direct inextensible chain, strong mass taper, mass-dependent bending, directional air drag, and a wrist that can yield to the lash. These are retained.

`compare.png` is a visual reference, not measured coordinate data. The numerical replay reproduces its stated input: 260 physical pixels right over 100 ms, starting at 0.60 s and stopping at 0.70 s. It exports the same eight sample times. The comparison baseline is the author's saved source, not the old top row in that image.

## Causes of unwanted motion

1. **The velocity constraint solver added a sideways kick.** After the tridiagonal length-velocity projection, compressed bars received an extra transverse impulse. This was not a constraint reaction or a force derived from elastic energy. With a stationary root, a reproducible test increased kinetic energy from 242,876,225.763 to 242,960,912.338. Removing only that kick gives 242,875,982.887. The constraint projection now only removes forbidden relative velocity.
2. **The wrist was a powered actuator.** Above 1,400 px/s its target instantly changed to the velocity heading. A 70 rad/s spring then turned the handle toward that target; after a delay it also moved the target downward. Moving a spring's equilibrium can do work even after the hand stops. The wrist now has a fixed downward rest angle and an 8 rad/s spring. It can yield and dissipate motion, but its equilibrium does not chase the cursor heading or move on an idle timer.
3. **The position correction lacked its final velocity projection.** Solving lengths changes the constraint directions. Projecting velocity again at the corrected positions removes remaining axial motion before the next step. This follows the position/velocity constraint separation of [Andersen's RATTLE algorithm](https://doi.org/10.1016/0021-9991(83)90014-1); the application is a damped game simulation, not a claim of a complete symplectic RATTLE integrator.
4. **The light tip had relatively weak internal damping.** A constant damping time did not give a constant damping ratio as mass and stiffness changed. Internal angular damping is now scaled by effective joint inertia and becomes stronger toward the tip. Air drag is unchanged.

Fast tips can still legitimately exceed hand speed: the taper concentrates energy, and gravity and stored elastic energy also contribute. Equalising tip speed to mouse speed would destroy this effect. The fix removes artificial input of energy instead.

## Formulas and retained structure

There are 49 rigid bars, including a 67.584 px handle, integrated at 960 Hz. The tail has 48 equal bars. Total rest length remains 768 px. Relative density is `0.04 + 0.96*(1-s)^2.4`, and the original lumped masses and tridiagonal solver are preserved.

The hand uses the exact critically damped update for a held substep target. With `a = hand - target`, `b = velocity + omega*a`, and `omega = 220 rad/s`:

```text
hand'     = target + (a + h*b)*exp(-omega*h)
velocity' = (velocity - h*omega*b)*exp(-omega*h)
```

The target is interpolated over each display frame as before. No derivative of a quantised cursor position is added as an extra force.

The neck immediately after the handle is softer over approximately one handle length. Its bending multiplier is `0.25 + 0.75*u*u*(3-2*u)`, where `u` is distance from the handle end divided by handle length, clamped to [0,1]. Beyond it, the original density-squared bending profile applies. This is an explicit material profile, not a correction to force a particular tip position.

Each bend has angle `theta`, gradient `J`, effective inverse inertia `W = J*M^-1*J^T`, and stiffness `k = EI/dualLength`. Its dashpot is `c = 2*zeta*sqrt(k/W)`. The damping ratio rises smoothly from 0.45 at the neck to 1.10 at the tip. The local linearised backward-Euler impulse is:

```text
lambda = -h * (k*theta + (c + h*k)*angularRate)
         / (1 + h*(c + h*k)*W)
velocity += M^-1 * J^T * lambda
```

This is a derived scalar spring/dashpot update, using the effective-mass and implicit damping approach explained by [Erin Catto](https://box2d.org/files/ErinCatto_SoftConstraints_GDC2011.pdf) and the stability motivation for implicit methods in [Baraff and Witkin](https://www.cs.cmu.edu/~baraff/papers/sig98.pdf). Their code is not imported. It is a local sequential approximation, not a globally implicit discrete elastic rod solver. The damping ratios and neck multiplier are application tuning, not measured leather properties.

The F8 rigid ring/pendulum code is unchanged from the author's version. The tail can cross itself; no self-collision repulsion, target-radius force, or prescribed tip trajectory has been added.

## Measured comparison

Local Release results use MSVC 19.51, C++20, and identical synthetic cursor histories. Full rows are in [baseline.csv](physics/baseline.csv) and [current.csv](physics/current.csv). These are numerical diagnostics, not a visual rating or a hardware latency test.

The primary jitter diagnostic measures **relative angular rates of the last eight joints**, sampled at every 960 Hz integration tick. This excludes rigid rotation of the whole loop. Its high-frequency component is the residual of a one-pole 12 Hz low-pass filter. The late interval starts 0.75 s after the input stops and ends with the scenario.

| Input | Late joint HF RMS, before / after (rad/s) | Reduction |
|---|---:|---:|
| Constant 450 px/s drift, then stop | 2.785 / 0.708 | 74.6% |
| Smooth 320 px / 120 ms stroke | 8.877 / 3.052 | 65.6% |
| Reference 260 px / 100 ms stroke | 12.793 / 10.631 | 16.9% |

At 144 display updates/s, late high-pass tip velocity falls by 35.6%, 39.7%, and 30.3% respectively. This separate display-sampled metric can alias angular motion and should not be used alone to tune the solver.

The change has a tradeoff: the smooth stroke's maximum forward reach falls from 701 to 449 px and peak tip speed from 24,145 to 15,276 px/s. For the reference input those values fall from 636 to 354 px and from 19,203 to 8,393 px/s. Body rise above the hand remains 657 px and 522 px respectively. The reference loop develops later; the weak input no longer gets the old powered wrist turn. A stronger input is needed for a quick, extended contact.

Across the replay scenarios, bar-length error stays below 1.3e-12 relative. The stationary pointer does not excite the hanging chain. Repeated identical runs agree exactly on this compiler; a 0.01 px input perturbation changes the tip by at most 0.038 px in the short comparison interval. This does not imply long-term trajectories are identical across compilers or display rates.

Algebraic tests separately check constraint passivity, mass orthogonality, velocity residuals, idempotence, and change-of-frame invariance. They include the energy-injecting regression above. Increasing the integration rate alone to 1,920 or 3,840 Hz did not monotonically improve late tip motion, so the production rate remains 960 Hz.

## Clicks and timing

Recognition still requires a strong directed gesture: 3,000 px/s peak and 2,500 px/s sustained mouse speed. A recognised stop only arms contact. It does not guarantee a click: the real simulated tip must extend to at least 80% of rest length, project at least 30% ahead along the stroke, move forward at least 2,200 px/s, and move outward.

The author's 180 ms contact window is preserved. Smooth 480 px / 120 ms horizontal strokes contact about 117–133 ms after stop confirmation, while 640 px / 120 ms strokes contact in about 58–67 ms in the local synthetic tests. Both modes give at most one hit. A 320 px / 120 ms gesture is recognised but does not extend far enough in time, so it is correctly skipped. These fixtures do **not** establish the original 80 ms acceptance target. Forcing every smaller gesture into that deadline would require changing geometry, input requirements, or contact rules.

## Reproduce without a desktop

```sh
CXX=g++-13 python3 tools/test_physics.py --label current --check
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release -DWHIP_WITH_AUDIO=OFF -DCMAKE_CXX_COMPILER=g++-13
cmake --build build/linux --parallel 2
ctest --test-dir build/linux --output-on-failure
```

On Windows, `python tools/test_physics.py --label current --check` finds the installed MSVC toolchain without changing global settings. `./tools/build.ps1 -SkipAudio -Headless` also rebuilds the embedded-audio EXE if the WAV pack is already prepared. These checks do not launch Whip, move the desktop pointer, inject clicks, play sound, or install hooks.

Each replay report includes an immutable source snapshot, source/harness SHA-256 hashes, metrics, frame traces, and reference poses/curves. Save a source directory with `core.cpp` and `core.hpp`, then use `--source-dir <directory> --label baseline` to compare a different model. Hashes for this recorded pair are in [sources.json](physics/sources.json).

Optional static comparison, using matplotlib only for plotting:

```sh
python tools/plot_physics.py build/physics-headless/baseline build/physics-headless/current
```

No Linux distribution is installed locally. The same portable tests are configured for Ubuntu 24.04 with GCC 13 in GitHub Actions. Local console runs and remote Linux checks are recorded separately in [verification](VERIFICATION.md). Hardware feel, audio sync, and frame preparation time remain separate acceptance work.
