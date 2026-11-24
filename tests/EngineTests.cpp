#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Engine808.h"

#include <cmath>
#include <vector>

using namespace access808;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    int msToSamples (float ms) { return (int) (ms * 0.001 * sr); }

    std::vector<float> run (Engine808& e, int samples)
    {
        std::vector<float> buf ((size_t) samples);
        e.render (buf.data(), samples);
        return buf;
    }

    Engine808 makeEngine (EngineParams p = {})
    {
        Engine808 e;
        e.prepare (sr);
        e.setParams (p);
        return e;
    }

    // A pure, steady tone: no punch, click, drift or vibrato.
    EngineParams pureTone()
    {
        EngineParams p;
        p.pitchDropSemis = 0.0f;
        p.clickLevel = 0.0f;
        p.sustain = 1.0f;
        return p;
    }

    // Frequency from positive-going zero crossings with linear interpolation.
    double measureFrequency (const std::vector<float>& x)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (size_t i = 1; i < x.size(); ++i)
        {
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = (double) (i - 1) + (double) (-x[i - 1]) / (double) (x[i] - x[i - 1]);
                if (first < 0.0) first = t;
                last = t;
                ++crossings;
            }
        }
        return crossings > 1 ? (crossings - 1) * sr / (last - first) : 0.0;
    }

    double cents (double measured, double expected) { return 1200.0 * std::log2 (measured / expected); }

    double noteHz (int note, double ref = 440.0) { return ref * std::exp2 ((note - 69) / 12.0); }
}

// ---- Glide ------------------------------------------------------------------

TEST_CASE ("Overlapping notes glide without retriggering the envelope")
{
    EngineParams p;
    p.glideMs = 100.0f;
    p.sustain = 1.0f;
    auto e = makeEngine (p);

    e.noteOn (36, 1.0f);
    run (e, msToSamples (50));
    REQUIRE (e.getStage() != Engine808::Stage::attack);

    e.noteOn (43, 1.0f); // still holding 36 -> legato
    REQUIRE (e.isGliding());
    REQUIRE (e.getStage() != Engine808::Stage::attack);

    run (e, msToSamples (50));
    REQUIRE_THAT (e.getCurrentPitch(), WithinAbs (39.5, 0.1)); // linear: halfway at half time

    run (e, msToSamples (60));
    REQUIRE_FALSE (e.isGliding());
    REQUIRE_THAT (e.getCurrentPitch(), WithinAbs (43.0, 1e-4));
}

TEST_CASE ("Non-overlapping notes retrigger and jump")
{
    auto e = makeEngine();
    e.noteOn (36, 1.0f);
    run (e, msToSamples (20));
    e.noteOff (36);
    run (e, msToSamples (5));

    e.noteOn (48, 1.0f);
    REQUIRE_FALSE (e.isGliding());
    REQUIRE (e.getStage() == Engine808::Stage::attack);
    REQUIRE_THAT (e.getCurrentPitch(), WithinAbs (48.0, 1e-6));
}

TEST_CASE ("Glide mode Off never glides")
{
    EngineParams p;
    p.glideMode = GlideMode::off;
    auto e = makeEngine (p);

    e.noteOn (36, 1.0f);
    run (e, 100);
    e.noteOn (40, 1.0f);
    REQUIRE_FALSE (e.isGliding());
    REQUIRE (e.getStage() == Engine808::Stage::attack);
    REQUIRE_THAT (e.getCurrentPitch(), WithinAbs (40.0, 1e-6));
}

TEST_CASE ("Releasing the newer note slides back to the still-held note")
{
    EngineParams p;
    p.glideMs = 50.0f;
    auto e = makeEngine (p);

    e.noteOn (36, 1.0f);
    run (e, 100);
    e.noteOn (41, 1.0f);
    run (e, msToSamples (60));
    REQUIRE_THAT (e.getCurrentPitch(), WithinAbs (41.0, 1e-4));

    e.noteOff (41);
    REQUIRE (e.isGliding());
    REQUIRE_THAT (e.getGlideTarget(), WithinAbs (36.0, 1e-6));
    REQUIRE (e.getStage() != Engine808::Stage::release);
}

TEST_CASE ("Releasing the older note keeps the current pitch")
{
    auto e = makeEngine();
    e.noteOn (36, 1.0f);
    run (e, 100);
    e.noteOn (41, 1.0f);
    run (e, msToSamples (200));
    e.noteOff (36);
    REQUIRE_FALSE (e.isGliding());
    REQUIRE_THAT (e.getCurrentPitch(), WithinAbs (41.0, 1e-4));
}

TEST_CASE ("Sustain pedal keeps the gate open so the next note glides")
{
    auto e = makeEngine();
    e.setSustainPedal (true);
    e.noteOn (36, 1.0f);
    run (e, 100);
    e.noteOff (36);
    REQUIRE (e.getStage() != Engine808::Stage::release);

    e.noteOn (38, 1.0f);
    REQUIRE (e.isGliding());

    e.noteOff (38);
    e.setSustainPedal (false);
    REQUIRE (e.getStage() == Engine808::Stage::release);
}

TEST_CASE ("Exponential curve moves faster than linear early on")
{
    EngineParams lin, ex;
    lin.glideMs = ex.glideMs = 100.0f;
    ex.glideCurve = GlideCurve::exponential;
    auto a = makeEngine (lin);
    auto b = makeEngine (ex);

    for (auto* e : { &a, &b })
    {
        e->noteOn (36, 1.0f);
        run (*e, 100);
        e->noteOn (48, 1.0f);
        run (*e, msToSamples (20));
    }
    REQUIRE (b.getCurrentPitch() > a.getCurrentPitch());
}

TEST_CASE ("Time per octave scales the glide with the interval")
{
    EngineParams p;
    p.glideMs = 100.0f;
    p.glideTiming = GlideTiming::perOctave;

    auto one = makeEngine (p);
    one.noteOn (36, 1.0f); run (one, 100);
    one.noteOn (48, 1.0f);                 // 1 octave -> 100 ms
    run (one, msToSamples (50));
    REQUIRE_THAT (one.getCurrentPitch(), WithinAbs (42.0, 0.1));

    auto two = makeEngine (p);
    two.noteOn (36, 1.0f); run (two, 100);
    two.noteOn (60, 1.0f);                 // 2 octaves -> 200 ms
    run (two, msToSamples (100));
    REQUIRE_THAT (two.getCurrentPitch(), WithinAbs (48.0, 0.1));
}

TEST_CASE ("Slide retrigger re-hits the punch and optionally the envelope")
{
    EngineParams p;
    p.sustain = 1.0f;

    SECTION ("Off keeps the slide smooth")
    {
        auto e = makeEngine (p);
        e.noteOn (36, 1.0f); run (e, msToSamples (300));
        e.noteOn (40, 1.0f);
        REQUIRE (e.getPitchDropLevel() < 0.01f);
    }
    SECTION ("Re-hit punch")
    {
        p.slideRetrigger = SlideRetrigger::punch;
        auto e = makeEngine (p);
        e.noteOn (36, 1.0f); run (e, msToSamples (300));
        e.noteOn (40, 1.0f);
        REQUIRE (e.isGliding());
        REQUIRE_THAT (e.getPitchDropLevel(), WithinAbs (1.0, 1e-6));
        REQUIRE (e.getStage() != Engine808::Stage::attack);
    }
    SECTION ("Re-hit punch and envelope")
    {
        p.slideRetrigger = SlideRetrigger::punchAndEnvelope;
        auto e = makeEngine (p);
        e.noteOn (36, 1.0f); run (e, msToSamples (300));
        e.noteOn (40, 1.0f);
        REQUIRE (e.isGliding());
        REQUIRE (e.getStage() == Engine808::Stage::attack);
    }
}

TEST_CASE ("Low and high note priority")
{
    EngineParams p;
    p.glideMs = 0.0f;

    SECTION ("Lowest note wins")
    {
        p.notePriority = NotePriority::low;
        auto e = makeEngine (p);
        e.noteOn (40, 1.0f); run (e, 100);
        e.noteOn (45, 1.0f);
        REQUIRE (e.getCurrentNote() == 40);
        e.noteOff (40);
        REQUIRE (e.getCurrentNote() == 45);
    }
    SECTION ("Highest note wins")
    {
        p.notePriority = NotePriority::high;
        auto e = makeEngine (p);
        e.noteOn (45, 1.0f); run (e, 100);
        e.noteOn (40, 1.0f);
        REQUIRE (e.getCurrentNote() == 45);
    }
}

// ---- Envelope ----------------------------------------------------------------

TEST_CASE ("Note decays to idle after release and output is silent")
{
    EngineParams p;
    p.releaseMs = 50.0f;
    auto e = makeEngine (p);
    e.noteOn (36, 1.0f);
    run (e, 1000);
    e.noteOff (36);
    run (e, msToSamples (60));
    REQUIRE (e.getStage() == Engine808::Stage::idle);

    for (auto s : run (e, 256))
        REQUIRE (s == 0.0f);
}

TEST_CASE ("Decay curve shapes the fall to sustain")
{
    auto levelAtHalf = [] (float curve)
    {
        EngineParams p;
        p.attackMs = 0.0f;
        p.decayMs = 100.0f;
        p.sustain = 0.0f;
        p.decayCurve = curve;
        auto e = makeEngine (p);
        e.noteOn (36, 1.0f);
        run (e, msToSamples (50) + 1);
        return e.getEnvelopeLevel();
    };

    const float expo = levelAtHalf (0.0f);
    const float lin  = levelAtHalf (0.5f);
    const float held = levelAtHalf (1.0f);

    REQUIRE (expo < 0.05f);
    REQUIRE_THAT (lin, WithinAbs (0.5, 0.02));
    REQUIRE_THAT (held, WithinAbs (0.875, 0.02));
}

TEST_CASE ("Every-hit phase reset crossfades without a click")
{
    EngineParams p = pureTone();
    p.phaseReset = PhaseReset::everyHit;
    p.glideMode = GlideMode::off;
    p.attackMs = 1.0f;
    auto e = makeEngine (p);

    e.noteOn (36, 1.0f);
    run (e, msToSamples (37.3f)); // stop mid-cycle
    e.noteOn (36, 1.0f);          // retrigger while sounding

    std::vector<float> buf (480);
    e.render (buf.data(), (int) buf.size());

    // 65 Hz sine at full level changes by at most ~0.009 per sample; allow headroom.
    for (size_t i = 1; i < buf.size(); ++i)
        REQUIRE (std::abs (buf[i] - buf[i - 1]) < 0.05f);
}

// ---- Tuning ------------------------------------------------------------------

TEST_CASE ("Every note is in tune within 1 cent")
{
    for (int note : { 24, 33, 36, 45, 57, 69 })
    {
        auto e = makeEngine (pureTone());
        e.noteOn (note, 1.0f);
        run (e, msToSamples (100));
        const auto f = measureFrequency (run (e, (int) sr));
        INFO ("note " << note << " measured " << f);
        REQUIRE (std::abs (cents (f, noteHz (note))) < 1.0);
    }
}

TEST_CASE ("Tuning reference moves every note")
{
    EngineParams p = pureTone();
    p.tuningRefHz = 432.0f;
    auto e = makeEngine (p);
    e.noteOn (45, 1.0f);
    run (e, msToSamples (100));
    REQUIRE (std::abs (cents (measureFrequency (run (e, (int) sr)), noteHz (45, 432.0))) < 1.0);
}

TEST_CASE ("A glide lands in tune")
{
    EngineParams p = pureTone();
    p.glideMs = 80.0f;
    auto e = makeEngine (p);
    e.noteOn (36, 1.0f);
    run (e, msToSamples (100));
    e.noteOn (43, 1.0f);
    run (e, msToSamples (150));
    REQUIRE (std::abs (cents (measureFrequency (run (e, (int) sr)), noteHz (43))) < 1.0);
}

// ---- Modulation and safety -----------------------------------------------------

TEST_CASE ("Vibrato moves the pitch after its delay")
{
    EngineParams p = pureTone();
    p.vibratoCents = 50.0f;
    p.vibratoRateHz = 5.0f;
    p.vibratoDelayMs = 100.0f;
    auto e = makeEngine (p);
    e.noteOn (45, 1.0f);

    run (e, msToSamples (50));
    REQUIRE_THAT (e.getLastFrequency(), WithinAbs (110.0, 0.05)); // still in the delay

    double lo = 1e9, hi = 0.0;
    for (int i = 0; i < 100; ++i)
    {
        run (e, msToSamples (5));
        lo = std::min (lo, e.getLastFrequency());
        hi = std::max (hi, e.getLastFrequency());
    }
    REQUIRE (cents (hi, lo) > 60.0); // ~ +/-50 cents swing
}

TEST_CASE ("Output stays finite and bounded")
{
    EngineParams p;
    p.pitchDropSemis = 48.0f;
    p.subLevel = 1.0f;
    p.clickLevel = 1.0f;
    p.drift = 1.0f;
    p.vibratoCents = 100.0f;

    for (auto type : { ClickType::noise, ClickType::tick, ClickType::knock })
    {
        p.clickType = type;
        auto e = makeEngine (p);
        e.noteOn (127, 1.0f);
        for (auto s : run (e, 4800))
        {
            REQUIRE (std::isfinite (s));
            REQUIRE (std::abs (s) < 4.0f);
        }
    }
}

// ---- Same-pitch overlap, retrigger ramp, click and attack -------------------

TEST_CASE ("An overlapping repeat of the same pitch re-hits and is not cut by the first note-off")
{
    auto e = makeEngine (pureTone());
    e.noteOn (36, 1.0f);
    run (e, msToSamples (200));
    e.noteOn (36, 1.0f);            // the repeat starts before the first note ends
    REQUIRE (e.getStage() == Engine808::Stage::attack); // a new hit, not a silent "slide"
    run (e, msToSamples (50));
    e.noteOff (36);                 // end of the first note
    run (e, msToSamples (500));
    REQUIRE (e.getCurrentNote() == 36);
    REQUIRE (e.getEnvelopeLevel() > 0.5f); // still sounding
    e.noteOff (36);                 // end of the repeat
    run (e, msToSamples (2000));
    REQUIRE (e.getEnvelopeLevel() < 0.01f);
}

TEST_CASE ("A fast retrigger with 0 ms attack never jumps the level")
{
    auto p = pureTone();
    p.attackMs = 0.0f;
    p.decayMs = 300.0f;
    p.sustain = 0.0f;
    p.releaseMs = 20.0f;

    auto maxStep = [] (const std::vector<float>& x)
    {
        float m = 0.0f;
        for (size_t i = 1; i < x.size(); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    };

    // Reference: the steepest a full-level note ever moves.
    auto fresh = makeEngine (p);
    fresh.noteOn (36, 1.0f);
    const float fullLevelStep = maxStep (run (fresh, msToSamples (10)));

    auto e = makeEngine (p);
    e.noteOn (36, 1.0f);
    run (e, msToSamples (150));
    e.noteOff (36);
    run (e, msToSamples (1) - 1);
    const float last = run (e, 1)[0]; // the sample before the retrigger, so a jump at the boundary counts
    e.noteOn (36, 1.0f);              // lands while the level is well below full
    auto out = run (e, msToSamples (10));
    out.insert (out.begin(), last);
    // Allowed: the waveform's own slope plus a rise over the 1 ms minimum ramp.
    // Without the ramp the level would jump by most of full scale in one sample.
    const float rampStep = 1.0f / (float) msToSamples (Engine808::minRetriggerRampMs);
    REQUIRE (maxStep (out) < (fullLevelStep + rampStep) * 1.1f);
}

TEST_CASE ("Click follows a slow attack unless turned off")
{
    auto clickEnergy = [] (bool follows, float attackMs)
    {
        EngineParams p;
        p.pitchDropSemis = 0.0f;
        p.clickLevel = 1.0f;
        p.attackMs = attackMs;
        p.clickFollowsAttack = follows;
        auto e = makeEngine (p);
        e.noteOn (36, 1.0f);
        auto out = run (e, msToSamples (3));
        double sum = 0.0;
        for (auto x : out) sum += (double) x * x;
        return sum;
    };

    REQUIRE (clickEnergy (true, 1.0f) == clickEnergy (false, 1.0f)); // short attacks are untouched
    REQUIRE (clickEnergy (true, 100.0f) < clickEnergy (false, 100.0f) * 0.05);
}
