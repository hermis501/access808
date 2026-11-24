# Access808

An accessible 808 bass synthesizer (VST3 on Windows and macOS, plus AU on
macOS) for hip-hop, trap and drill, built for blind producers using REAPER with
OSARA. It is a synthesized, monophonic 808 with legato glide that needs no
sighted help: bundled REAPER scripts write the note overlaps that make the
slides.

## Download

Get the latest version from the
[Releases page](https://github.com/hermis501/access808/releases/latest):

- Windows: `Access808-<version>-Windows-Setup.exe`
- macOS: `Access808-<version>-macOS.pkg`
- REAPER scripts on their own (for macOS, or for a manual install):
  `Access808-<version>-REAPER-scripts.zip`

The zip files with the bare VST3 and AU bundles are for installing by hand.

Requirements: Windows 10 or 11 (64-bit), or macOS 11 or later (Apple Silicon
or Intel), and a host that loads VST3 or AU instruments. REAPER and OSARA are
recommended but not required.

## Installing on Windows

Run `Access808-<version>-Windows-Setup.exe`. It installs the plugin to
`C:\Program Files\Common Files\VST3` and the manual to the Start menu.

The installer is not code-signed yet, so Windows may show "Windows protected
your PC". Activate "More info", then the "Run anyway" button.

The checkbox "Install the REAPER slide scripts and add them to the REAPER
Actions list" is on by default. On the next page, confirm the REAPER resource
folder; the default is right for a normal REAPER install, and for a portable
REAPER you choose its folder. Close REAPER before installing, so the actions
can be added to the Actions list, in both the main section and the MIDI editor
section. If REAPER is open, Setup asks you to close it, or to copy the scripts
only.

Uninstalling removes the plugin, the scripts and their actions. Your user
presets are kept.

To install by hand instead:

1. Copy `Access808.vst3` into `C:\Program Files\Common Files\VST3`.
2. Copy the files from the `ReaScripts` folder into
   `%APPDATA%\REAPER\Scripts\Access808`, keeping all four files together.
3. In REAPER's Actions list, choose New action, then Load ReaScript, and load
   the three files whose names start with "Access808 - ".

## Installing on macOS

Run `Access808-<version>-macOS.pkg`. It installs the VST3 to
`/Library/Audio/Plug-Ins/VST3` and the AU to
`/Library/Audio/Plug-Ins/Components`.

The package is not signed with an Apple Developer ID yet, so macOS may refuse
to open it at first. Open System Settings, Privacy and Security, find the
message about Access808 near the end, and choose "Open Anyway".

The macOS package does not install the REAPER scripts. To add them:

1. Unzip `Access808-<version>-REAPER-scripts.zip`.
2. Copy the four files into
   `~/Library/Application Support/REAPER/Scripts/Access808`, keeping them
   together.
3. In REAPER's Actions list, choose New action, then Load ReaScript, and load
   the three files whose names start with "Access808 - ". Load them in the
   MIDI editor section of the Actions list too, if you want them there.

## Making slides

Glide Mode is set to "Legato (overlap)" by default. If a new note starts while
the previous note is still held, the pitch slides to the new note. Notes that
don't overlap are fresh hits.

You don't need to edit note lengths by hand. Use the scripts instead:

- **Access808 - Slide selected notes into next note:** select notes, run it,
  and each selected note slides into the note after it. OSARA reports how many
  notes it changed, and which were skipped (the last note, or a next note with
  the same pitch).
- **Access808 - Remove slide from selected notes:** each selected note ends
  just before the next note, so the next note becomes a fresh hit.
- **Access808 - Set slide overlap:** how far the note overlaps the next one,
  as a note division (default 64, a 1/64 note). This does not change the
  sound, only how the overlap is written.

The scripts work in the MIDI editor, or on the selected MIDI item in the
arrange view. Each run is one undo step.

Other glide controls:

- Glide Time is the length of a slide, or Glide Sync sets it as a note length
  that follows the tempo (1/64 to 1/4, including triplets and dotted notes).
  While Glide Sync is on, Glide Time's value ends with "(inactive, Glide Sync
  is on)".
- Glide Timing: the same time for any interval, or time per octave, so that
  bigger jumps take longer.
- Glide Curve: Linear or Exponential.
- Glide Retrigger: Off for a smooth slide, "Re-hit punch" to add the pitch
  drop and click to every slide (common in drill), or "Re-hit punch and
  envelope" to also restart the volume.
- Note Priority: last, lowest or highest held note.
- If you release the newer note while the older one is still held, the pitch
  slides back. If the sustain pedal is down, every note slides.
- Two overlapping notes of the same pitch play as two hits. The first one
  ending does not cut the second.

## Sound notes

- Attack below about 3 ms sounds the same as 0 ms: the 808 starts its
  waveform at zero, so it never clicks. Attack is audible from about 5 ms.
- Click Follows Attack (on by default): a slow attack also softens the click,
  so a fade-in has no lone click in front of it. The click stays full up to
  3 ms of attack. Turn it off to keep a full click with a slow body.
- A new hit on a note that is still sounding always rises over at least 1 ms,
  even with Attack at 0 ms, so fast repeated notes do not click.
- Hit Consistency: "Natural" continues the waveform when a note re-hits while
  the 808 still sounds, like a real 808. "Identical every hit" starts every
  hit the same way, crossfading the old one.
- Every factory preset peaks near -6 dBFS, so presets can be compared by sound
  rather than loudness. The default Output Level is -2 dB.
- Stereo Width spreads only the harmonics above the fundamental. The sub stays
  in mono, so the 808 stays solid on club systems and phones.

## Kick ducking

Make a send from the kick track to the Access808 track (the default channels
1 and 2 are fine), then raise Duck Amount. Duck Threshold sets the kick level
that gives full ducking, and Duck Release sets how fast the 808 comes back.
The kick is not heard through Access808.

To check the routing by ear, play the beat and press Check Ducking in the Mix
and Ducking section. It says whether kick audio is arriving and by how many dB
the 808 is ducked, or what to fix: no kick audio, Duck Amount at 0%, or a
threshold that is too high.

## Using it with OSARA

All 45 controls are host parameters with plain names and values in units, so
the whole synth can be played and automated from OSARA's FX parameter list.
Names start with their group, for example Glide, Pitch Drop, Click, Vibrato,
Velocity and Duck. The parameters are also grouped into sections (Glide,
Pitch, Amp Envelope, Tone, Saturation and Filter, Modulation, Playing, Mix and
Ducking), in the same order as the plugin window. Factory presets appear in
REAPER's preset list.

The plugin window works with screen readers (NVDA, JAWS and Narrator on
Windows, VoiceOver on macOS):

- Tab moves through sections in the same order as OSARA's parameter list:
  Preset, Glide, Pitch, Amp Envelope, Tone, Saturation and Filter, Modulation,
  Playing, Mix and Ducking, then Window.
- F1 speaks what the focused control does, with its current value. The Help
  for Focused Control button in the Window section does the same.
- Messages are spoken in your screen reader's own voice: through OSARA inside
  REAPER, and as a screen reader notification in other hosts.
- On sliders:
  - Up and Down arrows move by a small step; hold Shift for finer steps.
  - Page Up and Page Down move by a large step.
  - Home and End jump to the minimum and maximum.
  - Enter or F2 opens a box where you type a value, such as "120", "1.5 s" or
    "2.5k". The new value is spoken.
- On list boxes, Up and Down change the choice.
- Presets: the Preset list holds factory presets and your own presets. Save As
  (Control+S, also Control and not Command on macOS), Rename and Delete work on user presets, with a name and an
  optional category. Each result is spoken.
- Window: Window Size goes from 100% to 200%, and Theme can be Dark or High
  contrast. When the window is larger than the screen, it scrolls to keep the
  focused control in view.

If REAPER takes the arrow keys or Control+S, turn on "Send all keyboard input
to plugin" in the FX window's menu.

User presets are saved in `%APPDATA%\Access808\Presets\<category>` on
Windows, and in `~/Library/Application Support/Access808/Presets/<category>`
on macOS.

## Building

Requires CMake 3.22 or later, JUCE 8.0.15, and Catch2 3.5.4 for the tests:
Visual Studio 2022 or later on Windows, Xcode on macOS.

JUCE and Catch2 are not in the repository. By default the build looks for them
in `C:\_tools\JUCE` and `C:\_tools\Catch2-3.5.4`; point it elsewhere with
`-DACCESS808_JUCE_DIR=<path>` and `-DACCESS808_CATCH2_DIR=<path>`, or skip
the tests with `-DACCESS808_BUILD_TESTS=OFF`. Apply
`patches/juce-vst3-utf16-vendor.patch` to JUCE with `git apply`, so hosts get
the manufacturer name with its accented letter intact.

1. Configure: `cmake -S . -B build`
2. Build: `cmake --build build --config Release`
3. Test: `ctest --test-dir build -C Release`

The plugins are written to `build/Access808_artefacts/Release/VST3` and, on
macOS, `build/Access808_artefacts/Release/AU`.

On Windows, the simplest way to build everything is to run
`build-installer.bat` (double-click it, or run it from a command prompt). It
configures the project, builds the plugin, runs the tests, builds the
installer, and reports each step as it goes. Use `build-installer.bat notests`
to skip the tests. To build only the installer, which needs Inno Setup 6, run
`cmake --build build --config Release --target installer`. This writes
`build\installer\Access808-<version>-Windows-Setup.exe`. The version comes from
`project()` in `CMakeLists.txt`.

Every push to GitHub builds and tests both platforms. Pushing a version tag,
such as `v0.1.0`, also publishes a release with the Windows installer, the
macOS package and the scripts. The tag must match the version in
`CMakeLists.txt`, and `CHANGELOG.md` must have a section for it.

## License

Access808 is free software under the
[GNU General Public License v3.0 or later](LICENSE). It is built with
[JUCE](https://juce.com), used under JUCE's AGPLv3 licence option, which
GPLv3 is compatible with.

## Author

Hermis Kasperavičius, 2026. Bug reports and ideas are welcome in
[Issues](https://github.com/hermis501/access808/issues).
