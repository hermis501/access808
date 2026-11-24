#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Engine808.h"
#include "Parameters.h"
#include "PresetManager.h"

class Access808Processor : public juce::AudioProcessor,
                           public juce::VST3ClientExtensions
{
public:
    Access808Processor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    access808::PresetManager presets { apvts };

    // ---- REAPER / OSARA (see ReaperHost.cpp) ----
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    void setIHostApplication (Steinberg::FUnknown* hostContext) override;

    // OSARA's speech function when running in REAPER with OSARA, else null. Message thread.
    using OsaraOutputMessage = void (*) (const char*);
    OsaraOutputMessage getOsaraOutputMessage() const noexcept { return osaraOutputMessage; }

    // What the kick sidechain is doing, for the editor's "Check Ducking" button.
    struct DuckStatus
    {
        bool signalRecent = false;  // kick audio seen in the last 2 seconds
        float reductionDb = 0.0f;   // largest recent ducking, in dB
        float amount = 0.0f;        // Duck Amount, 0..1
        bool inputConnected = false;
    };
    DuckStatus getDuckStatus() const;

private:
    using Oversampler = juce::dsp::Oversampling<float>;

    // Per-oversampling-rate state, so switching rate never allocates.
    struct SaturationStage
    {
        explicit SaturationStage (size_t stages)
            : oversampler (1, stages, Oversampler::filterHalfBandPolyphaseIIR, true, false) {}

        Oversampler oversampler;
        juce::dsp::LinkwitzRileyFilter<float> crossover; // multiband split
        juce::SmoothedValue<float> drive, mix;
        float tapeState = 0.0f, tapeCoef = 0.0f;
    };

    access808::EngineParams readEngineParams();
    void handleMidi (const juce::MidiMessage&);
    void ensureBufferSizes (int numSamples);
    void processSaturation (SaturationStage&, juce::dsp::AudioBlock<float>& block, const float* baseDriveGain, int satType);
    void selectOversampling();

    access808::Engine808 engine;

    SaturationStage os4 { 2 }, os8 { 3 };
    SaturationStage* activeStage = &os4;

    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::dsp::LinkwitzRileyFilter<float> widthSplit;
    std::vector<float> widthDelay;
    int widthDelayPos = 0, widthDelaySamples = 0;

    juce::AudioBuffer<float> monoBuffer;
    std::vector<float> velocityBuffer, driveGainBuffer, duckBuffer, sideBuffer;

    juce::SmoothedValue<float> outputGain, cutoffSmoothed, resonanceSmoothed, widthSmoothed;
    float dcX1 = 0.0f, dcY1 = 0.0f, dcCoef = 0.999f;
    float duckEnv = 0.0f;

    int currentProgram = 0;
    int preparedBlockSize = 512;

    OsaraOutputMessage osaraOutputMessage = nullptr;

    float widthCrossover = 200.0f;

    std::atomic<juce::int64> samplesProcessed { 0 }, lastSidechainSample { std::numeric_limits<juce::int64>::min() / 2 };
    std::atomic<float> recentReductionDb { 0.0f };

    // Raw parameter pointers, cached once so the audio thread does no lookups.
    struct RawParams
    {
        std::atomic<float> *glideMode, *glideTime, *glideSync, *glideTiming, *glideCurve, *slideRetrigger, *notePriority,
                           *tune, *fine, *tuningRef, *pitchDrop, *pitchDropTime,
                           *attack, *hold, *decay, *decayCurve, *sustain, *release, *phaseReset,
                           *waveShape, *subLevel, *clickType, *clickLevel, *clickTone, *clickFollowsAttack,
                           *saturationType, *drive, *driveCompensation, *saturationMix, *oversampling, *cutoff, *resonance,
                           *vibratoRate, *vibratoDepth, *vibratoDelay, *drift,
                           *velocitySens, *velocityToDrive, *velocityToClick, *bendRange,
                           *stereoWidth, *duckAmount, *duckThreshold, *duckRelease, *output;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Access808Processor)
};
