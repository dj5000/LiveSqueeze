# Platform guide

LiveSqueeze sits between your applications and your speakers. How the "in between" device is provided depends on the
operating system.

| Platform | Virtual device | Created by |
|---|---|---|
| Linux (PipeWire) | A sink named **LiveSqueeze** | LiveSqueeze itself |
| Windows | A virtual cable such as **VB-Cable** | You install it once |
| macOS | **BlackHole** (use the 16ch build for surround) | You install it once |

On Windows and macOS a custom virtual device of our own would need driver signing, so version 1 uses a well-known
existing virtual cable. A device of our own is a later goal.

## Linux (PipeWire)

Requires PipeWire with a session manager (WirePlumber), which is the default on current Fedora, Ubuntu, Debian,
Arch and openSUSE. PulseAudio-only systems are not supported yet.

1. Start LiveSqueeze (the tray app `livesqueeze`, or the headless `lsq-run`). It creates a virtual output called
   **LiveSqueeze** and plays the processed sound on your real speakers.
2. In your sound settings (or in your video player's audio output setting) choose **LiveSqueeze** as the output.
   To use it for everything, set it as the default output.
3. When LiveSqueeze is not running, the LiveSqueeze output disappears. Applications that follow the default output
   are normally moved back to your speakers by PipeWire's session manager; an application that was pinned to the
   LiveSqueeze output may need to be switched back by hand. (This is the behaviour to expect from WirePlumber, but it
   has not yet been checked on a real desktop; see the hardware checklist.)

Notes:

- By default the sink has 5.1 channels. Use `lsq-run --sink-layout 7.1` for 7.1 sources (some games and Blu-rays).
  Stereo sources work with either.
- LiveSqueeze always plays to a real device; it will not choose a virtual one as its output.
- Both of LiveSqueeze's streams run in the same PipeWire graph cycle, so there is no clock drift between them.
- The added delay is about 33 ms in the default "safe" setting of `lsq-run` and about 26 ms in "balanced",
  on top of PipeWire's own buffering. Players with an audio-delay setting can compensate for lip-sync.

Check that it works without any hardware or desktop: `scripts/e2e/run_pw_e2e.sh build` starts its own headless
PipeWire and verifies the virtual sink, the channel mapping for 5.1 and 7.1, the compression and a soak run.
It needs `pipewire`, `wireplumber`, `pw-cat`, `pw-record` and `dbus-run-session`. On a machine whose timers are
woken up late (virtual machines, busy CI runners) a few of its measurements are disturbed by the player and recorder
rather than by LiveSqueeze; the script says so when it happens instead of failing.

If you hear clicks or dropouts, run `lsq-run --trace trace.csv`, reproduce the problem, stop it with Ctrl-C and attach
`trace.csv` to the report: it shows whether the audio source stopped delivering or LiveSqueeze ran dry.

## Windows

> **Not yet run on real Windows audio hardware.** The Windows build is compiled and unit-tested in CI (with simulated
> and software-timed devices), and the steps below are written from how WASAPI and VB-Cable are documented to work, not
> from having used them together. Please use [HARDWARE_TEST_CHECKLIST.md](HARDWARE_TEST_CHECKLIST.md) and report what
> you find.

Windows has no way to create a virtual audio device without a signed kernel driver, so LiveSqueeze uses an existing
**virtual cable**: [VB-Cable](https://vb-audio.com/Cable/) (free for personal use; it is not included with LiveSqueeze
and is installed separately). Anything played to the cable's "input" side comes out of its "output" side, where
LiveSqueeze reads it, processes it and plays it on your speakers through WASAPI (shared mode, at the device's own
format; LiveSqueeze converts rate and channels itself).

1. Install VB-Cable and restart Windows. A playback device **CABLE Input** and a recording device **CABLE Output**
   appear.
2. Sound settings > More sound settings > Playback > **CABLE Input** > Properties:
   - Levels: set the volume to **100**. A lower volume would make quiet dialogue quieter *before* it reaches the
     compressor, pushing it towards the noise floor. Use LiveSqueeze's own output volume instead.
   - Advanced: choose a sample rate (48000 Hz is a good choice; LiveSqueeze converts whatever it finds).
   - For surround films: Configure > choose **5.1** (or 7.1). If the cable only offers stereo on your system,
     LiveSqueeze still compresses and limits; the downmix is then simply a pass-through of the two channels.
3. Start LiveSqueeze. On the **Devices** page choose **Input: CABLE Output** and **Output: your speakers or headphones**
   (not "CABLE"; LiveSqueeze refuses to play into a virtual cable because that would feed back into its own input).
4. Make the cable the output of your player, or of the whole system (Sound settings > Output). The player's own volume
   and the system volume still apply before the cable.
5. Do not enable "Listen to this device" on CABLE Output; it would play the unprocessed sound as well.

Notes:

- LiveSqueeze needs no administrator rights. "Start LiveSqueeze when I log in" writes a per-user entry
  (`HKCU\...\Run`).
- The program is DPI-aware (per monitor) and works with the system text size, high-contrast themes and the
  Windows magnifier; screen readers are untested (see [ACCESSIBILITY.md](ACCESSIBILITY.md)).
- An application using WASAPI exclusive mode on the real speakers blocks LiveSqueeze from playing, and the status
  line says it could not open the output.
- If you plug in a different output device, LiveSqueeze follows within a few seconds when "Automatic" is chosen.

## macOS

> **Not yet run on real macOS audio hardware.** The macOS build is compiled and unit-tested in CI (Apple Silicon
> runner), but the steps below have not been tried with BlackHole by the author. Please use
> [HARDWARE_TEST_CHECKLIST.md](HARDWARE_TEST_CHECKLIST.md) and report what you find.

macOS has no virtual output device built in, so LiveSqueeze reads from an existing one:
[BlackHole](https://github.com/ExistentialAudio/BlackHole) (free, installed separately). Use the **16ch** version for
5.1 and 7.1 films; the 2ch version is enough for stereo.

1. Install BlackHole 16ch and restart the Mac if the installer asks.
2. Audio MIDI Setup > **BlackHole 16ch**: set the format to **48 000 Hz** and, for surround films, use *Configure
   Speakers* to pick 5.1 (channels 1 to 6: left, right, centre, subwoofer, left surround, right surround; this is the
   order macOS and most players use). BlackHole has no volume control; that is expected, and it means the volume of
   the processed sound is **LiveSqueeze's output volume** (Simple page).
3. Start LiveSqueeze. macOS asks once whether LiveSqueeze may use the microphone: allow it. (LiveSqueeze does not
   record anything; macOS counts reading a virtual cable as audio input.)
4. On the **Devices** page choose **Input: BlackHole 16ch** and **Output: your speakers or headphones**. A 16-channel
   device cannot say which speakers its channels are, so also set **Channel layout of the input** to **5.1** (or 7.1).
5. Make BlackHole the output of your player, or of the whole Mac (System Settings > Sound > Output).

Notes:

- The app bundle is signed ad hoc, which is enough to run it on the Mac that built it. A copy downloaded from somewhere
  else is quarantined by Gatekeeper: right-click > Open once, or remove the quarantine flag. Proper signing and
  notarisation belong to packaging, which is not done yet.
- "Start LiveSqueeze when I log in" installs a per-user LaunchAgent.
- From macOS 14.4 Core Audio can tap the system's output directly, which would remove the need for BlackHole. That is
  a planned improvement, not part of this version.
- Apple Silicon and Intel: configure with `-DLSQ_UNIVERSAL=ON` for a binary that runs on both.
