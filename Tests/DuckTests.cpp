#include "Engine/Sequencer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

constexpr int kActive = 0;
constexpr int kNoBus = 1;
constexpr int kSilent = 2;
constexpr int kWaiting = 3;
constexpr int kStopped = 4;

void render (psyb::PsyEngine& engine, const psyb::Block& block, std::vector<float>* audio,
             std::vector<float>* gain, const psyb::MidiIn* midi = nullptr, int midiCount = 0,
             const float* const* sidechain = nullptr, int sidechainChannels = 0)
{
    std::vector<float> localGain (static_cast<size_t> (block.numSamples));
    std::vector<float> localAudio (static_cast<size_t> (block.numSamples));
    engine.process (block, midi, midiCount, localAudio.data(), sidechain, sidechainChannels, localGain.data());
    if (audio != nullptr)
        audio->insert (audio->end(), localAudio.begin(), localAudio.end());
    if (gain != nullptr)
        gain->insert (gain->end(), localGain.begin(), localGain.end());
}

int firstDip (const std::vector<float>& gain, int from = 0)
{
    for (int i = from; i < static_cast<int> (gain.size()); ++i)
        if (gain[static_cast<size_t> (i)] < 0.999f)
            return i;
    return -1;
}

void testTiming()
{
    psyb::PsyEngine engine;
    engine.loadInit();
    engine.setDuckDepth (45.f);
    engine.setDuckReleaseMs (120.f);
    const int latency = engine.busLatencySamples();
    CHECK (latency > 0);

    std::vector<float> gain;
    double ppq = 0.0;
    constexpr int kBlock = 64;
    constexpr int kTotal = 24000 + 80;
    int done = 0;
    while (done < kTotal)
    {
        const int n = std::min (kBlock, kTotal - done);
        psyb::Block block { 48000.0, 120.0, ppq, n, true };
        render (engine, block, nullptr, &gain);
        ppq += static_cast<double> (n) * 120.0 / (48000.0 * 60.0);
        done += n;
    }

    CHECK (firstDip (gain) == latency);
    CHECK (gain[static_cast<size_t> (latency - 1)] == 1.f);
    CHECK (gain[static_cast<size_t> (24000 + latency - 1)] == 1.f);
    CHECK (firstDip (gain, 24000) == 24000 + latency);

    psyb::PsyEngine tempo;
    tempo.loadInit();
    tempo.setDuckDepth (45.f);
    tempo.setDuckReleaseMs (10.f);
    std::vector<float> first;
    psyb::Block warm { 48000.0, 120.0, 0.0, 2000, true };
    render (tempo, warm, nullptr, &first);
    const double ppq2 = 0.9;
    const int at = psyb::sampleAtPpq (1.0, ppq2, 175.0, 48000.0);
    std::vector<float> second;
    psyb::Block changed { 48000.0, 175.0, ppq2, 4000, true };
    render (tempo, changed, nullptr, &second);
    CHECK (at >= 0 && at < 4000);
    CHECK (firstDip (second) == at + tempo.busLatencySamples());

    psyb::PsyEngine looped;
    looped.loadInit();
    looped.setDuckDepth (45.f);
    looped.setDuckReleaseMs (10.f);
    psyb::Block lead { 48000.0, 120.0, 0.0, 2000, true };
    render (looped, lead, nullptr, nullptr);
    std::vector<float> jumped;
    psyb::Block again { 48000.0, 120.0, 0.0, 256, true };
    render (looped, again, nullptr, &jumped);
    CHECK (firstDip (jumped) == looped.busLatencySamples());
    std::fprintf (stderr, "D1 latency %d\n", latency);
}

void send (psyb::PsyEngine& engine, int note, int channel, bool playing = false)
{
    psyb::MidiIn event {};
    event.note = note;
    event.channel = channel;
    event.velocity = 100;
    psyb::Block block { 48000.0, 120.0, 0.0, 128, playing };
    engine.process (block, &event, 1, nullptr);
}

void useMidi (psyb::PsyEngine& engine)
{
    auto lane = engine.lane (0);
    lane.source = psyb::SourceMode::Midi;
    lane.enabled = true;
    engine.setLane (0, lane);
}

void testRouter()
{
    psyb::PsyEngine consumed;
    consumed.loadInit();
    useMidi (consumed);
    consumed.setDuckSource (psyb::DuckSource::Midi);
    consumed.setDuckDepth (45.f);
    send (consumed, 36, 0);
    CHECK (! consumed.voiceActive (0));
    CHECK (consumed.duckTriggerCount() == 1);
    send (consumed, 48, 0);
    CHECK (consumed.voiceActive (0));

    psyb::PsyEngine passed;
    passed.loadInit();
    useMidi (passed);
    passed.setDuckSource (psyb::DuckSource::Midi);
    passed.setDuckPassthru (true);
    send (passed, 36, 5);
    CHECK (passed.voiceActive (0));
    CHECK (passed.duckTriggerCount() == 1);

    psyb::PsyEngine normal;
    normal.loadInit();
    useMidi (normal);
    normal.setDuckSource (psyb::DuckSource::Internal);
    send (normal, 36, 0);
    CHECK (normal.voiceActive (0));
    CHECK (normal.duckTriggerCount() == 0);

    psyb::PsyEngine filtered;
    filtered.loadInit();
    useMidi (filtered);
    filtered.setDuckSource (psyb::DuckSource::Midi);
    filtered.setDuckChannel (3);
    send (filtered, 36, 0);
    CHECK (filtered.voiceActive (0));
    CHECK (filtered.duckTriggerCount() == 0);
    send (filtered, 36, 2);
    CHECK (filtered.duckTriggerCount() == 1);
    std::fprintf (stderr, "D2 router\n");
}

int countBursts (const std::vector<float>& signal, int gap, int bursts)
{
    std::vector<float> audio (signal.size(), 0.f);
    for (int n = 0; n < bursts; ++n)
    {
        const int at = 300 + n * gap;
        for (int i = 0; i < 400 && at + i < static_cast<int> (audio.size()); ++i)
            audio[static_cast<size_t> (at + i)] = 0.9f;
    }
    psyb::PsyEngine engine;
    engine.loadInit();
    engine.setDuckSource (psyb::DuckSource::Audio);
    engine.setDuckDepth (45.f);
    const float* channels[2] = { audio.data(), audio.data() };
    psyb::Block block { 48000.0, 120.0, 0.0, static_cast<int> (audio.size()), false };
    std::vector<float> gain;
    render (engine, block, nullptr, &gain, nullptr, 0, channels, 1);
    const int latency = engine.busLatencySamples();
    const int moved = firstDip (gain);
    CHECK (moved >= 0);
    if (moved >= 0)
    {
        CHECK (moved >= 300 + latency);
        CHECK (moved <= 300 + latency + 240);
    }
    return engine.duckTriggerCount();
}

void testAudio()
{
    const int close = countBursts (std::vector<float> (8000), 480, 2);
    CHECK (close == 1);
    const int apart = countBursts (std::vector<float> (12000), 4800, 2);
    CHECK (apart == 2);

    std::vector<float> audio (4000, 0.f);
    for (int i = 300; i < 800; ++i)
        audio[static_cast<size_t> (i)] = 0.9f;
    psyb::PsyEngine stereo;
    stereo.loadInit();
    stereo.setDuckSource (psyb::DuckSource::Audio);
    stereo.setDuckDepth (45.f);
    const float* channels[2] = { audio.data(), audio.data() };
    psyb::Block block { 48000.0, 120.0, 0.0, static_cast<int> (audio.size()), false };
    stereo.process (block, nullptr, 0, nullptr, channels, 2, nullptr);
    CHECK (stereo.duckTriggerCount() == 1);
    std::fprintf (stderr, "D3 close %d apart %d\n", close, apart);
}

void testStates()
{
    psyb::PsyEngine stopped;
    stopped.loadInit();
    psyb::Block idle { 48000.0, 120.0, 0.0, 32, false };
    stopped.process (idle, nullptr, 0, nullptr);
    CHECK (stopped.duckState() == kStopped);
    psyb::Block running { 48000.0, 120.0, 0.0, 32, true };
    stopped.process (running, nullptr, 0, nullptr);
    CHECK (stopped.duckState() == kActive);

    psyb::PsyEngine waiting;
    waiting.loadInit();
    waiting.setDuckSource (psyb::DuckSource::Midi);
    waiting.process (idle, nullptr, 0, nullptr);
    CHECK (waiting.duckState() == kWaiting);
    send (waiting, 36, 0);
    CHECK (waiting.duckState() == kActive);

    psyb::PsyEngine nobus;
    nobus.loadInit();
    nobus.setDuckSource (psyb::DuckSource::Audio);
    nobus.process (idle, nullptr, 0, nullptr, nullptr, 0);
    CHECK (nobus.duckState() == kNoBus);

    psyb::PsyEngine silent;
    silent.loadInit();
    silent.setDuckSource (psyb::DuckSource::Audio);
    std::vector<float> zeros (512, 0.f);
    const float* channel = zeros.data();
    const int blocks = static_cast<int> (2.05 * 48000.0 / 512.0) + 1;
    for (int i = 0; i < blocks; ++i)
    {
        psyb::Block block { 48000.0, 120.0, 0.0, 512, false };
        silent.process (block, nullptr, 0, nullptr, &channel, 1, nullptr);
    }
    CHECK (silent.duckState() == kSilent);

    std::vector<float> kick (512, 0.f);
    for (int i = 0; i < 200; ++i)
        kick[static_cast<size_t> (i)] = 0.9f;
    const float* kickCh = kick.data();
    psyb::Block hit { 48000.0, 120.0, 0.0, 512, false };
    silent.process (hit, nullptr, 0, nullptr, &kickCh, 1, nullptr);
    CHECK (silent.duckState() == kActive);
    std::fprintf (stderr, "D4 states\n");
}

void testShape()
{
    psyb::PsyEngine engine;
    engine.loadInit();
    engine.setDuckDepth (45.f);
    engine.setDuckHoldMs (0.f);
    engine.setDuckReleaseMs (80.f);
    engine.setDuckCurve (psyb::DuckCurve::Exp);
    std::vector<float> gain;
    psyb::Block block { 48000.0, 120.0, 0.0, 8000, true };
    render (engine, block, nullptr, &gain);
    float trough = 1.f;
    for (float sample : gain)
        trough = std::min (trough, sample);
    const float gotDb = 20.f * std::log10 (trough);
    const float expectDb = 20.f * std::log10 (0.55f);
    CHECK (std::abs (gotDb - expectDb) < 0.1f);

    int troughAt = -1;
    for (int i = 0; i < static_cast<int> (gain.size()); ++i)
        if (std::abs (gain[static_cast<size_t> (i)] - trough) < 0.002f)
        {
            troughAt = i;
            break;
        }
    CHECK (troughAt > 0);
    const int releaseAt = engine.busLatencySamples()
                          + static_cast<int> (std::lround (0.001 * 48000.0));
    const float span = 1.f - trough;
    const int releaseSamples = static_cast<int> (std::lround (0.080 * 48000.0));
    const int end = releaseAt + releaseSamples - 1;
    CHECK (end < static_cast<int> (gain.size()));
    CHECK (1.f - gain[static_cast<size_t> (end)] <= span * 0.001f + 1.0e-6f);
    CHECK (1.f - gain[static_cast<size_t> (end - 48)] > span * 0.001f);

    psyb::PsyEngine linear;
    linear.loadInit();
    linear.setDuckDepth (45.f);
    linear.setDuckHoldMs (0.f);
    linear.setDuckReleaseMs (80.f);
    linear.setDuckCurve (psyb::DuckCurve::Lin);
    std::vector<float> lin;
    render (linear, block, nullptr, &lin);
    int linTrough = -1;
    for (int i = 0; i < static_cast<int> (lin.size()); ++i)
        if (lin[static_cast<size_t> (i)] < 0.56f)
        {
            linTrough = i;
            break;
        }
    int linDone = -1;
    for (int i = linTrough; i < static_cast<int> (lin.size()); ++i)
        if (lin[static_cast<size_t> (i)] > 0.999f)
        {
            linDone = i;
            break;
        }
    CHECK (linTrough > 0 && linDone > linTrough);
    CHECK (std::abs ((linDone - linTrough) - releaseSamples) <= 48);

    psyb::PsyEngine held;
    held.loadInit();
    held.setDuckDepth (45.f);
    held.setDuckHoldMs (20.f);
    held.setDuckReleaseMs (40.f);
    std::vector<float> hold;
    render (held, block, nullptr, &hold);
    int heldSamples = 0;
    for (float sample : hold)
        if (std::abs (sample - 0.55f) < 0.002f)
            ++heldSamples;
    const int holdExpect = static_cast<int> (std::lround (0.020 * 48000.0));
    CHECK (std::abs (heldSamples - holdExpect) <= 48);

    psyb::PsyEngine retrig;
    retrig.loadInit();
    retrig.setDuckSource (psyb::DuckSource::Midi);
    retrig.setDuckDepth (60.f);
    retrig.setDuckHoldMs (0.f);
    retrig.setDuckReleaseMs (80.f);
    const int latency = retrig.busLatencySamples();
    psyb::MidiIn notes[2] {};
    notes[0].note = 36;
    notes[0].velocity = 100;
    notes[1].note = 36;
    notes[1].velocity = 100;
    notes[1].sample = latency + 10;
    psyb::Block chord { 48000.0, 120.0, 0.0, 512, false };
    std::vector<float> glide;
    render (retrig, chord, nullptr, &glide, notes, 2);
    const int at = latency + 10 + latency;
    CHECK (at > 1 && at < static_cast<int> (glide.size()));
    CHECK (std::abs (glide[static_cast<size_t> (at)] - glide[static_cast<size_t> (at - 1)]) < 0.05f);
    CHECK (glide[static_cast<size_t> (at - 1)] < 0.95f);
    std::fprintf (stderr, "D5 trough %.2f dB\n", gotDb);
}

void testTargets()
{
    auto take = [] (float depth, bool rollTarget)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        auto lane = engine.lane (0);
        lane.source = psyb::SourceMode::Midi;
        lane.enabled = true;
        engine.setLane (0, lane);
        auto other = engine.lane (1);
        other.enabled = false;
        engine.setLane (1, other);
        engine.setDuckDepth (depth);
        engine.setDuckTarget (0, rollTarget);
        engine.setDuckTarget (1, ! rollTarget);
        engine.setDuckSource (psyb::DuckSource::Internal);
        psyb::RollParams params;
        params.asus = 1.f;
        params.adecMs = 4000.f;
        params.cutoffHz = 400.f;
        params.sub = 0.8f;
        engine.setRollParams (params);
        engine.setEngineAudible (0, true);
        psyb::MidiIn note {};
        note.note = 40;
        note.velocity = 100;
        std::vector<float> audio;
        psyb::Block block { 48000.0, 145.0, 0.0, 2000, true };
        render (engine, block, &audio, nullptr, &note, 1);
        return audio;
    };

    const auto plain = take (0.f, true);
    const auto spared = take (100.f, false);
    const auto ducked = take (100.f, true);
    float same = 0.f;
    float moved = 0.f;
    for (size_t i = 0; i < plain.size(); ++i)
    {
        same = std::max (same, std::abs (plain[i] - spared[i]));
        moved = std::max (moved, std::abs (plain[i] - ducked[i]));
    }
    CHECK (same == 0.f);
    CHECK (moved > 0.01f);
    std::fprintf (stderr, "D6 spared diff %.3g ducked diff %.3g\n", same, moved);
}

void renderScenario (std::vector<float>& out, bool duckTargets)
{
    constexpr double kSr = 48000.0;
    constexpr double kBpm = 145.0;
    constexpr int kTotal = static_cast<int> (32.0 * kSr * 60.0 / kBpm);
    out.assign (static_cast<size_t> (kTotal), 0.f);
    psyb::PsyEngine engine;
    engine.loadInit();
    if (! duckTargets)
    {
        engine.setDuckDepth (100.f);
        engine.setDuckTarget (0, false);
        engine.setDuckTarget (1, false);
        engine.setDuckTarget (2, false);
    }
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

void testBypass()
{
    std::vector<float> plain;
    std::vector<float> muted;
    renderScenario (plain, true);
    renderScenario (muted, false);
    const std::string path = std::string (BASSSYNTH_SOURCE_DIR) + "/Tests/fixtures/m1_ref.f32";
    std::vector<float> ref (plain.size());
    FILE* in = std::fopen (path.c_str(), "rb");
    CHECK (in != nullptr);
    if (in == nullptr)
        return;
    const auto read = std::fread (ref.data(), sizeof (float), ref.size(), in);
    std::fclose (in);
    CHECK (read == ref.size());
    float plainDiff = 0.f;
    float mutedDiff = 0.f;
    if (read == ref.size())
    {
        for (size_t i = 0; i < plain.size(); ++i)
        {
            plainDiff = std::max (plainDiff, std::abs (plain[i] - ref[i]));
            mutedDiff = std::max (mutedDiff, std::abs (muted[i] - ref[i]));
        }
    }
    std::fprintf (stderr, "D7 plain %.3g targets-off %.3g\n", plainDiff, mutedDiff);
    CHECK (plainDiff <= 1.0e-6f);
    CHECK (mutedDiff <= 1.0e-6f);
}

void testClicks()
{
    psyb::PsyEngine engine;
    engine.loadInit();
    useMidi (engine);
    engine.setDuckSource (psyb::DuckSource::Midi);
    engine.setDuckPassthru (true);
    engine.setDuckDepth (80.f);
    engine.setDuckHoldMs (50.f);
    engine.setDuckReleaseMs (200.f);
    psyb::RollParams params;
    params.asus = 1.f;
    params.adecMs = 4000.f;
    params.arelMs = 400.f;
    params.cutoffHz = 180.f;
    params.sub = 1.f;
    engine.setRollParams (params);
    engine.setEngineAudible (0, true);
    psyb::MidiIn note {};
    note.note = 36;
    note.velocity = 100;
    std::vector<float> audio;
    std::vector<float> gain;
    psyb::Block block { 48000.0, 120.0, 0.0, 2500, false };
    render (engine, block, &audio, &gain, &note, 1);
    float peak = 0.f;
    for (int i = 1800; i < 2500; ++i)
        peak = std::max (peak, std::abs (audio[static_cast<size_t> (i)]));
    CHECK (peak > 0.02f);

    engine.setDuckTarget (0, false);
    std::vector<float> after;
    psyb::Block next { 48000.0, 120.0, 1.0, 128, false };
    render (engine, next, &after, nullptr);
    float prev = audio.back();
    float step = 0.f;
    for (float sample : after)
    {
        if (std::abs (prev) > 0.02f && std::abs (sample) > 0.02f)
        {
            const float ratio = std::abs (sample / prev);
            step = std::max (step, std::abs (ratio - 1.f));
        }
        prev = sample;
    }
    CHECK (step < 0.5f);

    engine.setDuckDepth (30.f);
    std::vector<float> depthGain;
    psyb::Block depthBlock { 48000.0, 120.0, 2.0, 64, false };
    render (engine, depthBlock, nullptr, &depthGain);
    float depthStep = 0.f;
    for (size_t i = 1; i < depthGain.size(); ++i)
        depthStep = std::max (depthStep, std::abs (depthGain[i] - depthGain[i - 1]));
    CHECK (depthStep < 0.05f);

    engine.setDuckSource (psyb::DuckSource::Internal);
    std::vector<float> tail;
    for (int i = 0; i < 40; ++i)
    {
        psyb::Block release { 48000.0, 120.0, 3.0, 512, false };
        render (engine, release, nullptr, &tail);
    }
    CHECK (! tail.empty() && tail.back() == 1.f);
    float stuck = 0.f;
    for (size_t i = 1; i < tail.size(); ++i)
        stuck = std::max (stuck, std::abs (tail[i] - tail[i - 1]));
    CHECK (stuck < 0.05f);
    std::fprintf (stderr, "D8 target step %.3f depth step %.3f\n", step, depthStep);
}

void testRealtime()
{
    psyb::PsyEngine engine;
    engine.loadInit();
    engine.setDuckSource (psyb::DuckSource::Audio);
    psyb::Block block { 48000.0, 120.0, 0.0, 128, true };
    const float* missing[1] = { nullptr };
    engine.process (block, nullptr, 0, nullptr, nullptr, 0, nullptr);
    engine.process (block, nullptr, 0, nullptr, missing, 1, nullptr);
    CHECK (engine.duckState() == kNoBus || engine.duckState() == kSilent || engine.duckState() == kActive);
    std::fprintf (stderr, "D9 null sidechain\n");
}

void testPerf()
{
    auto measure = [] (float depth)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        useMidi (engine);
        auto slide = engine.lane (1);
        slide.source = psyb::SourceMode::Midi;
        slide.enabled = true;
        engine.setLane (1, slide);
        engine.setEngineAudible (0, true);
        engine.setEngineAudible (1, true);
        engine.setDuckDepth (depth);
        engine.setDuckSource (psyb::DuckSource::Internal);
        psyb::MidiIn notes[2] {};
        notes[0].note = 36;
        notes[0].velocity = 100;
        notes[1].note = 48;
        notes[1].velocity = 100;
        notes[1].channel = 0;
        std::vector<float> audio (512);
        psyb::Block warm { 48000.0, 145.0, 0.0, 512, true };
        engine.process (warm, notes, 2, audio.data());
        constexpr int kBlocks = 400;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kBlocks; ++i)
        {
            psyb::Block block { 48000.0, 145.0, 0.25 * static_cast<double> (i + 1), 512, true };
            engine.process (block, nullptr, 0, audio.data());
        }
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / kBlocks;
    };

    const double off = measure (0.f);
    const double on = measure (45.f);
    const double added = off > 0.0 ? (on - off) / off : 0.0;
    std::fprintf (stderr, "D10 duck %.3f ms vs %.3f ms (%+.1f%%)\n", on, off, added * 100.0);
    CHECK (added < 0.05 || on - off < 0.05);
}

} // namespace

int runDuckTests()
{
    testTiming();
    testRouter();
    testAudio();
    testStates();
    testShape();
    testTargets();
    testBypass();
    testClicks();
    testRealtime();
    testPerf();
    return fails;
}
