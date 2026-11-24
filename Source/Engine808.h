#pragma once

// Monophonic 808 voice: note stack with priority, legato glide, pitch-drop
// "punch", AHDSR amp envelope with curve, sine/triangle oscillator, sub
// octave, click transient, vibrato and analog drift.
//
// Deliberately free of JUCE so it can be unit tested in isolation
// (tests/EngineTests.cpp). Saturation, filter, width, ducking and output gain
// live in the processor, after this engine.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace access808
{

enum class GlideMode      { off = 0, legato = 1 };
enum class GlideCurve     { linear = 0, exponential = 1 };
enum class GlideTiming    { constantTime = 0, perOctave = 1 };
enum class SlideRetrigger { off = 0, punch = 1, punchAndEnvelope = 2 };
enum class NotePriority   { last = 0, low = 1, high = 2 };
enum class PhaseReset     { fromSilence = 0, everyHit = 1 };
enum class ClickType      { noise = 0, tick = 1, knock = 2 };

struct EngineParams
{
    GlideMode      glideMode      = GlideMode::legato;
    GlideCurve     glideCurve     = GlideCurve::linear;
    GlideTiming    glideTiming    = GlideTiming::constantTime;
    SlideRetrigger slideRetrigger = SlideRetrigger::off;
    NotePriority   notePriority   = NotePriority::last;
    PhaseReset     phaseReset     = PhaseReset::fromSilence;
    ClickType      clickType      = ClickType::noise;

    float glideMs          = 80.0f;   // per slide, or per octave with GlideTiming::perOctave
    float tuneSemis        = 0.0f;
    float fineCents        = 0.0f;
    float tuningRefHz      = 440.0f;  // frequency of A4
    float pitchDropSemis   = 12.0f;   // each hit starts this far above the note...
    float pitchDropMs      = 40.0f;   // ...and falls to it over roughly this time
    float attackMs         = 1.0f;
    float holdMs           = 0.0f;
    float decayMs          = 1500.0f; // time from full level to the sustain level
    float decayCurve       = 0.0f;    // 0 exponential, 0.5 linear, 1 held
    float sustain          = 0.5f;    // 0..1
    float releaseMs        = 250.0f;  // time to silence
    float waveShape        = 0.0f;    // 0 sine, 1 triangle
    float subLevel         = 0.0f;    // 0..1, sine one octave down
    float clickLevel       = 0.3f;    // 0..1
    float clickTone        = 0.5f;    // 0..1, dark..bright
    bool  clickFollowsAttack = true;  // a slow attack softens the click too
    float velocitySens     = 0.5f;    // 0..1, velocity to loudness
    float velocityToClick  = 0.0f;    // 0..1
    float bendRangeSemis   = 12.0f;
    float vibratoRateHz    = 5.0f;
    float vibratoCents     = 0.0f;    // depth, 0 = off
    float vibratoDelayMs   = 300.0f;  // time after a hit before vibrato fades in
    float drift            = 0.0f;    // 0..1 analog pitch drift
};

class Engine808
{
public:
    enum class Stage { idle, attack, hold, decay, sustain, release };

    static constexpr float maxDriftCents = 8.0f;
    static constexpr float crossfadeMs = 2.0f;
    static constexpr float minRetriggerRampMs = 1.0f; // never jump the level of a sounding note
    static constexpr float clickFullUpToAttackMs = 3.0f;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        const auto sr = (float) sampleRate;
        bendCoef   = std::exp (-1.0f / (0.005f * sr)); // ~5 ms
        smoothCoef = std::exp (-1.0f / (0.010f * sr)); // ~10 ms
        driftCoef  = std::exp (-1.0f / (0.300f * sr)); // ~300 ms
        reset();
    }

    void reset()
    {
        heldCount = 0;
        sustainPedal = false;
        stage = Stage::idle;
        envLevel = 0.0f;
        phase = subPhase = 0.0;
        glideActive = false;
        clickEnv = 0.0f;
        pitchDropEnv = 0.0f;
        bendTarget = bendSmoothed = 0.0f;
        fade = {};
        firstRender = true;
        currentNote = -1;
        noteCounts.fill (0);
        softRamp = false;
    }

    void setParams (const EngineParams& p) { params = p; }

    // ---- MIDI-style input -------------------------------------------------

    void noteOn (int note, float velocity01)
    {
        if (note < 0 || note > 127)
            return;

        const bool gateWasOn = isGateOn();
        const bool samePitchOverlap = noteCounts[(size_t) note] > 0;

        // Overlapping notes of the same pitch (common in REAPER when a note is
        // lengthened over a repeat of itself) are counted, so the first one's
        // note-off doesn't silence the second.
        ++noteCounts[(size_t) note];
        removeHeld (note);
        pushHeld (note);
        const int target = selectNote();

        if (samePitchOverlap && target == note)
        {
            trigger (note, velocity01); // a repeat of the sounding pitch is a new hit, never a silent "slide"
            return;
        }

        if (! gateWasOn)
        {
            trigger (target, velocity01);
            return;
        }

        if (target != note)
            return; // priority keeps the current note (e.g. low priority and a higher key)

        if (params.glideMode == GlideMode::off)
        {
            trigger (target, velocity01);
            return;
        }

        startGlide (target);

        if (params.slideRetrigger != SlideRetrigger::off)
        {
            pitchDropEnv = 1.0f;
            startClick (velocity01);
        }
        if (params.slideRetrigger == SlideRetrigger::punchAndEnvelope)
        {
            stage = Stage::attack; // ramps from the current level: no click
            softRamp = true;
            setVelocity (velocity01);
        }
    }

    void noteOff (int note)
    {
        if (note < 0 || note > 127 || noteCounts[(size_t) note] == 0)
            return;
        if (--noteCounts[(size_t) note] > 0)
            return; // another overlapping copy of this pitch is still held

        removeHeld (note);

        if (heldCount > 0)
        {
            const int target = selectNote();
            if (target != currentNote)
                moveToNote (target); // trill / return slides
            return;
        }

        if (! sustainPedal)
            releaseGate();
    }

    void setSustainPedal (bool down)
    {
        sustainPedal = down;
        if (! down && heldCount == 0)
            releaseGate();
    }

    // value in -1..1
    void setPitchBend (float value) { bendTarget = std::clamp (value, -1.0f, 1.0f); }

    void allNotesOff()
    {
        heldCount = 0;
        noteCounts.fill (0);
        sustainPedal = false;
        releaseGate();
    }

    // ---- Rendering -------------------------------------------------------

    // Writes numSamples mono samples to out. If velocityOut is non-null it
    // receives the velocity (0..1) of the sounding note per sample, used by
    // the processor for velocity-to-drive.
    void render (float* out, float* velocityOut, int numSamples)
    {
        const auto sr = (float) sampleRate;

        if (firstRender)
        {
            tuneSmoothed  = params.tuneSemis + params.fineCents * 0.01f;
            shapeSmoothed = params.waveShape;
            subSmoothed   = params.subLevel;
            sustainSmoothed = params.sustain;
            firstRender = false;
        }

        const float dropCoef   = std::exp (-6.9078f / (std::max (params.pitchDropMs, 0.1f) * 0.001f * sr));
        const float attackInc  = params.attackMs > 0.0f ? 1.0f / (params.attackMs * 0.001f * sr) : 1.0f;
        const float softRampInc = std::min (attackInc, 1.0f / (minRetriggerRampMs * 0.001f * sr));
        const float decayInc   = 1.0f / (std::max (params.decayMs, 1.0f) * 0.001f * sr);
        const float releaseInc = 1.0f / (std::max (params.releaseMs, 1.0f) * 0.001f * sr);
        const int   holdLen    = (int) (params.holdMs * 0.001f * sr);
        const float tuneTarget = params.tuneSemis + params.fineCents * 0.01f;
        const float shapeTarget = std::clamp (params.waveShape, 0.0f, 1.0f);
        const float sustainTarget = std::clamp (params.sustain, 0.0f, 1.0f);
        const float fadeDec    = 1.0f / (crossfadeMs * 0.001f * sr);
        const float vibInc     = params.vibratoRateHz / sr;
        const int   vibDelay   = (int) (params.vibratoDelayMs * 0.001f * sr);
        const float vibFadeInc = 1.0f / (0.25f * sr); // vibrato fades in over 250 ms
        const int   driftEvery = (int) (0.25f * sr);

        ClickSetup cs = clickSetup (sr);

        for (int i = 0; i < numSamples; ++i)
        {
            if (velocityOut != nullptr)
                velocityOut[i] = velocity;

            if (stage == Stage::idle && ! fade.active)
            {
                out[i] = 0.0f;
                continue;
            }

            // ---- smoothed controls ----
            tuneSmoothed    = tuneTarget    + (tuneSmoothed    - tuneTarget)    * smoothCoef;
            shapeSmoothed   = shapeTarget   + (shapeSmoothed   - shapeTarget)   * smoothCoef;
            subSmoothed     = params.subLevel + (subSmoothed   - params.subLevel) * smoothCoef;
            sustainSmoothed = sustainTarget + (sustainSmoothed - sustainTarget) * smoothCoef;
            bendSmoothed    = bendTarget    + (bendSmoothed    - bendTarget)    * bendCoef;

            // ---- pitch ----
            if (glideActive)
            {
                glidePos = std::min (glidePos + glideInc, 1.0f);
                const float t = params.glideCurve == GlideCurve::linear
                              ? glidePos
                              : (1.0f - std::exp (-5.0f * glidePos)) / (1.0f - std::exp (-5.0f));
                currentPitch = glideStart + (glideTarget - glideStart) * t;
                if (glidePos >= 1.0f)
                {
                    currentPitch = glideTarget;
                    glideActive = false;
                }
            }

            pitchDropEnv *= dropCoef;

            // vibrato
            float vib = 0.0f;
            if (params.vibratoCents > 0.0f)
            {
                if (noteAge >= vibDelay)
                    vibFade = std::min (vibFade + vibFadeInc, 1.0f);
                vib = std::sin (twoPiF * vibPhase) * params.vibratoCents * 0.01f * vibFade;
            }
            vibPhase += vibInc;
            if (vibPhase >= 1.0f) vibPhase -= 1.0f;
            ++noteAge;

            // drift
            if (params.drift > 0.0f)
            {
                if (++driftCounter >= driftEvery)
                {
                    driftCounter = 0;
                    driftTarget = nextNoise();
                }
                driftValue = driftTarget + (driftValue - driftTarget) * driftCoef;
            }
            const float driftSemis = params.drift * (driftValue * maxDriftCents + noteDriftCents) * 0.01f;

            const float pitch = currentPitch + tuneSmoothed + vib + driftSemis
                              + pitchDropEnv * params.pitchDropSemis
                              + bendSmoothed * params.bendRangeSemis;
            const double freq = std::min ((double) params.tuningRefHz * std::exp2 ((pitch - 69.0f) / 12.0),
                                          sampleRate * 0.45);
            lastFreq = freq;

            // ---- main oscillator ----
            float osc = oscillator (phase, subPhase, shapeSmoothed, subSmoothed);
            advance (phase, subPhase, freq);

            // ---- envelope ----
            switch (stage)
            {
                case Stage::attack:
                    envLevel += softRamp ? softRampInc : attackInc;
                    if (envLevel >= 1.0f) { envLevel = 1.0f; stage = Stage::hold; holdCounter = holdLen; softRamp = false; }
                    break;
                case Stage::hold:
                    if (--holdCounter <= 0) { stage = Stage::decay; decayPos = 0.0f; }
                    break;
                case Stage::decay:
                    decayPos = std::min (decayPos + decayInc, 1.0f);
                    envLevel = sustainSmoothed + (1.0f - sustainSmoothed) * decayShape (decayPos, params.decayCurve);
                    if (decayPos >= 1.0f) stage = Stage::sustain;
                    break;
                case Stage::sustain:
                    envLevel = sustainSmoothed;
                    break;
                case Stage::release:
                    releasePos = std::min (releasePos + releaseInc, 1.0f);
                    envLevel = releaseStart * expoShape (releasePos);
                    break;
                case Stage::idle:
                    envLevel = 0.0f;
                    break;
            }

            float sample = osc * envLevel * velocityGain;

            // ---- click ----
            if (clickEnv > 1.0e-5f)
            {
                sample += renderClick (cs) * clickEnv * clickGain;
                clickEnv *= cs.decay;
            }

            // ---- crossfade out of the previous hit (phase reset) ----
            if (fade.active)
            {
                sample += oscillator (fade.phase, fade.subPhase, shapeSmoothed, subSmoothed) * fade.level * fade.gain;
                advance (fade.phase, fade.subPhase, fade.freq);
                fade.gain -= fadeDec;
                if (fade.gain <= 0.0f) fade.active = false;
            }

            out[i] = sample;

            const bool envDone = (stage == Stage::release && releasePos >= 1.0f)
                              || (stage == Stage::sustain && envLevel < 1.0e-5f);
            if (envDone && clickEnv <= 1.0e-5f)
            {
                stage = Stage::idle;
                envLevel = 0.0f;
                glideActive = false;
            }
        }
    }

    void render (float* out, int numSamples) { render (out, nullptr, numSamples); }

    // ---- State inspection (tests, UI) -------------------------------------

    float getCurrentPitch() const noexcept   { return currentPitch; }
    float getGlideTarget() const noexcept    { return glideActive ? glideTarget : currentPitch; }
    bool  isGliding() const noexcept         { return glideActive; }
    Stage getStage() const noexcept          { return stage; }
    float getEnvelopeLevel() const noexcept  { return envLevel; }
    int   getHeldCount() const noexcept      { return heldCount; }
    int   getCurrentNote() const noexcept    { return currentNote; }
    float getPitchDropLevel() const noexcept { return pitchDropEnv; }
    double getLastFrequency() const noexcept { return lastFreq; }

private:
    static constexpr double twoPi = 6.283185307179586;
    static constexpr float twoPiF = 6.2831853f;

    struct ClickSetup { float decay, lpA, freq, freqDecay; };

    struct Fade
    {
        bool active = false;
        double phase = 0.0, subPhase = 0.0, freq = 0.0;
        float level = 0.0f, gain = 0.0f;
    };

    bool isGateOn() const { return stage != Stage::idle && stage != Stage::release; }

    static float oscillator (double ph, double subPh, float shape, float sub)
    {
        const float sine = (float) std::sin (twoPi * ph);
        const float p = (float) ph;
        const float tri = p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        return sine + (tri - sine) * shape + (float) std::sin (twoPi * subPh) * sub;
    }

    void advance (double& ph, double& subPh, double freq) const
    {
        ph += freq / sampleRate;
        if (ph >= 1.0) ph -= 1.0;
        subPh += 0.5 * freq / sampleRate;
        if (subPh >= 1.0) subPh -= 1.0;
    }

    // Remaining fraction (1 -> 0) for t in 0..1.
    static float expoShape (float t)
    {
        constexpr float k = 6.9078f; // -60 dB
        static const float floorV = std::exp (-k);
        return (std::exp (-k * t) - floorV) / (1.0f - floorV);
    }

    static float decayShape (float t, float curve)
    {
        const float e = expoShape (t);
        const float l = 1.0f - t;
        const float h = 1.0f - t * t * t;
        curve = std::clamp (curve, 0.0f, 1.0f);
        return curve < 0.5f ? e + (l - e) * (curve * 2.0f)
                            : l + (h - l) * ((curve - 0.5f) * 2.0f);
    }

    ClickSetup clickSetup (float sr) const
    {
        const float tone = std::clamp (params.clickTone, 0.0f, 1.0f);
        switch (params.clickType)
        {
            case ClickType::tick:
                return { std::exp (-6.9078f / (0.0015f * sr)), 0.0f, 2000.0f * std::pow (4.0f, tone), 1.0f };
            case ClickType::knock:
                return { std::exp (-6.9078f / (0.008f * sr)), 0.0f, 300.0f * std::pow (5.0f, tone),
                         std::exp (std::log (0.5f) / (0.008f * sr)) };
            case ClickType::noise:
            default:
            {
                const float cut = 1000.0f * std::pow (12.0f, tone); // 1k..12k Hz
                return { std::exp (-6.9078f / (0.004f * sr)), 1.0f - std::exp (-twoPiF * cut / sr), 0.0f, 1.0f };
            }
        }
    }

    float renderClick (const ClickSetup& cs)
    {
        if (params.clickType == ClickType::noise)
        {
            clickLp += cs.lpA * (nextNoise() - clickLp);
            return clickLp * 1.5f;
        }

        const float s = std::sin (twoPiF * clickPhase);
        clickPhase += clickFreq / (float) sampleRate;
        if (clickPhase >= 1.0f) clickPhase -= 1.0f;
        clickFreq *= cs.freqDecay;
        return s;
    }

    void startClick (float velocity01)
    {
        const float v = std::clamp (velocity01, 0.0f, 1.0f);
        clickEnv = 1.0f;
        clickLp = 0.0f;
        clickPhase = 0.0f;
        clickFreq = clickSetup ((float) sampleRate).freq;
        clickGain = params.clickLevel * (1.0f - params.velocityToClick + params.velocityToClick * v);

        // A slow attack would otherwise leave a lone click before the fade-in.
        if (params.clickFollowsAttack && params.attackMs > clickFullUpToAttackMs)
            clickGain *= clickFullUpToAttackMs / params.attackMs;
    }

    void setVelocity (float velocity01)
    {
        velocity = std::clamp (velocity01, 0.0f, 1.0f);
        velocityGain = 1.0f - params.velocitySens + params.velocitySens * velocity;
    }

    void trigger (int note, float velocity01)
    {
        const bool sounding = stage != Stage::idle && envLevel > 1.0e-4f;

        if (sounding && params.phaseReset == PhaseReset::everyHit)
        {
            fade.active = true;
            fade.phase = phase;
            fade.subPhase = subPhase;
            fade.freq = lastFreq;
            fade.level = envLevel * velocityGain;
            fade.gain = 1.0f;
            envLevel = 0.0f;
            phase = subPhase = 0.0;
        }
        else if (! sounding)
        {
            phase = subPhase = 0.0; // consistent punch from silence
        }
        // otherwise: phase continues and the attack ramps from the current level,
        // at least over minRetriggerRampMs so an attack of 0 ms can't jump the level.
        softRamp = sounding && ! fade.active;

        currentNote = note;
        currentPitch = (float) note;
        glideActive = false;
        pitchDropEnv = 1.0f;
        stage = Stage::attack;
        noteAge = 0;
        vibFade = 0.0f;
        vibPhase = 0.0f;
        noteDriftCents = nextNoise() * 3.0f;

        setVelocity (velocity01);
        startClick (velocity01);
    }

    void startGlide (int targetNote)
    {
        currentNote = targetNote;
        const float target = (float) targetNote;

        float ms = params.glideMs;
        if (params.glideTiming == GlideTiming::perOctave)
            ms *= std::abs (target - currentPitch) / 12.0f;

        if (ms <= 0.0f || target == currentPitch)
        {
            currentPitch = target;
            glideActive = false;
            return;
        }
        glideStart = currentPitch;
        glideTarget = target;
        glidePos = 0.0f;
        glideInc = 1.0f / (ms * 0.001f * (float) sampleRate);
        glideActive = true;
    }

    void moveToNote (int note)
    {
        if (params.glideMode == GlideMode::legato)
            startGlide (note);
        else
        {
            currentNote = note;
            currentPitch = (float) note;
            glideActive = false;
        }
    }

    void releaseGate()
    {
        if (stage != Stage::idle && stage != Stage::release)
        {
            stage = Stage::release;
            releaseStart = envLevel;
            releasePos = 0.0f;
        }
    }

    int selectNote() const
    {
        if (heldCount == 0) return currentNote;
        const auto* begin = held.data();
        const auto* end = held.data() + heldCount;
        switch (params.notePriority)
        {
            case NotePriority::low:  return *std::min_element (begin, end);
            case NotePriority::high: return *std::max_element (begin, end);
            case NotePriority::last:
            default:                 return held[(size_t) heldCount - 1];
        }
    }

    void pushHeld (int note)
    {
        if (heldCount < (int) held.size())
            held[(size_t) heldCount++] = note;
    }

    void removeHeld (int note)
    {
        auto* end = held.data() + heldCount;
        auto* newEnd = std::remove (held.data(), end, note);
        heldCount = (int) (newEnd - held.data());
    }

    float nextNoise()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) rng * (2.0f / 4294967295.0f) - 1.0f;
    }

    EngineParams params;
    double sampleRate = 44100.0;

    std::array<int, 128> held {};
    std::array<std::uint8_t, 128> noteCounts {};
    int heldCount = 0;
    bool softRamp = false;
    bool sustainPedal = false;
    int currentNote = -1;

    Stage stage = Stage::idle;
    float envLevel = 0.0f;
    int holdCounter = 0;
    float decayPos = 0.0f, releasePos = 0.0f, releaseStart = 0.0f;
    float velocity = 1.0f, velocityGain = 1.0f;

    float currentPitch = 36.0f;
    float glideStart = 36.0f, glideTarget = 36.0f, glidePos = 0.0f, glideInc = 0.0f;
    bool glideActive = false;

    float pitchDropEnv = 0.0f;
    float bendTarget = 0.0f, bendSmoothed = 0.0f, bendCoef = 0.0f;

    float smoothCoef = 0.0f, tuneSmoothed = 0.0f, shapeSmoothed = 0.0f, subSmoothed = 0.0f, sustainSmoothed = 0.5f;
    bool firstRender = true;

    float vibPhase = 0.0f, vibFade = 0.0f;
    int noteAge = 0;

    float driftCoef = 0.0f, driftValue = 0.0f, driftTarget = 0.0f, noteDriftCents = 0.0f;
    int driftCounter = 0;

    double phase = 0.0, subPhase = 0.0, lastFreq = 65.4;
    Fade fade;

    float clickEnv = 0.0f, clickLp = 0.0f, clickPhase = 0.0f, clickFreq = 0.0f, clickGain = 0.0f;
    std::uint32_t rng = 0x12345678u;
};

} // namespace access808
