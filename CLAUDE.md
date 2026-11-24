# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Access808: an accessible, synthesized, monophonic 808 bass plugin (JUCE 8; VST3 on Windows and macOS, plus AU on macOS) for blind hip-hop/trap/drill producers using REAPER + OSARA. The core feature is glide that needs no sighted help. Decisions already made with the user:

- Glide is triggered by **overlap/legato only**. Bundled ReaScripts create or remove the overlap, so the producer never edits note ends by hand.
- The engine is **synthesized** (no samples).
- The formats are **VST3, plus AU on macOS**. Windows is the main platform (this machine); macOS is built and tested only in CI.
- The license is **GPLv3 or later** (JUCE is used under its AGPLv3 option), as in the user's ASampler project, which this repo copies for CMake, CI and packaging.
- The UI is **host parameters first**, plus an accessible editor.
- Out of scope for now, at the user's request: a post-distortion EQ, an output clipper and level meters.

The user asks questions as numbered plain-text lists, not through dialog tools. `README.md` is the user-facing manual; keep it in sync when behaviour changes.

## Commands

External dependencies live outside the repo: JUCE 8.0.15 at `C:/_tools/JUCE` (`-DACCESS808_JUCE_DIR=`), Catch2 at `C:/_tools/Catch2-3.5.4` (`-DACCESS808_CATCH2_DIR=`), and pluginval at `C:/_tools/pluginval.exe`. The local JUCE already carries `patches/juce-vst3-utf16-vendor.patch`; CI clones stock JUCE and applies it. JUCE stores that header with CRLF, so the patch body lines are CRLF too: keep them, or `git apply` fails on macOS.

```sh
cmake -S . -B build -G "Visual Studio 18 2026" -A x64          # configure (first run ~1.5 min)
cmake --build build --config Release --target Access808_VST3 -- -m
cmake --build build --config Release --target EngineTests ProcessorTests -- -m
./build/Release/EngineTests.exe                                  # engine unit tests (JUCE-free)
./build/Release/EngineTests.exe "Every note is in tune*"         # one test case (Catch2 name/wildcard)
./build/Release/ProcessorTests.exe                               # whole processor rendered offline
/c/_tools/pluginval.exe --strictness-level 10 --validate-in-process "build/Access808_artefacts/Release/VST3/Access808.vst3"
python tests/check_vst3_vendor.py build/Access808_artefacts/Release/VST3/Access808.vst3   # manufacturer name intact
```

The installer is built with Inno Setup 6 (`C:/Program Files (x86)/Inno Setup 6/ISCC.exe`):

```sh
cmake --build build --config Release --target installer          # -> build/installer/Access808-<version>-Windows-Setup.exe
```

`build-installer.bat` (repo root, `notests` argument optional) is the user's one-step manual build: configure if needed, plugin, tests, installer. It ends with `pause`. Keep it CRLF and ASCII, and keep its messages plain text, because the user listens to them with a screen reader.

To test the `reaper-kb.ini` editing without installing anything:

1. Compile it from PowerShell, not Git Bash, because MSYS mangles `/` switches: `ISCC /O<dir> installer\tests\ReaperActionsTest.iss`
2. Run `<dir>\ReaperActionsTest.exe /kb=<copy of reaper-kb.ini> /report=<file>`. Don't use `/log`, because Inno reserves it.

GitHub: `hermis501/access808` (public). `.github/workflows/build.yml` builds, tests (ctest), checks the vendor name, runs pluginval (and auval on macOS) and packages on Windows and macOS for every push. A `v<version>` tag also publishes a release; the tag must match `project(Access808 VERSION ...)` and `CHANGELOG.md` needs a `## <version>` section, whose text becomes the release notes. The macOS `.pkg` installs only the plugins; the REAPER scripts ship as a separate zip.

Add `--skip-gui-tests` to pluginval to skip opening the editor. The built plugin is not copied to the system VST3 folder. There is no Lua interpreter on this machine, so the ReaScripts can only be checked by reading them or running them in REAPER.

## Architecture

The signal path is split so that the musically important logic can be tested without JUCE.

- `Source/Engine808.h` is header-only, has **no JUCE dependency**, and is what `tests/EngineTests.cpp` tests. It handles everything per note:
  - The held-note stack with last, low or high priority.
  - The legato decision: glide if the gate is on (a key or the sustain pedal is held), otherwise retrigger.
  - Glide that takes a constant time or a time per octave, with a linear or exponential curve, plus the slide retrigger options.
  - The pitch-drop punch and the click (noise, tick or knock).
  - The AHDSR envelope, which is position-based: decay and release times are exact, and the decay curve goes exponential, then linear, then held.
  - Same-pitch overlaps, counted per pitch so the first note-off doesn't end the second; the repeat is a new hit.
  - A minimum 1 ms rise when a sounding note re-hits, and the click scaled down by attacks over 3 ms (Click Follows Attack).
  - The phase reset with a 2 ms crossfade, vibrato, drift, the tuning reference and pitch bend.
  - Smoothing for tune, wave, sub and sustain.

  `render(out, velocityOut, n)` also writes the per-sample velocity of the sounding note, which the processor uses for velocity-to-drive.
- `Source/PluginProcessor.cpp` processes each block in this order:
  1. Read the sidechain input (bus 0, named "Sidechain (kick)") into a per-sample duck gain, **before** the in-place buffer is overwritten.
  2. Render the engine sample-accurately, split at MIDI events.
  3. Build the per-sample drive gain.
  4. Apply oversampled saturation (7 types; mix and drive compensation happen inside the oversampled domain).
  5. Apply the DC blocker, the SVF low-pass, the stereo width (M/S with a delayed band as the side, so L+R equals 2×mid exactly; the band starts at 1.5× the note's frequency, 60 to 300 Hz), the duck gain and the output gain.

  The 4x and 8x oversamplers are both prepared, each in its own `SaturationStage` with its own crossover, smoothers and tape state, so switching between them never allocates. `selectOversampling()` picks one each block (8x when rendering if the parameter says so) and updates `setLatencySamples`. Saturation is processed in chunks of at most the prepared block size. Parameters are read through `RawParams` pointers cached in the constructor.
- `Source/Parameters.cpp` holds the APVTS layout (45 parameters, in one parameter group per section, which OSARA shows as a tree) and the help text. **The parameter names and value strings are the main UI for OSARA users.**
  - Use plain English, start names with their group word, put a unit in every value string, and keep `valueFromString` able to parse what `stringFromValue` produces (also "1.5 s" and "2.5k").
  - The layout order is the order OSARA lists them in.
  - IDs are saved in projects, so never rename them. Choice parameters are read as int indices that map onto engine enums, so append new choices at the end.
  - `glideSyncBeats` must match the Glide Sync choice list.
- `Source/Presets.h` holds the factory presets as overrides on top of the defaults (real units, choice indices), with "Category - Name" names. Each sets Output so it peaks near -6 dBFS over notes 24–48; re-measure when changing a preset's sound. They are exposed as host programs.
- `Source/PresetManager.cpp` keeps the combined factory + user list. User presets are APVTS XML files at `%APPDATA%/Access808/Presets/<Category>/<Name>.a808preset` (macOS: `~/Library/Application Support/Access808/Presets`). Loading sets each parameter with `setValueNotifyingHost`. Host programs cover factory presets only. It also does Save over a user preset, Export to any file, and Import (copies into the root folder, numbering a taken name as "Name (2)"). Tests redirect the folder with `setUserPresetDirectoryForTests`.
- `Source/PluginEditor.cpp` is the accessible editor:
  - Content is laid out at 100% inside `content` and scaled by a transform inside `scaledHolder`, which a `Viewport` scrolls. A `FocusChangeListener` scrolls the focused control into view.
  - `ui::Section` is an accessibility `group` and focus container. Tab order comes from `setExplicitFocusOrder` on the sections: the full-width Preset section at the top (preset list, Previous, Next, then Save, Save As, Rename, Delete, Import, Export, Open Folder), then column by column. `Section::paintOverChildren` draws the visible focus ring.
  - Labels use `setAccessible(false)`, because each control carries its own title and help text.
  - `ui::AccessibleSlider` steps 1% or 10% of the travel (at least one interval), supports Home and End, and opens a typed-value prompt on Enter or F2, or on a click on its value box. Double-click or Alt+click resets to the parameter default.
  - JUCE gives screen readers a slider's, combo box's or button's **tooltip** as its accessibility help, not `getHelpText()`. So every control's tooltip is exactly its plain help text. `ui::HelpTooltipWindow::getTipFor` adds the name and the mouse hints for display only; JUCE hides the tooltip window from screen readers.
  - `ProcessorTests.exe "Editor snapshot"` (hidden test) saves the editor as `%TEMP%/Access808-editor.png`, to check the layout without a host. The content must stay under about 1290 px wide, to fit a 1366 px screen.
  - `ui::announce()` goes to the editor's speaker: `osara_outputMessage` when REAPER and OSARA are present (found in `ReaperHost.cpp` through `IReaperHostApplication`), otherwise a UIA notification from `ScreenReaderAnnouncer` (ported from ASampler), never JUCE's SAPI voice. Don't announce what the screen reader already reads, such as a list box's new item.
  - NVDA doesn't read accessibility help text, so F1 (and the Help button) speaks the last focused control's title, value and help.
  - Editor sections follow the parameter layout order, so OSARA's list and the Tab order match.
  - Window size and theme are per-user settings in a `PropertiesFile`, not host parameters.
- `ReaScripts/` contains Lua actions that write or remove the note overlap in the active MIDI take and speak the results with `reaper.osara_outputMessage`. Each action loads `Access808_common.lua` from its own folder.

- `installer/Access808.iss` is the Inno Setup script:
  - It installs the VST3 bundle to `{commoncf64}\VST3` with admin rights.
  - The `reaperscripts` task (on by default) shows a REAPER resource folder page, copies the scripts to `<resource>\Scripts\Access808`, and registers them in `reaper-kb.ini` in sections 0 and 32060 (the MIDI editor uses the `RS7d3c_` prefix). It asks for REAPER to be closed first, detected through the `REAPERwnd` window class.
  - The resource path is saved in `HKA\Software\Access808` for the uninstaller.
  - The action IDs are fixed SHA-1 values in `installer/ReaperActions.iss`. Change them only together with the script file names.
  - `reaper-kb.ini` is edited as raw `AnsiString` bytes, so users' UTF-8 lines survive. Don't switch to the `LoadStringsFromFile`/`SaveStringsToFile` line APIs.
  - The AppId GUID must never change.

The manufacturer is "Hermis Kasperavičius" (`ACCESS808_VENDOR` in `CMakeLists.txt`, and `AppPublisher` in `installer/Access808.iss`). Protect the č:

- Keep `CMakeLists.txt` UTF-8 and `Access808.iss` UTF-8 with a BOM, which is what makes Inno read it as UTF-8.
- The global `/utf-8` compile option must stay. The VST3 wrapper target compiles `JucePlugin_Manufacturer` too.
- JUCE's moduleinfo helper is a separate CMake project without `/utf-8`, so `cmake/FixModuleInfoVendor.cmake` rewrites the Vendor fields after the build.
- JUCE doesn't regenerate `build/Access808_artefacts/JuceLibraryCode/Access808_resources.rc` when `COMPANY_NAME` changes. Delete it so the DLL's file properties update.
- `C:/_tools/JUCE` carries a local patch (Azimuter) in `juce_VST3ModuleInfo.h` that fixes the UTF-16 class info. Stock JUCE widens each UTF-8 byte separately.

When adding a parameter:

1. Add the ID in `Parameters.h`.
2. Add it to the layout and the help text in `Parameters.cpp`.
3. Add the pointer to `RawParams`, in the header and in the constructor initialiser, **in the same order**.
4. Wire it in `readEngineParams` or `processBlock`.
5. Add it to a section in the editor constructor, and mention it in `README.md`.
