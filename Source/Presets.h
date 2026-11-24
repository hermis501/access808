#pragma once

#include "Parameters.h"

#include <utility>
#include <vector>

// Factory presets, exposed to the host as programs (REAPER's preset menu,
// which OSARA reads) and in the editor's preset box. Each preset starts from
// the parameter defaults and overrides the listed values (in real units;
// choices by index). Names start with a category so lists scan by ear.
// Output is set per preset so every preset peaks near -6 dBFS over notes
// 24 to 48: browsing compares sounds, not loudness.

namespace access808
{
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<Preset>& getFactoryPresets()
{
    using namespace ids;
    static const std::vector<Preset> presets {
        { "Init", {} },

        // ---- Classic ----
        { "Classic - 808 Long", {
            { glideTime, 60.0f }, { pitchDrop, 7.0f }, { pitchDropTime, 30.0f }, { decay, 3000.0f },
            { sustain, 0.0f }, { release, 200.0f }, { drive, 3.0f }, { clickLevel, 0.15f }, { output, -3.0f } } },
        { "Classic - 808 Short", {
            { pitchDrop, 5.0f }, { pitchDropTime, 25.0f }, { decay, 900.0f }, { sustain, 0.0f },
            { release, 120.0f }, { drive, 2.0f }, { clickLevel, 0.2f }, { output, -3.0f } } },
        { "Classic - Boom", {
            { pitchDrop, 3.0f }, { decay, 5000.0f }, { sustain, 0.0f }, { release, 400.0f },
            { drive, 0.0f }, { clickLevel, 0.05f }, { subLevel, 0.3f }, { cutoff, 1500.0f }, { output, -5.0f } } },

        // ---- Trap ----
        { "Trap - Long Slide", {
            { glideTime, 180.0f }, { pitchDrop, 12.0f }, { pitchDropTime, 45.0f }, { decay, 4000.0f },
            { sustain, 0.6f }, { release, 400.0f }, { waveShape, 0.15f }, { drive, 9.0f }, { clickLevel, 0.3f }, { output, -1.5f } } },
        { "Trap - Synced Slide", {
            { glideSync, 5.0f }, { pitchDrop, 12.0f }, { decay, 3000.0f }, { sustain, 0.6f },
            { waveShape, 0.2f }, { drive, 10.0f }, { saturationType, 3.0f }, { output, -2.0f } } },
        { "Trap - Clean Sub", {
            { glideTime, 70.0f }, { pitchDrop, 3.0f }, { pitchDropTime, 20.0f }, { sustain, 0.8f },
            { drive, 0.0f }, { clickLevel, 0.05f }, { cutoff, 2000.0f }, { output, -4.5f } } },
        { "Trap - Hard Hitter", {
            { glideTime, 90.0f }, { pitchDrop, 14.0f }, { decay, 3000.0f }, { sustain, 0.5f },
            { drive, 24.0f }, { saturationType, 1.0f }, { cutoff, 6000.0f }, { velocityToDrive, 0.5f }, { output, -1.5f } } },
        { "Trap - Wide Distorted", {
            { glideTime, 120.0f }, { pitchDrop, 12.0f }, { sustain, 0.6f }, { drive, 18.0f },
            { saturationType, 6.0f }, { stereoWidth, 0.6f }, { waveShape, 0.3f }, { output, -9.0f } } },
        { "Trap - Tape Warm", {
            { glideTime, 100.0f }, { pitchDrop, 9.0f }, { decay, 3500.0f }, { sustain, 0.5f },
            { drive, 12.0f }, { saturationType, 3.0f }, { drift, 0.3f }, { output, -2.5f } } },

        // ---- Drill ----
        { "Drill - Slide", {
            { glideTime, 110.0f }, { glideCurve, 1.0f }, { pitchDrop, 18.0f }, { pitchDropTime, 25.0f },
            { decay, 2500.0f }, { sustain, 0.7f }, { release, 300.0f }, { waveShape, 0.25f }, { drive, 14.0f },
            { saturationType, 2.0f }, { clickLevel, 0.45f }, { clickTone, 0.7f }, { output, -4.5f } } },
        { "Drill - Re-hit Slide", {
            { glideTime, 90.0f }, { glideCurve, 1.0f }, { slideRetrigger, 1.0f }, { pitchDrop, 16.0f },
            { pitchDropTime, 20.0f }, { sustain, 0.7f }, { drive, 15.0f }, { saturationType, 2.0f },
            { clickType, 1.0f }, { clickLevel, 0.4f }, { output, -4.5f } } },
        { "Drill - Octave Glide", {
            { glideTime, 150.0f }, { glideTiming, 1.0f }, { pitchDrop, 12.0f }, { sustain, 0.75f },
            { drive, 16.0f }, { saturationType, 6.0f }, { waveShape, 0.2f }, { output, -7.5f } } },
        { "Drill - Wobble Tail", {
            { glideTime, 100.0f }, { sustain, 0.7f }, { drive, 12.0f }, { saturationType, 2.0f },
            { vibratoDepth, 25.0f }, { vibratoRate, 5.5f }, { vibratoDelay, 400.0f }, { output, -4.5f } } },

        // ---- Punch ----
        { "Punch - Short Knock", {
            { glideTime, 40.0f }, { pitchDrop, 24.0f }, { pitchDropTime, 20.0f }, { decay, 600.0f },
            { sustain, 0.0f }, { release, 80.0f }, { drive, 10.0f }, { clickType, 2.0f }, { clickLevel, 0.5f },
            { phaseReset, 1.0f }, { output, -1.0f } } },
        { "Punch - Bit Crushed", {
            { pitchDrop, 12.0f }, { decay, 1500.0f }, { sustain, 0.3f }, { drive, 20.0f },
            { saturationType, 5.0f }, { saturationMix, 0.6f }, { cutoff, 8000.0f }, { output, -3.0f } } },
        { "Punch - Folded", {
            { pitchDrop, 10.0f }, { sustain, 0.5f }, { drive, 14.0f }, { saturationType, 4.0f },
            { cutoff, 7000.0f }, { resonance, 0.2f }, { output, -1.5f } } },
    };
    return presets;
}
} // namespace access808
