#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace access808;

namespace
{
    constexpr float multibandCrossoverHz = 150.0f;
    // The width band starts just above the fundamental, so the sub stays mono
    // while the harmonics that carry the 808's character can spread.
    constexpr float widthCrossoverMinHz = 60.0f, widthCrossoverMaxHz = 300.0f, widthCrossoverRatio = 1.5f;
    constexpr float widthSideGain = 1.0f;
    constexpr float sidechainPresentLevel = 0.003f; // about -50 dBFS
    constexpr float widthDelayMs = 12.0f;

    // Rough loudness compensation for a saturator driven with gain g, tuned for
    // a full-scale-ish 808 (peak around 0.7). Approximate by design.
    float driveCompensation (float g)
    {
        if (g <= 1.0f) return 1.0f;
        return std::tanh (0.7f) / std::tanh (0.7f * g) * (1.0f - 0.25f * (1.0f - 1.0f / g));
    }

    template <typename T>
    T load (std::atomic<T>* p) { return p->load (std::memory_order_relaxed); }
}

Access808Processor::Access808Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Sidechain (kick)", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Access808", createParameterLayout())
{
    auto get = [this] (const char* id)
    {
        auto* ptr = apvts.getRawParameterValue (id);
        jassert (ptr != nullptr);
        return ptr;
    };

    raw = { get (ids::glideMode), get (ids::glideTime), get (ids::glideSync), get (ids::glideTiming),
            get (ids::glideCurve), get (ids::slideRetrigger), get (ids::notePriority),
            get (ids::tune), get (ids::fine), get (ids::tuningRef), get (ids::pitchDrop), get (ids::pitchDropTime),
            get (ids::pitchDropCurve),
            get (ids::attack), get (ids::hold), get (ids::decay), get (ids::decaySync), get (ids::decayCurve),
            get (ids::sustain), get (ids::release), get (ids::phaseReset),
            get (ids::waveShape), get (ids::harmonics), get (ids::subLevel), get (ids::clickType), get (ids::clickLevel), get (ids::clickTone),
            get (ids::clickFollowsAttack),
            get (ids::saturationType), get (ids::drive), get (ids::driveCompensation), get (ids::saturationMix),
            get (ids::oversampling), get (ids::cutoff), get (ids::resonance),
            get (ids::vibratoRate), get (ids::vibratoDepth), get (ids::vibratoDelay), get (ids::drift),
            get (ids::velocitySens), get (ids::velocityToDrive), get (ids::velocityToClick), get (ids::velocityToPitchDrop),
            get (ids::bendRange),
            get (ids::stereoWidth), get (ids::duckAmount), get (ids::duckThreshold), get (ids::duckRelease),
            get (ids::output) };
}

bool Access808Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    const bool inOk = in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    return outOk && inOk;
}

void Access808Processor::ensureBufferSizes (int numSamples)
{
    if (monoBuffer.getNumSamples() < numSamples)
        monoBuffer.setSize (1, numSamples, false, false, true);
    for (auto* v : { &velocityBuffer, &driveGainBuffer, &duckBuffer, &sideBuffer })
        if ((int) v->size() < numSamples)
            v->resize ((size_t) numSamples);
}

void Access808Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    preparedBlockSize = juce::jmax (1, samplesPerBlock);
    ensureBufferSizes (samplesPerBlock);

    for (auto* stage : { &os4, &os8 })
    {
        stage->oversampler.reset();
        stage->oversampler.initProcessing ((size_t) samplesPerBlock);
        const double osRate = sampleRate * (double) stage->oversampler.getOversamplingFactor();

        stage->crossover.prepare ({ osRate, (juce::uint32) samplesPerBlock * 8, 1 });
        stage->crossover.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        stage->crossover.setCutoffFrequency (multibandCrossoverHz);

        stage->drive.reset (osRate, 0.02);
        stage->mix.reset (osRate, 0.02);
        stage->drive.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (load (raw.drive)));
        stage->mix.setCurrentAndTargetValue (load (raw.saturationMix));
        stage->tapeCoef = 1.0f - (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 10000.0 / osRate);
        stage->tapeState = 0.0f;
    }
    activeStage = nullptr;
    selectOversampling();

    filter.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 1 });
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    filter.reset();

    widthSplit.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 1 });
    widthSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    widthCrossover = 200.0f;
    widthSplit.setCutoffFrequency (widthCrossover);
    widthDelaySamples = juce::jmax (1, juce::roundToInt (sampleRate * widthDelayMs * 0.001));
    widthDelay.assign ((size_t) widthDelaySamples, 0.0f);
    widthDelayPos = 0;

    const double smoothTime = 0.02;
    for (auto* s : { &outputGain, &cutoffSmoothed, &resonanceSmoothed, &widthSmoothed })
        s->reset (sampleRate, smoothTime);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (load (raw.output)));
    cutoffSmoothed.setCurrentAndTargetValue (load (raw.cutoff));
    resonanceSmoothed.setCurrentAndTargetValue (load (raw.resonance));
    widthSmoothed.setCurrentAndTargetValue (load (raw.stereoWidth));

    dcCoef = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 5.0 / sampleRate); // 5 Hz high-pass
    dcX1 = dcY1 = 0.0f;
    duckEnv = 0.0f;
}

void Access808Processor::selectOversampling()
{
    const int mode = (int) load (raw.oversampling);
    const bool want8x = mode == 1 || (mode == 2 && isNonRealtime());
    auto* wanted = want8x ? &os8 : &os4;

    if (wanted == activeStage)
        return;

    wanted->oversampler.reset();
    wanted->crossover.reset();
    wanted->tapeState = 0.0f;
    activeStage = wanted;
    setLatencySamples (juce::roundToInt (activeStage->oversampler.getLatencyInSamples()));
}

EngineParams Access808Processor::readEngineParams()
{
    EngineParams e;
    e.glideMode      = (GlideMode)      (int) load (raw.glideMode);
    e.glideCurve     = (GlideCurve)     (int) load (raw.glideCurve);
    e.glideTiming    = (GlideTiming)    (int) load (raw.glideTiming);
    e.slideRetrigger = (SlideRetrigger) (int) load (raw.slideRetrigger);
    e.notePriority   = (NotePriority)   (int) load (raw.notePriority);
    e.phaseReset     = (PhaseReset)     (int) load (raw.phaseReset);
    e.clickType      = (ClickType)      (int) load (raw.clickType);

    // Tempo for the synced times; 120 BPM when the host gives none.
    auto beatMs = [this]
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    bpm = juce::jlimit (20.0, 999.0, *b);
        return 60000.0 / bpm;
    };

    e.glideMs = load (raw.glideTime);
    const int sync = (int) load (raw.glideSync);
    if (sync > 0 && sync < (int) std::size (glideSyncBeats))
        e.glideMs = (float) (glideSyncBeats[sync] * beatMs());

    e.tuneSemis       = load (raw.tune);
    e.fineCents       = load (raw.fine);
    e.tuningRefHz     = load (raw.tuningRef);
    e.pitchDropSemis  = load (raw.pitchDrop);
    e.pitchDropMs     = load (raw.pitchDropTime);
    e.pitchDropCurve  = load (raw.pitchDropCurve);
    e.attackMs        = load (raw.attack);
    e.holdMs          = load (raw.hold);
    e.decayMs         = load (raw.decay);
    const int decaySync = (int) load (raw.decaySync);
    if (decaySync > 0 && decaySync < (int) std::size (decaySyncBeats))
        e.decayMs = (float) (decaySyncBeats[decaySync] * beatMs());
    e.decayCurve      = load (raw.decayCurve);
    e.sustain         = load (raw.sustain);
    e.releaseMs       = load (raw.release);
    e.waveShape       = load (raw.waveShape);
    e.harmonics       = load (raw.harmonics);
    e.subLevel        = load (raw.subLevel);
    e.clickLevel      = load (raw.clickLevel);
    e.clickTone       = load (raw.clickTone);
    e.clickFollowsAttack = load (raw.clickFollowsAttack) >= 0.5f;
    e.velocitySens    = load (raw.velocitySens);
    e.velocityToClick = load (raw.velocityToClick);
    e.velocityToPitchDrop = load (raw.velocityToPitchDrop);
    e.bendRangeSemis  = load (raw.bendRange);
    e.vibratoRateHz   = load (raw.vibratoRate);
    e.vibratoCents    = load (raw.vibratoDepth);
    e.vibratoDelayMs  = load (raw.vibratoDelay);
    e.drift           = load (raw.drift);
    return e;
}

void Access808Processor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isSustainPedalOn())
        engine.setSustainPedal (true);
    else if (m.isSustainPedalOff())
        engine.setSustainPedal (false);
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
}

void Access808Processor::processSaturation (SaturationStage& st, juce::dsp::AudioBlock<float>& block,
                                            const float* baseDriveGain, int satType)
{
    auto* d = block.getChannelPointer (0);
    const auto n = block.getNumSamples();
    const auto factor = st.oversampler.getOversamplingFactor();
    const bool compensate = load (raw.driveCompensation) >= 0.5f;

    st.mix.setTargetValue (load (raw.saturationMix));

    // Bit depth for the crusher follows the Drive knob: 16 bits at 0 dB, 4 bits at 36 dB.
    const float bits = 16.0f - 12.0f * juce::jlimit (0.0f, 1.0f, load (raw.drive) / 36.0f);
    const float q = std::exp2 (bits - 1.0f);

    for (size_t i = 0; i < n; ++i)
    {
        st.drive.setTargetValue (baseDriveGain[i / factor]);
        const float g = st.drive.getNextValue();
        const float m = st.mix.getNextValue();
        const float comp = compensate ? driveCompensation (g) : 1.0f;
        const float dry = d[i];
        const float x = dry * g;
        float wet;

        switch (satType)
        {
            case 1:  wet = juce::jlimit (-1.0f, 1.0f, x) * comp; break;                                   // hard clip
            case 2:  wet = (x >= 0.0f ? std::tanh (x) : std::tanh (0.6f * x) / 0.6f) * comp; break;        // tube
            case 3:                                                                                         // tape
            {
                const float y = std::atan (x) * 0.7f;
                st.tapeState += st.tapeCoef * (y - st.tapeState);
                wet = st.tapeState * comp * 1.3f;
                break;
            }
            case 4:  wet = std::sin (x) * comp; break;                                                     // wavefolder
            case 5:  wet = std::round (std::tanh (x) * q) / q * comp; break;                               // bit crush
            case 6:                                                                                         // multiband
            {
                float low = 0.0f, high = 0.0f;
                st.crossover.processSample (0, dry, low, high);
                wet = low + std::tanh (high * g) * comp;
                break;
            }
            default: wet = std::tanh (x) * comp; break;                                                    // soft clip
        }

        d[i] = dry + (wet - dry) * m;
    }
}

void Access808Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    ensureBufferSizes (numSamples); // allocates only if the host exceeds the prepared size
    selectOversampling();

    // ---- sidechain (kick) envelope, read before the buffer is overwritten ----
    {
        const int inChannels = getTotalNumInputChannels();
        const float amount = load (raw.duckAmount);
        const float threshold = juce::Decibels::decibelsToGain (load (raw.duckThreshold));
        const float sr = (float) getSampleRate();
        const float attackCoef = std::exp (-1.0f / (0.001f * sr));
        const float releaseCoef = std::exp (-1.0f / (load (raw.duckRelease) * 0.001f * sr));
        float blockLevel = 0.0f, minDuck = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float level = 0.0f;
            for (int ch = 0; ch < inChannels; ++ch)
                level = juce::jmax (level, std::abs (buffer.getSample (ch, i)));

            const float coef = level > duckEnv ? attackCoef : releaseCoef;
            duckEnv = level + (duckEnv - level) * coef;
            duckBuffer[(size_t) i] = 1.0f - amount * juce::jmin (1.0f, duckEnv / threshold);
            blockLevel = juce::jmax (blockLevel, level);
            minDuck = juce::jmin (minDuck, duckBuffer[(size_t) i]);
        }

        // Status for the editor: was there kick audio recently, and how much did it duck?
        const auto now = samplesProcessed.load (std::memory_order_relaxed);
        if (blockLevel > sidechainPresentLevel)
            lastSidechainSample.store (now, std::memory_order_relaxed);
        const float reduction = -juce::Decibels::gainToDecibels (juce::jmax (minDuck, 1.0e-4f));
        const float decayed = recentReductionDb.load (std::memory_order_relaxed) - 10.0f * (float) numSamples / sr; // 10 dB/s
        recentReductionDb.store (juce::jmax (reduction, decayed, 0.0f), std::memory_order_relaxed);
        samplesProcessed.store (now + numSamples, std::memory_order_relaxed);
    }

    engine.setParams (readEngineParams());

    // ---- engine, sample-accurate MIDI ----
    auto* mono = monoBuffer.getWritePointer (0);
    auto* vel = velocityBuffer.data();
    int pos = 0;
    for (const auto metadata : midi)
    {
        const int eventPos = juce::jlimit (0, numSamples, metadata.samplePosition);
        if (eventPos > pos)
        {
            engine.render (mono + pos, vel + pos, eventPos - pos);
            pos = eventPos;
        }
        handleMidi (metadata.getMessage());
    }
    if (pos < numSamples)
        engine.render (mono + pos, vel + pos, numSamples - pos);

    // ---- per-sample drive (velocity to drive) ----
    {
        const float driveDb = load (raw.drive);
        const float velToDrive = load (raw.velocityToDrive);
        float lastVel = -1.0f, lastGain = 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            if (vel[i] != lastVel)
            {
                lastVel = vel[i];
                const float db = juce::jmax (0.0f, driveDb + velToDrive * maxVelocityDriveCutDb * (lastVel - 1.0f));
                lastGain = juce::Decibels::decibelsToGain (db);
            }
            driveGainBuffer[(size_t) i] = lastGain;
        }
    }

    // ---- saturation (oversampled) ----
    // Chunked so a host block larger than the prepared size can't overrun the oversampler.
    for (int start = 0; start < numSamples; start += preparedBlockSize)
    {
        const int len = juce::jmin (preparedBlockSize, numSamples - start);
        float* chunk[] = { mono + start };
        juce::dsp::AudioBlock<float> monoBlock (chunk, 1, (size_t) len);
        auto upBlock = activeStage->oversampler.processSamplesUp (monoBlock);
        processSaturation (*activeStage, upBlock, driveGainBuffer.data() + start, (int) load (raw.saturationType));
        activeStage->oversampler.processSamplesDown (monoBlock);
    }

    // ---- DC blocker, filter, stereo width, ducking, output gain ----
    cutoffSmoothed.setTargetValue (load (raw.cutoff));
    resonanceSmoothed.setTargetValue (load (raw.resonance));
    widthSmoothed.setTargetValue (load (raw.stereoWidth));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (load (raw.output)));

    const float nyquistLimit = (float) getSampleRate() * 0.45f;
    auto* side = sideBuffer.data();

    {
        const float target = juce::jlimit (widthCrossoverMinHz, widthCrossoverMaxHz,
                                           (float) engine.getLastFrequency() * widthCrossoverRatio);
        widthCrossover += 0.3f * (target - widthCrossover);
        widthSplit.setCutoffFrequency (widthCrossover);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[i];
        const float y = x - dcX1 + dcCoef * dcY1;
        dcX1 = x;
        dcY1 = y;

        if (cutoffSmoothed.isSmoothing() || resonanceSmoothed.isSmoothing() || i == 0)
        {
            filter.setCutoffFrequency (juce::jmin (cutoffSmoothed.getNextValue(), nyquistLimit));
            filter.setResonance (0.707f + resonanceSmoothed.getNextValue() * 7.0f);
        }

        const float mid = filter.processSample (0, y);

        // Width: side = delayed upper band only; L+R = 2*mid, so mono stays exact.
        float low = 0.0f, high = 0.0f;
        widthSplit.processSample (0, mid, low, high);
        const float delayed = widthDelay[(size_t) widthDelayPos];
        widthDelay[(size_t) widthDelayPos] = high;
        if (++widthDelayPos >= widthDelaySamples) widthDelayPos = 0;

        const float gain = outputGain.getNextValue() * duckBuffer[(size_t) i];
        mono[i] = mid * gain;
        side[i] = delayed * widthSideGain * widthSmoothed.getNextValue() * gain;
    }

    const int outChannels = getTotalNumOutputChannels();
    if (outChannels >= 2)
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            l[i] = mono[i] + side[i];
            r[i] = mono[i] - side[i];
        }
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);
    }
    else if (outChannels == 1)
    {
        buffer.copyFrom (0, 0, monoBuffer, 0, 0, numSamples);
    }
}

Access808Processor::DuckStatus Access808Processor::getDuckStatus() const
{
    DuckStatus st;
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    st.signalRecent = (double) (samplesProcessed.load() - lastSidechainSample.load()) < 2.0 * sr;
    st.reductionDb = recentReductionDb.load();
    st.amount = load (raw.duckAmount);
    st.inputConnected = getTotalNumInputChannels() > 0;
    return st;
}

// ---- Programs (factory presets) ----

int Access808Processor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

const juce::String Access808Processor::getProgramName (int index)
{
    const auto& factory = getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) factory.size()) ? factory[(size_t) index].name : "";
}

void Access808Processor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;
    presets.applyFactory (index);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

// ---- State ----

void Access808Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("presetName", presets.getCurrentName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Access808Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = state.getProperty ("program", 0);
            presets.setCurrentName (state.getProperty ("presetName", "Init").toString());
            apvts.replaceState (state);
            updateHostDisplay (ChangeDetails().withProgramChanged (true));
        }
}

juce::AudioProcessorEditor* Access808Processor::createEditor()
{
    return new Access808Editor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Access808Processor();
}
