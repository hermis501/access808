#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Every user-facing control is a host parameter. OSARA users drive the plugin
// mostly through REAPER's FX parameter list, so the parameter *names* and the
// *value text* (getText) are the primary UI: keep names plain English, no
// abbreviations, group words first ("Glide ...", "Click ...", "Duck ..."),
// and give every value a unit.
//
// The layout order below is the order OSARA lists them in: most used first.

namespace access808::ids
{
    // Glide
    inline constexpr auto glideMode         = "glideMode";
    inline constexpr auto glideTime         = "glideTime";
    inline constexpr auto glideSync         = "glideSync";
    inline constexpr auto glideTiming       = "glideTiming";
    inline constexpr auto glideCurve        = "glideCurve";
    inline constexpr auto slideRetrigger    = "slideRetrigger";
    inline constexpr auto notePriority      = "notePriority";
    // Pitch
    inline constexpr auto tune              = "tune";
    inline constexpr auto fine              = "fine";
    inline constexpr auto tuningRef         = "tuningRef";
    inline constexpr auto pitchDrop         = "pitchDrop";
    inline constexpr auto pitchDropTime     = "pitchDropTime";
    inline constexpr auto pitchDropCurve    = "pitchDropCurve";
    // Envelope
    inline constexpr auto attack            = "attack";
    inline constexpr auto hold              = "hold";
    inline constexpr auto decay             = "decay";
    inline constexpr auto decaySync         = "decaySync";
    inline constexpr auto decayCurve        = "decayCurve";
    inline constexpr auto sustain           = "sustain";
    inline constexpr auto release           = "release";
    inline constexpr auto phaseReset        = "phaseReset";
    // Tone
    inline constexpr auto waveShape         = "waveShape";
    inline constexpr auto harmonics         = "harmonics";
    inline constexpr auto subLevel          = "subLevel";
    inline constexpr auto clickType         = "clickType";
    inline constexpr auto clickLevel        = "clickLevel";
    inline constexpr auto clickTone         = "clickTone";
    inline constexpr auto clickFollowsAttack = "clickFollowsAttack";
    // Saturation and filter
    inline constexpr auto saturationType    = "saturationType";
    inline constexpr auto drive             = "drive";
    inline constexpr auto driveCompensation = "driveCompensation";
    inline constexpr auto saturationMix     = "saturationMix";
    inline constexpr auto oversampling      = "oversampling";
    inline constexpr auto cutoff            = "cutoff";
    inline constexpr auto resonance         = "resonance";
    // Modulation
    inline constexpr auto vibratoRate       = "vibratoRate";
    inline constexpr auto vibratoDepth      = "vibratoDepth";
    inline constexpr auto vibratoDelay      = "vibratoDelay";
    inline constexpr auto drift             = "drift";
    // Playing
    inline constexpr auto velocitySens      = "velocitySens";
    inline constexpr auto velocityToDrive   = "velocityToDrive";
    inline constexpr auto velocityToClick   = "velocityToClick";
    inline constexpr auto velocityToPitchDrop = "velocityToPitchDrop";
    inline constexpr auto bendRange         = "bendRange";
    // Mix
    inline constexpr auto stereoWidth       = "stereoWidth";
    inline constexpr auto duckAmount        = "duckAmount";
    inline constexpr auto duckThreshold     = "duckThreshold";
    inline constexpr auto duckRelease       = "duckRelease";
    inline constexpr auto output            = "output";
}

namespace access808
{
    // Increment when a change to the layout would break saved projects.
    inline constexpr int parameterVersion = 1;

    // Glide Sync choices, as note lengths in quarter-note beats (index 0 = off).
    inline constexpr float glideSyncBeats[] = { 0.0f, 1.0f / 16.0f, 1.0f / 12.0f, 1.0f / 8.0f, 1.0f / 6.0f,
                                                0.25f, 1.0f / 3.0f, 0.375f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f };

    // Decay Sync choices, as note lengths in quarter-note beats (index 0 = off).
    // A bar is four beats.
    inline constexpr float decaySyncBeats[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f };

    inline constexpr float maxVelocityDriveCutDb = 18.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Short help text for each parameter, used as accessible help in the editor.
    juce::String getParameterHelp (const juce::String& paramId);
}
