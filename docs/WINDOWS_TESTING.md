# Testing LiveSqueeze on Windows

About 20 minutes, using only the mouse (or keyboard) and the LiveSqueeze window. No command line is needed.

## 1. One-time setup (5 minutes)

1. **Get LiveSqueeze.** Signed in to GitHub, open the repository's *Actions* tab, choose the latest run for the
   branch being tested, scroll to *Artifacts* and download **LiveSqueeze-windows-app**. Unzip it anywhere, for example
   to your Desktop. Inside the `LiveSqueeze` folder, `livesqueeze.exe` is the app.
2. **Install VB-Cable** from https://vb-audio.com/Cable/ (free). Run its installer as administrator, then restart
   Windows. Two new devices appear: *CABLE Input* (a speaker) and *CABLE Output* (a microphone).
3. **Set up the cable once:** right-click the speaker icon in the taskbar → *Sound settings* → *More sound settings*.
   On the *Playback* tab, double-click **CABLE Input**:
   - *Levels* tab: set it to **100**.
   - *Advanced* tab: choose **48000 Hz**.
   - Optional, for 5.1 films: back on the Playback tab, select CABLE Input → *Configure* → **5.1 Surround**.

## 2. Start it

1. Double-click **livesqueeze.exe**. Windows may say it protected your PC, because the app is not signed yet. Click
   *More info* → *Run anyway*.
2. In LiveSqueeze, open the **Devices** page:
   - *Input (virtual cable)*: **CABLE Output**.
   - *Output (speakers or headphones)*: your real speakers or headphones (not CABLE).
3. In Windows *Sound settings*, set the output to **CABLE Input**. From now on everything you play goes through
   LiveSqueeze.
4. The status at the bottom of the window should say **Running**, and the *In* and *Out* level meters should move when
   something plays.

**To stop at any time:** set the Windows output back to your speakers, then quit LiveSqueeze from its tray icon
(next to the clock) → *Quit LiveSqueeze*.

## 3. Does it sound right? (10 minutes)

On the **Simple** page, choose the preset **Movie / Night** and play a film or a TV episode.

- [ ] Dialogue is clear and comfortable.
- [ ] Whispers and quiet speech are easier to hear than before.
- [ ] Explosions and loud music are clearly tamed, with no crackling or distortion.
- [ ] Move the **Strength** slider from *Gentle* to *Strong*. The difference between loud and quiet gets smaller.
- [ ] Click **Bypass** (or use the tray menu) on and off. You hear the difference, and switching makes no click.
- [ ] Ten minutes pass with no clicks, pops or dropouts. The status line counts *Dropouts*. It should stay at 0 or 1.
- [ ] The sound stays in sync with the picture.

## 4. The fixes in this version (5 minutes)

**Settings survive a reconnect.**
1. On the **Advanced** page, lower **Output volume** a lot, for example to -20 dB.
2. While something is playing, unplug your headphones or USB speakers and plug them back in. With built-in speakers,
   change their sample rate in *More sound settings* → *Advanced* instead.
3. The status shows *Reconnecting*, then *Running*.
- [ ] The sound comes back at the **same low volume**, not suddenly louder.
- [ ] Bypass and the preset are also as you left them.

**No click when the limiter tightens.**
1. On the **Simple** page, move **Strength** all the way down. That is the *Limiter only* preset.
2. Play loud music.
3. Pick the preset **Late night**. Or, on the **Advanced** page, drag **Ceiling** lower.
- [ ] There is no click or crackle at the moment of the change.

## 5. If something is wrong

Open **Diagnostics…**, click **Copy to clipboard**, and paste the text into your bug report. Say what you were doing
when it happened. LiveSqueeze can also be run in the background: tick *Start hidden in the tray* and *Start LiveSqueeze
when I log in*, if you like it.

## What this does not check

A corrupt sample stopping the sound (the third fix) cannot be triggered by hand. The automated tests cover it.
Screen readers have not been tested with LiveSqueeze. If you use one, please tell us what works and what does not.
