# Architecture

The UI thread owns the per-monitor DirectComposition overlays, the settings panel, the Manual, and the 480 Hz fixed-step XPBD state. The lash has 64 segments of 15 physical pixels (960 pixels in total). The active frame timer runs approximately every 7 ms; simulation accumulates fixed steps independently of rendering. A settled whip stops scheduling frames. Each monitor has a premultiplied BGRA swap chain starting at 512 × 512, growing in 64-pixel increments to enclose the whip, rather than a full-desktop buffer. The panel uses live vector controls, a dark blue gradient, native rounded corners, and a blue accent inspired by Fluent flyouts.

The input thread owns WH_MOUSE_LL and WH_KEYBOARD_LL and performs only atomic state updates and message posting. Injected events and the application's marker are ignored for recognition and passed on normally. Physical coordinates are used throughout. A full-screen pause removes both hooks; background Raw Input observes Escape and F8 without suppressing them.

A gesture requires a fast stroke, minimum travel, and a confirmed stop. Swing and stop events have a unique gesture ID. An impact forecast simulates a copy of the rope for up to 71 ms, leaving a 32–68 ms scheduling window and space under the 80 ms stop-to-click target. Renewed motion or a held button cancels a pending strike. The actual tip position is used for Sadomaso; Exact pins the tip to the captured target.

Audio runs on a separate COM/MTA thread with immutable PCM buffers and 24 preallocated voices. OnCriticalError only signals an event. The audio thread recreates the engine and voices, with a two-second retry interval when no output exists. No missed impacts are replayed. Windows' virtual audio client follows the default output device; a device notification also requests recovery. Stopping unused audio processing reduces idle load.

Sound is queued before the predicted visual/click deadline using XAudio2's approximate driver latency plus a processing quantum. This is an estimate, not a measured acoustic onset; hardware acceptance requires loopback/camera measurements. Cancellation after the device has buffered audio cannot retract already played sound.

The main process sends a heartbeat to a guard instance of the same executable. The guard restores cursor visibility on process death or a four-second unresponsive interval. Session, power, display, and shell restarts have explicit restoration paths. The cursor scheme is not replaced.

The UI and settings use an English/Russian enum. English is the default. Mode, language, and portable-file precedence are independent of manual, UI, session, and full-screen pause reasons.
