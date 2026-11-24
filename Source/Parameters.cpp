#include "Parameters.h"

#include <map>

namespace access808
{
namespace
{
    using Float  = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Attr   = juce::AudioParameterFloatAttributes;

    juce::ParameterID pid (const char* id) { return { id, parameterVersion }; }

    // Parses the leading number of strings like "80 ms", "1.2 kHz", "-6 dB", "50%".
    float parseNumber (const juce::String& text)
    {
        auto t = text.trim().toLowerCase();
        float value = t.getFloatValue();
        if (t.containsChar ('k') && ! t.contains ("ms"))
            value *= 1000.0f;
        return value;
    }

    juce::String trimZeros (float v, int decimals)
    {
        auto s = juce::String (v, decimals);
        if (s.containsChar ('.'))
            s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return s == "-0" ? juce::String ("0") : s;
    }

    Attr msAttr()
    {
        return Attr().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int)
            {
                if (v >= 1000.0f) return trimZeros (v / 1000.0f, 2) + " s";
                return trimZeros (v, v < 10.0f ? 1 : 0) + " ms";
            })
            .withValueFromStringFunction ([] (const juce::String& s)
            {
                const auto t = s.trim().toLowerCase();
                const float v = t.getFloatValue();
                // "1.5 s" or "1.5 seconds" -> ms; plain numbers are ms.
                const bool seconds = (t.endsWith ("s") && ! t.endsWith ("ms")) || t.endsWith ("seconds") || t.endsWith ("second");
                return seconds ? v * 1000.0f : v;
            });
    }

    Attr percentAttr()
    {
        return Attr().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s) / 100.0f; });
    }

    Attr unitAttr (const juce::String& unit, int decimals, bool showPlus = false)
    {
        return Attr().withLabel (unit)
            .withStringFromValueFunction ([unit, decimals, showPlus] (float v, int)
            {
                auto s = trimZeros (v, decimals);
                if (showPlus && v > 0.0f) s = "+" + s;
                return s + " " + unit;
            })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); });
    }

    Attr hzAttr (int decimals)
    {
        return Attr().withLabel ("Hz")
            .withStringFromValueFunction ([decimals] (float v, int)
            {
                if (v >= 1000.0f) return trimZeros (v / 1000.0f, 2) + " kHz";
                return trimZeros (v, decimals) + " Hz";
            })
            .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); });
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float interval = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, interval);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::NormalisableRange<float> linear (float lo, float hi, float interval)
    {
        return { lo, hi, interval };
    }

    std::unique_ptr<Float> percent (const char* id, const char* name, float def)
    {
        return std::make_unique<Float> (pid (id), name, linear (0.0f, 1.0f, 0.01f), def, percentAttr());
    }

    std::unique_ptr<Choice> choice (const char* id, const char* name, juce::StringArray items, int def)
    {
        return std::make_unique<Choice> (pid (id), name, items, def);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Each section is a parameter group. VST3 hosts see them as units, and
    // OSARA's FX parameter dialog shows them as a tree.
    auto group = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioProcessorParameterGroup> (id, name, "|");
    };

    // ---- Glide ----
    {
        auto g = group ("glide", "Glide");
        // Glide Sync is made first so Glide Time's text can say when it is inactive.
        auto sync = choice (ids::glideSync, "Glide Sync",
                            { "Off (use Glide Time)", "1/64 note", "1/32 triplet", "1/32 note", "1/16 triplet",
                              "1/16 note", "1/8 triplet", "1/16 dotted", "1/8 note", "1/4 triplet", "1/8 dotted",
                              "1/4 note" }, 0);
        auto* syncParam = sync.get();

        auto timeAttr = msAttr();
        auto msText = timeAttr.getStringFromValueFunction();
        timeAttr = timeAttr.withStringFromValueFunction ([msText, syncParam] (float v, int len)
        {
            auto s = msText (v, len);
            if (syncParam->getIndex() > 0)
                s << " (inactive, Glide Sync is on)";
            return s;
        });

        g->addChild (choice (ids::glideMode, "Glide Mode", { "Off", "Legato (overlap)" }, 1));
        g->addChild (std::make_unique<Float> (pid (ids::glideTime), "Glide Time",
                                             skewed (0.0f, 2000.0f, 150.0f, 1.0f), 80.0f, timeAttr));
        g->addChild (std::move (sync));
        g->addChild (choice (ids::glideTiming, "Glide Timing", { "Same time for any interval", "Time per octave" }, 0));
        g->addChild (choice (ids::glideCurve, "Glide Curve", { "Linear", "Exponential" }, 0));
        g->addChild (choice (ids::slideRetrigger, "Glide Retrigger",
                            { "Off", "Re-hit punch", "Re-hit punch and envelope" }, 0));
        g->addChild (choice (ids::notePriority, "Note Priority", { "Last note", "Lowest note", "Highest note" }, 0));
        layout.add (std::move (g));
    }

    // ---- Pitch ----
    {
        auto g = group ("pitch", "Pitch");
        g->addChild (std::make_unique<Float> (pid (ids::tune), "Tune", linear (-24.0f, 24.0f, 1.0f), 0.0f,
                                             unitAttr ("semitones", 0, true)));
        g->addChild (std::make_unique<Float> (pid (ids::fine), "Fine Tune", linear (-100.0f, 100.0f, 1.0f), 0.0f,
                                             unitAttr ("cents", 0, true)));
        g->addChild (std::make_unique<Float> (pid (ids::tuningRef), "Tuning Reference (A4)",
                                             linear (415.0f, 466.0f, 0.1f), 440.0f, hzAttr (1)));
        g->addChild (std::make_unique<Float> (pid (ids::pitchDrop), "Pitch Drop Amount", linear (0.0f, 48.0f, 0.1f), 12.0f,
                                             unitAttr ("semitones", 1)));
        g->addChild (std::make_unique<Float> (pid (ids::pitchDropTime), "Pitch Drop Time",
                                             skewed (1.0f, 500.0f, 50.0f, 0.1f), 40.0f, msAttr()));
        layout.add (std::move (g));
    }

    // ---- Amp envelope ----
    {
        auto g = group ("envelope", "Amp Envelope");
        g->addChild (std::make_unique<Float> (pid (ids::attack), "Attack", skewed (0.0f, 200.0f, 10.0f, 0.1f), 1.0f, msAttr()));
        g->addChild (std::make_unique<Float> (pid (ids::hold), "Hold", skewed (0.0f, 2000.0f, 200.0f, 1.0f), 0.0f, msAttr()));
        g->addChild (std::make_unique<Float> (pid (ids::decay), "Decay", skewed (10.0f, 10000.0f, 1000.0f, 1.0f), 1500.0f, msAttr()));
        g->addChild (std::make_unique<Float> (pid (ids::decayCurve), "Decay Curve", linear (0.0f, 1.0f, 0.01f), 0.0f,
                                             Attr().withLabel ("%")
                                                 .withStringFromValueFunction ([] (float v, int)
                                                 {
                                                     const int pc = juce::roundToInt (v * 100.0f);
                                                     juce::String s (pc);
                                                     s << "%";
                                                     if (pc == 0)   s << " (exponential)";
                                                     if (pc == 50)  s << " (linear)";
                                                     if (pc == 100) s << " (held)";
                                                     return s;
                                                 })
                                                 .withValueFromStringFunction ([] (const juce::String& s)
                                                 {
                                                     const auto t = s.toLowerCase();
                                                     if (t.contains ("exp"))  return 0.0f;
                                                     if (t.contains ("lin"))  return 0.5f;
                                                     if (t.contains ("held")) return 1.0f;
                                                     return parseNumber (s) / 100.0f;
                                                 })));
        g->addChild (percent (ids::sustain, "Sustain", 0.5f));
        g->addChild (std::make_unique<Float> (pid (ids::release), "Release", skewed (5.0f, 5000.0f, 300.0f, 1.0f), 250.0f, msAttr()));
        g->addChild (choice (ids::phaseReset, "Hit Consistency",
                            { "Natural (reset only from silence)", "Identical every hit (crossfaded)" }, 0));
        layout.add (std::move (g));
    }

    // ---- Tone ----
    {
        auto g = group ("tone", "Tone");
        g->addChild (percent (ids::waveShape, "Wave Shape (sine to triangle)", 0.0f));
        g->addChild (percent (ids::subLevel, "Sub Octave Level", 0.0f));
        g->addChild (choice (ids::clickType, "Click Type", { "Noise", "Tick", "Knock" }, 0));
        g->addChild (percent (ids::clickLevel, "Click Level", 0.3f));
        g->addChild (percent (ids::clickTone, "Click Tone", 0.5f));
        g->addChild (choice (ids::clickFollowsAttack, "Click Follows Attack", { "Off", "On" }, 1));
        layout.add (std::move (g));
    }

    // ---- Saturation and filter ----
    {
        auto g = group ("saturation", "Saturation and Filter");
        g->addChild (choice (ids::saturationType, "Saturation Type",
                            { "Soft clip", "Hard clip", "Tube (asymmetric)", "Tape", "Wavefolder", "Bit crush",
                              "Multiband (clean sub)" }, 0));
        g->addChild (std::make_unique<Float> (pid (ids::drive), "Drive", linear (0.0f, 36.0f, 0.1f), 6.0f, unitAttr ("dB", 1)));
        g->addChild (choice (ids::driveCompensation, "Drive Compensation", { "Off", "On" }, 1));
        g->addChild (percent (ids::saturationMix, "Saturation Mix", 1.0f));
        g->addChild (choice (ids::oversampling, "Oversampling", { "4x", "8x", "4x live, 8x when rendering" }, 2));
        g->addChild (std::make_unique<Float> (pid (ids::cutoff), "Filter Cutoff", skewed (20.0f, 20000.0f, 1000.0f, 1.0f),
                                             20000.0f, hzAttr (0)));
        g->addChild (percent (ids::resonance, "Filter Resonance", 0.0f));
        layout.add (std::move (g));
    }

    // ---- Modulation ----
    {
        auto g = group ("modulation", "Modulation");
        g->addChild (std::make_unique<Float> (pid (ids::vibratoRate), "Vibrato Rate", skewed (0.1f, 12.0f, 4.0f, 0.01f), 5.0f,
                                             hzAttr (2)));
        g->addChild (std::make_unique<Float> (pid (ids::vibratoDepth), "Vibrato Depth", linear (0.0f, 100.0f, 1.0f), 0.0f,
                                             unitAttr ("cents", 0)));
        g->addChild (std::make_unique<Float> (pid (ids::vibratoDelay), "Vibrato Delay", skewed (0.0f, 2000.0f, 300.0f, 1.0f),
                                             300.0f, msAttr()));
        g->addChild (percent (ids::drift, "Analog Drift", 0.0f));
        layout.add (std::move (g));
    }

    // ---- Playing ----
    {
        auto g = group ("playing", "Playing");
        g->addChild (percent (ids::velocitySens, "Velocity to Level", 0.5f));
        g->addChild (percent (ids::velocityToDrive, "Velocity to Drive", 0.0f));
        g->addChild (percent (ids::velocityToClick, "Velocity to Click", 0.0f));
        g->addChild (std::make_unique<Float> (pid (ids::bendRange), "Pitch Bend Range", linear (0.0f, 24.0f, 1.0f), 12.0f,
                                             unitAttr ("semitones", 0)));
        layout.add (std::move (g));
    }

    // ---- Mix ----
    {
        auto g = group ("mix", "Mix and Ducking");
        g->addChild (percent (ids::stereoWidth, "Stereo Width", 0.0f));
        g->addChild (percent (ids::duckAmount, "Duck Amount", 0.0f));
        g->addChild (std::make_unique<Float> (pid (ids::duckThreshold), "Duck Threshold", linear (-60.0f, 0.0f, 0.1f), -24.0f,
                                             unitAttr ("dB", 1)));
        g->addChild (std::make_unique<Float> (pid (ids::duckRelease), "Duck Release", skewed (10.0f, 1000.0f, 150.0f, 1.0f),
                                             120.0f, msAttr()));
        g->addChild (std::make_unique<Float> (pid (ids::output), "Output Level", linear (-48.0f, 12.0f, 0.1f), -2.0f,
                                             unitAttr ("dB", 1, true)));
        layout.add (std::move (g));
    }

    return layout;
}

juce::String getParameterHelp (const juce::String& id)
{
    static const std::map<juce::String, juce::String> help {
        { ids::glideMode,         "Legato: a note that starts while the previous note is still held slides to the new pitch. Off: every note is a new hit." },
        { ids::glideTime,         "How long a slide takes. Inactive when Glide Sync is set to a note length; the value then says so." },
        { ids::glideSync,         "Sets the slide time as a note length that follows the project tempo." },
        { ids::glideTiming,       "Same time: every slide takes the glide time. Time per octave: bigger intervals take longer." },
        { ids::glideCurve,        "Linear moves evenly. Exponential moves fast at first, then settles." },
        { ids::slideRetrigger,    "What a slide re-hits. Off: a smooth slide. Re-hit punch: pitch drop and click on every slide. Punch and envelope: also restarts the volume envelope." },
        { ids::notePriority,      "Which held key sounds when several are held." },
        { ids::tune,              "Transposes the whole 808 in semitones." },
        { ids::fine,              "Fine tuning in cents." },
        { ids::tuningRef,         "Frequency of A above middle C. Standard is 440 hertz." },
        { ids::pitchDrop,         "How far above the note each new hit starts, for punch." },
        { ids::pitchDropTime,     "How quickly the punch pitch falls to the note." },
        { ids::attack,            "Fade-in time of each new hit." },
        { ids::hold,              "Time at full level before the decay starts." },
        { ids::decay,             "Time to fall from full level to the sustain level." },
        { ids::decayCurve,        "Shape of the decay. Zero percent is exponential like a classic 808, fifty is linear, one hundred holds the level longer, then falls." },
        { ids::sustain,           "Level held while the key is down. Zero gives a pure decaying 808." },
        { ids::release,           "Fade-out time after the key is released." },
        { ids::phaseReset,        "Natural: a new hit while the 808 still sounds continues the waveform, like a real 808. Identical every hit: every hit starts the waveform from zero for the same punch each time, crossfading the old hit to avoid clicks." },
        { ids::waveShape,         "Zero percent is a pure sine. Higher adds triangle harmonics, which carry better on small speakers." },
        { ids::subLevel,          "Adds a sine one octave below." },
        { ids::clickType,         "Noise is a short burst, tick is a high blip, knock is a woody thump." },
        { ids::clickLevel,        "Level of the attack click on new hits." },
        { ids::clickTone,         "Brightness of the click." },
        { ids::clickFollowsAttack, "On: a slow attack also softens the click, so a soft fade-in has no lone click in front. The click is full up to 3 milliseconds of attack." },
        { ids::saturationType,    "Character of the distortion. Multiband keeps the sub clean and distorts only the upper part." },
        { ids::drive,             "Gain into the saturator. More drive gives more distortion and harmonics." },
        { ids::driveCompensation, "Keeps the loudness roughly level as drive changes, so you judge the tone, not the volume." },
        { ids::saturationMix,     "Blend of clean and saturated signal." },
        { ids::oversampling,      "Higher oversampling reduces harsh aliasing at high drive but uses more processor." },
        { ids::cutoff,            "Low-pass filter after saturation, to tame harshness." },
        { ids::resonance,         "Emphasis at the filter cutoff." },
        { ids::vibratoRate,       "Speed of the vibrato." },
        { ids::vibratoDepth,      "Depth of the vibrato in cents. Zero turns it off." },
        { ids::vibratoDelay,      "Time after a hit before the vibrato fades in." },
        { ids::drift,             "Slow random pitch movement, like an analog circuit." },
        { ids::velocitySens,      "How much note velocity changes loudness. Zero ignores velocity." },
        { ids::velocityToDrive,   "At full velocity the drive is as set. Softer notes get up to 18 decibels less drive." },
        { ids::velocityToClick,   "How much note velocity changes the click level." },
        { ids::bendRange,         "Pitch wheel range in semitones, up and down." },
        { ids::stereoWidth,       "Widens the upper harmonics. The sub always stays mono, and the sound stays safe in mono." },
        { ids::duckAmount,        "How much the 808 ducks when the kick hits. Send the kick track to this track to use it." },
        { ids::duckThreshold,     "Kick level at which the ducking reaches the full amount." },
        { ids::duckRelease,       "How quickly the 808 comes back after the kick." },
        { ids::output,            "Final output level." },
    };

    if (auto it = help.find (id); it != help.end())
        return it->second;
    return {};
}

} // namespace access808
