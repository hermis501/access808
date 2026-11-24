// Offline tests of the whole processor: every saturation type, oversampling,
// drive compensation, ducking, stereo width and state save/load.
// Links against the plugin's shared-code target.

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"
#include "Presets.h"

using namespace access808;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    void setParam (Access808Processor& p, const char* id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    struct Result
    {
        std::vector<float> left, right;
        float rms() const
        {
            double sum = 0.0;
            for (auto s : left) sum += (double) s * s;
            return (float) std::sqrt (sum / (double) juce::jmax ((size_t) 1, left.size()));
        }
        float peak() const
        {
            float m = 0.0f;
            for (size_t i = 0; i < left.size(); ++i)
                m = juce::jmax (m, std::abs (left[i]), std::abs (right[i]));
            return m;
        }
        bool finite() const
        {
            for (size_t i = 0; i < left.size(); ++i)
                if (! std::isfinite (left[i]) || ! std::isfinite (right[i])) return false;
            return true;
        }
    };

    // Plays one note for `seconds`. `kick` fills the sidechain input with a
    // loud 50 Hz burst in the first 50 ms of every 250 ms.
    Result play (Access808Processor& p, float seconds, int note = 36, bool kick = false, int measureFromBlock = 8)
    {
        p.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buffer (2, block);
        Result r;
        const int blocks = (int) (seconds * sr / block);
        int sample = 0;

        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            if (kick)
                for (int i = 0; i < block; ++i)
                {
                    const int t = (sample + i) % (int) (0.25 * sr);
                    const float v = t < (int) (0.05 * sr) ? 0.9f * std::sin (6.2831853f * 50.0f * (float) t / (float) sr) : 0.0f;
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);

            p.processBlock (buffer, midi);

            if (b >= measureFromBlock)
                for (int i = 0; i < block; ++i)
                {
                    r.left.push_back (buffer.getSample (0, i));
                    r.right.push_back (buffer.getSample (1, i));
                }
            sample += block;
        }
        return r;
    }

    std::unique_ptr<Access808Processor> makeProcessor()
    {
        auto p = std::make_unique<Access808Processor>();
        p->setRateAndBufferSizeDetails (sr, block);
        setParam (*p, ids::sustain, 1.0f);
        setParam (*p, ids::clickLevel, 0.0f);
        return p;
    }
}

TEST_CASE ("Every saturation type gives finite, bounded output at full drive")
{
    for (int type = 0; type < 7; ++type)
        for (int os : { 0, 1 })
        {
            auto p = makeProcessor();
            setParam (*p, ids::saturationType, (float) type);
            setParam (*p, ids::oversampling, (float) os);
            setParam (*p, ids::drive, 36.0f);
            setParam (*p, ids::output, 0.0f);
            const auto r = play (*p, 0.5f);
            INFO ("type " << type << " oversampling " << os);
            REQUIRE (r.finite());
            REQUIRE (r.peak() < 4.0f);
            REQUIRE (r.rms() > 0.01f);
        }
}

TEST_CASE ("Drive compensation keeps loudness within a few dB")
{
    auto rmsAt = [] (float driveDb, bool comp)
    {
        auto p = makeProcessor();
        setParam (*p, ids::drive, driveDb);
        setParam (*p, ids::driveCompensation, comp ? 1.0f : 0.0f);
        setParam (*p, ids::output, 0.0f);
        return play (*p, 0.5f).rms();
    };

    const float clean = rmsAt (0.0f, true);
    const float hotComp = rmsAt (30.0f, true);
    const float hotRaw = rmsAt (30.0f, false);

    const float compDiffDb = std::abs (juce::Decibels::gainToDecibels (hotComp / clean));
    const float rawDiffDb  = std::abs (juce::Decibels::gainToDecibels (hotRaw / clean));
    INFO ("compensated " << compDiffDb << " dB, uncompensated " << rawDiffDb << " dB");
    REQUIRE (compDiffDb < 4.0f);
    REQUIRE (compDiffDb < rawDiffDb);
}

TEST_CASE ("Ducking lowers the 808 while the kick plays")
{
    auto withKick = [] (float amount)
    {
        auto p = makeProcessor();
        setParam (*p, ids::duckAmount, amount);
        setParam (*p, ids::duckThreshold, -12.0f);
        return play (*p, 1.0f, 36, true).rms();
    };

    const float dry = withKick (0.0f);
    const float ducked = withKick (1.0f);
    REQUIRE (ducked < dry * 0.8f);
}

TEST_CASE ("Stereo width keeps the mono sum identical to the centre")
{
    auto p = makeProcessor();
    setParam (*p, ids::stereoWidth, 1.0f);
    setParam (*p, ids::waveShape, 1.0f);
    setParam (*p, ids::drive, 18.0f);
    const auto wide = play (*p, 0.5f, 48);

    auto q = makeProcessor();
    setParam (*q, ids::stereoWidth, 0.0f);
    setParam (*q, ids::waveShape, 1.0f);
    setParam (*q, ids::drive, 18.0f);
    const auto narrow = play (*q, 0.5f, 48);

    double sideEnergy = 0.0, monoError = 0.0;
    for (size_t i = 0; i < wide.left.size(); ++i)
    {
        sideEnergy += std::abs (wide.left[i] - wide.right[i]);
        monoError  += std::abs ((wide.left[i] + wide.right[i]) - (narrow.left[i] + narrow.right[i]));
    }
    REQUIRE (sideEnergy > 1.0);     // it really is wider
    REQUIRE (monoError < 1.0e-3);   // and folds to mono exactly
}

TEST_CASE ("State and factory presets round-trip")
{
    auto p = makeProcessor();
    p->setCurrentProgram (4);
    setParam (*p, ids::glideTime, 333.0f);

    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto q = std::make_unique<Access808Processor>();
    q->setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (q->apvts.getRawParameterValue (ids::glideTime)->load() == 333.0f);
    REQUIRE (q->getCurrentProgram() == 4);
    REQUIRE (q->presets.getCurrentName() == p->presets.getCurrentName());
}

TEST_CASE ("Every factory preset renders cleanly")
{
    const int count = (int) getFactoryPresets().size();
    for (int i = 0; i < count; ++i)
    {
        auto p = std::make_unique<Access808Processor>();
        p->setRateAndBufferSizeDetails (sr, block);
        p->setCurrentProgram (i);
        const auto r = play (*p, 0.5f, 36, false, 0);
        INFO ("preset " << getFactoryPresets()[(size_t) i].name);
        REQUIRE (r.finite());
        REQUIRE (r.peak() < 2.0f);
        REQUIRE (r.peak() > 0.01f);
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    return Catch::Session().run (argc, argv);
}
