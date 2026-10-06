# Accessibility

LiveSqueeze is meant to be used by people who rely on it to hear dialogue, some of whom also rely
on a keyboard, a screen reader, large text or a high contrast theme. This page says what the
window does for them, and, as important, what has and has not been checked.

## What the window does

**Keyboard.** Every control can be reached with Tab and operated from the keyboard.

| Keys | Does |
|---|---|
| Ctrl+B (Command+B on a Mac) | Bypass on/off (plays the plain stereo downmix, for comparison) |
| Ctrl+P | Processing on/off (starts or stops LiveSqueeze) |
| Ctrl+1, Ctrl+2, Ctrl+3 | Simple, Advanced and Devices pages |
| Alt+B, Alt+P, Alt+S, Alt+A, Alt+D | The same, through the underlined letters (Windows and Linux only; macOS has no Alt mnemonics) |
| Left / Right, Page Up / Page Down, Home / End | Move a slider by one step, ten steps, to either end |
| Type a number, Enter | Set a value exactly (every slider has a number box) |

**Screen readers.** Every control has an accessible name and, where it helps, a description (what
the setting does and its range). The meters and the compression curve are custom drawn, so they
carry their own name and a description with the current value:

- the level meters publish their number about **once a second**, not on every reading, so a screen
  reader is not flooded;
- the gain meter says it in words ("turned down 6.2 dB, lifted 3.1 dB");
- the curve describes itself ("Sounds louder than -24 dB are turned down at 4 to 1 ...") and
  changes only when a setting changes.

The status line at the top says what LiveSqueeze is doing, in words, and a status icon in front of
it repeats it as a shape.

**Not colour alone.** The tray icon has a different outline for each state: circle (processing),
ring (bypassed), triangle (problem), square (stopped), diamond (starting). The status line starts
with the matching symbol. Meters always show their number. The gain meter's "turned down" and
"lifted" bars differ by side and by hatching. The curve uses line styles (solid, dashed, dotted),
not just colours.

**Large text and themes.** All sizes are derived from the font, so the window grows with the
system's text size, labels wrap instead of being cut off, and the Advanced page scrolls. All
colours come from the system palette (so high contrast and dark themes apply); nothing is hard
coded except the tray icon artwork, which has a dark outline and works on light and dark panels.

**No tray?** Some desktops (GNOME without an extension) have no system tray. LiveSqueeze then
works as an ordinary window: it stays open, closing it quits, and `--minimized` is ignored.

**Messages.** When audio is lost or comes back, and the window is hidden, the tray shows a
notification. The text is in the Diagnostics output as well.

## What has been checked, and how

The tests in `tests/app` run with Qt's `offscreen` platform and a simulated audio backend, on
every platform in CI:

- every interactive control on every page has a non-empty accessible name (as Qt's accessibility
  tree reports it) and every parameter row has a description;
- the Tab chain is closed, reaches every control on each page and visits them top to bottom;
- the Ctrl shortcuts, the Alt mnemonics (not on macOS), arrow and Page keys and typing into a number box all work;
- the meters do not change their accessible description between announcements;
- the window is drawn at normal and at **double** text size, in a light and a dark palette, on all
  three pages, saved as pictures (`build/tests/app/screenshots/`) and checked for text that is cut
  off.

## What has not been checked

- **Real screen readers.** NVDA, JAWS, VoiceOver and Orca were not run. The tests check what Qt
  exposes to them, not what they say. Qt's accessibility support on each platform is the layer in
  between. If you use one, please report how it sounds.
- **The tray on real desktops.** Behaviour of the tray icon and its notifications on Windows 11,
  macOS, GNOME, KDE and others is untested here.
- **High contrast modes** of the operating systems (Windows "contrast themes", macOS "increase
  contrast"). The window follows the palette it is given; the tray icon art is fixed.
- **Voice control and switch access.**

## Reporting problems

Use Devices > Diagnostics to copy the details of your setup, and say which assistive technology
you use. Accessibility problems are treated as bugs, not as feature requests.
