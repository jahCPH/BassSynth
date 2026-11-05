#include "Dsp/AlgorithmVersion.h"
#include "Engine/EngineFactory.h"
#include "Engine/Sequencer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
int fails = 0;

void check (bool ok, const char* text, int line)
{
    if (ok)
        return;
    std::fprintf (stderr, "FAIL %d: %s\n", line, text);
    ++fails;
}

#define CHECK(cond) check ((cond), #cond, __LINE__)

std::string referencePath()
{
    const char* root = BASSSYNTH_SOURCE_DIR;
    return std::string (root) + "/Tests/fixtures/m1_ref.f32";
}

void renderScenario (std::vector<float>& out)
{
    constexpr double kSr = 48000.0;
    constexpr double kBpm = 145.0;
    constexpr int kTotal = static_cast<int> (32.0 * kSr * 60.0 / kBpm);
    out.assign (static_cast<size_t> (kTotal), 0.f);

    psyb::PsyEngine engine;
    engine.loadInit();
    auto slide = engine.lane (1);
    slide.enabled = true;
    engine.setLane (1, slide);
    engine.setEngineAudible (0, true);
    engine.setEngineAudible (1, true);
    engine.setEngineLevelDb (0, 0.f);
    engine.setEngineLevelDb (1, 0.f);
    engine.setLaneGlide (0, 0.f, 0.f);
    engine.setLaneGlide (1, 0.40f, 80.f);
    engine.setSlideAccent (0.5f);
    engine.setSubOwner (psyb::SubOwner::Roll);

    psyb::Step tie {};
    tie.on = true;
    tie.tie = true;
    tie.gate = 0.5f;
    tie.velocity = 100;
    engine.setStep (1, 4, tie);
    psyb::Step accent = tie;
    accent.tie = false;
    accent.accent = true;
    accent.offset = 12;
    engine.setStep (1, 5, accent);

    psyb::MidiIn held {};
    held.note = 48;
    held.velocity = 100;

    int done = 0;
    bool midiGlideSent = false;
    while (done < kTotal)
    {
        const int n = std::min (128, kTotal - done);
        if (done == 128 * 80)
            engine.setSubOwner (psyb::SubOwner::Slide);
        if (done == 128 * 160)
            engine.setSubOwner (psyb::SubOwner::Off);
        if (done == 128 * 240)
            engine.setSubOwner (psyb::SubOwner::Roll);
        if (done == 128 * 100)
            engine.setEngineLevelDb (1, -12.f);
        if (done == 128 * 140)
            engine.setEngineAudible (1, false);
        if (done == 128 * 150)
            engine.setEngineAudible (1, true);
        if (done == 128 * 300)
        {
            auto lane = engine.lane (1);
            lane.source = psyb::SourceMode::Midi;
            lane.enabled = true;
            engine.setLane (1, lane);
        }

        psyb::MidiIn events[3] {};
        int eventCount = 0;
        if (done == 0)
            events[eventCount++] = held;
        if (! midiGlideSent && done >= 128 * 320)
        {
            events[eventCount].sample = 0;
            events[eventCount].note = 36;
            events[eventCount].velocity = 100;
            ++eventCount;
            events[eventCount].sample = 64;
            events[eventCount].note = 48;
            events[eventCount].velocity = 110;
            ++eventCount;
            midiGlideSent = true;
        }

        psyb::Block block;
        block.sampleRate = kSr;
        block.bpm = kBpm;
        block.ppqStart = static_cast<double> (done) * kBpm / (kSr * 60.0);
        block.numSamples = n;
        block.playing = true;
        engine.process (block, eventCount > 0 ? events : nullptr, eventCount, out.data() + done);
        done += n;
    }
}
} // namespace

int runModuleTests()
{
    std::vector<float> got;
    renderScenario (got);
    const auto path = referencePath();

    if (std::getenv ("BASSSYNTH_WRITE_M1") != nullptr)
    {
        if (FILE* out = std::fopen (path.c_str(), "wb"))
        {
            const auto wrote = std::fwrite (got.data(), sizeof (float), got.size(), out);
            std::fclose (out);
            std::fprintf (stderr, "M1 wrote %zu samples to %s\n", wrote, path.c_str());
        }
        return fails;
    }

    std::vector<float> ref (got.size());
    FILE* in = std::fopen (path.c_str(), "rb");
    CHECK (in != nullptr);
    if (in != nullptr)
    {
        const auto read = std::fread (ref.data(), sizeof (float), ref.size(), in);
        std::fclose (in);
        CHECK (read == ref.size());
        float diff = 0.f;
        int mismatches = 0;
        int first = -1;
        int worst = -1;
        if (read == ref.size())
        {
            for (size_t i = 0; i < got.size(); ++i)
            {
                const float d = std::abs (got[i] - ref[i]);
                if (d != 0.f)
                {
                    if (first < 0)
                        first = static_cast<int> (i);
                    ++mismatches;
                }
                if (d > diff)
                    worst = static_cast<int> (i);
                diff = std::max (diff, d);
            }
        }
        std::fprintf (stderr, "M1 samples %zu max abs diff %.8g mismatches %d first %d worst %d\n", got.size(), diff, mismatches, first, worst);
        if (first >= 0)
            std::fprintf (stderr, "M1 at %d got %.9g ref %.9g\n", first, got[static_cast<size_t> (first)], ref[static_cast<size_t> (first)]);
        if (worst >= 0)
            std::fprintf (stderr, "M1 worst %d got %.9g ref %.9g\n", worst, got[static_cast<size_t> (worst)], ref[static_cast<size_t> (worst)]);
        CHECK (diff <= 1.0e-6f);
    }

    auto engines = psyb::createEngines();
    CHECK (engines.size() == 3);
    engines.pop_back();
    engines[0]->prepare (48000.0, 512);
    engines[0]->setEnabled (true);
    engines[0]->setAudible (true);
    engines[0]->setSubOwner (psyb::SubOwner::Roll);
    engines[0]->noteOn (0, 48, 100, true, 0.f, false);
    std::vector<float> bass (512, 0.f);
    engines[0]->render (bass.data(), 512);
    float bassPeak = 0.f;
    for (float sample : bass)
        bassPeak = std::max (bassPeak, std::abs (sample));
    std::fprintf (stderr, "M3 bass peak %.5f with FX removed\n", bassPeak);
    CHECK (bassPeak > 0.01f);
    std::vector<float> slide (128, 0.f);
    engines[1]->prepare (48000.0, 128);
    engines[1]->render (slide.data(), 128);
    float slidePeak = 0.f;
    for (float sample : slide)
        slidePeak = std::max (slidePeak, std::abs (sample));
    CHECK (slidePeak == 0.f);

    const auto missing = psyb::resolveAlgorithmVersions ({});
    CHECK (missing.roll == 1 && missing.slide == 1 && missing.fx == 1);
    psyb::AlgorithmVersionFields fields;
    fields.hasRoll = true;
    fields.hasSlide = true;
    fields.hasFx = true;
    fields.roll = 2;
    fields.slide = 3;
    fields.fx = 4;
    const auto loaded = psyb::resolveAlgorithmVersions (fields);
    CHECK (loaded.roll == 2 && loaded.slide == 3 && loaded.fx == 4);
    psyb::PsyEngine engine;
    engine.setStoredAlgorithmVersion (0, loaded.roll);
    engine.setStoredAlgorithmVersion (1, loaded.slide);
    engine.setStoredAlgorithmVersion (2, loaded.fx);
    CHECK (engine.storedAlgorithmVersion (0) == 2);
    CHECK (engine.storedAlgorithmVersion (1) == 3);
    CHECK (engine.storedAlgorithmVersion (2) == 4);
    CHECK (engines[0]->algorithmVersion() == 1);
    return fails;
}
