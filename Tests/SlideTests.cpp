#include "Dsp/ReportedLatency.h"
#include "Engine/RollVoice.h"
#include "Engine/Sequencer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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

psyb::RollParams slideParams()
{
    psyb::RollParams params;
    params.cutoffHz = 8000.f;
    params.reso = 0.f;
    params.envAmt = 0.f;
    params.fatkMs = 0.5f;
    params.fdecMs = 250.f;
    params.fsus = 0.f;
    params.aatkMs = 0.5f;
    params.adecMs = 300.f;
    params.asus = 1.f;
    params.arelMs = 80.f;
    params.drive = 0.f;
    params.sub = 0.f;
    params.phaseReset = true;
    params.filterReset = true;
    return params;
}

void silenceLane (psyb::PsyEngine& engine, int lane)
{
    psyb::LaneConfig cfg = engine.lane (lane);
    cfg.enabled = false;
    engine.setLane (lane, cfg);
}

void useMidi (psyb::PsyEngine& engine, int lane)
{
    psyb::LaneConfig cfg = engine.lane (lane);
    cfg.enabled = true;
    cfg.source = psyb::SourceMode::Midi;
    engine.setLane (lane, cfg);
}

psyb::MidiIn midi (int sample, int note, int velocity)
{
    psyb::MidiIn event {};
    event.sample = sample;
    event.note = note;
    event.velocity = velocity;
    return event;
}

std::vector<float> render (psyb::PsyEngine& engine, int total, double sr, double bpm,
                           const std::vector<psyb::MidiIn>& events, bool playing)
{
    std::vector<float> out (static_cast<size_t> (std::max (0, total)), 0.f);
    int done = 0;
    while (done < total)
    {
        const int n = std::min (128, total - done);
        std::vector<psyb::MidiIn> here;
        for (auto event : events)
        {
            if (event.sample < done || event.sample >= done + n)
                continue;
            event.sample -= done;
            here.push_back (event);
        }
        psyb::Block block;
        block.sampleRate = sr;
        block.bpm = bpm;
        block.numSamples = n;
        block.playing = playing;
        block.ppqStart = static_cast<double> (done) * bpm / (sr * 60.0);
        engine.process (block, here.empty() ? nullptr : here.data(), static_cast<int> (here.size()), out.data() + done);
        done += n;
    }
    return out;
}

float goertzel (const std::vector<float>& frame, int begin, int count, float freq, double sr)
{
    const float coeff = 2.f * std::cos (6.283185307179586f * freq / static_cast<float> (sr));
    float s1 = 0.f;
    float s2 = 0.f;
    for (int i = 0; i < count; ++i)
    {
        const float x = frame[static_cast<size_t> (begin + i)];
        const float s0 = x + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

int periodAt (const std::vector<float>& frame, int at)
{
    const int n = static_cast<int> (frame.size());
    int first = -1;
    for (int i = std::max (1, at); i < n; ++i)
    {
        if (frame[static_cast<size_t> (i - 1)] <= 0.f && frame[static_cast<size_t> (i)] > 0.f)
        {
            if (first < 0)
                first = i;
            else
                return i - first;
        }
    }
    return -1;
}

void testS1()
{
    psyb::PsyEngine silent;
    silent.loadInit();
    silent.setEngineAudible (0, false);
    auto slide = silent.lane (1);
    slide.enabled = true;
    silent.setLane (1, slide);
    silent.setEngineAudible (1, false);
    const auto zeros = render (silent, 2000, 48000.0, 145.0, { midi (0, 60, 100) }, true);
    for (float sample : zeros)
        CHECK (sample == 0.f);

    auto rollTake = [] (float slideCut)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        useMidi (engine, 0);
        silenceLane (engine, 1);
        silenceLane (engine, 2);
        psyb::RollParams other;
        other.cutoffHz = slideCut;
        engine.setSlideParams (other);
        engine.setEngineAudible (1, false);
        auto slideLane = engine.lane (1);
        slideLane.enabled = true;
        slideLane.source = psyb::SourceMode::Seq;
        engine.setLane (1, slideLane);
        for (int i = 0; i < psyb::kMaxSteps; ++i)
            engine.setStep (1, i, {});
        return render (engine, 1500, 48000.0, 145.0, { midi (0, 60, 100) }, false);
    };
    const auto left = rollTake (400.f);
    const auto right = rollTake (8000.f);
    float diff = 0.f;
    for (size_t i = 0; i < left.size(); ++i)
        diff = std::max (diff, std::abs (left[i] - right[i]));
    CHECK (diff == 0.f);
}

void testS2andS3()
{
    psyb::PsyEngine engine;
    engine.loadInit();
    silenceLane (engine, 0);
    silenceLane (engine, 2);
    auto lane = engine.lane (1);
    lane.enabled = true;
    lane.source = psyb::SourceMode::Seq;
    engine.setLane (1, lane);
    engine.setEngineAudible (1, true);
    engine.setLaneGlide (1, 0.40f, 60.f);
    auto params = slideParams();
    engine.setSlideParams (params);

    for (int i = 0; i < 4; ++i)
    {
        psyb::Step step {};
        step.on = i < 2;
        step.tie = i == 0;
        step.gate = 0.5f;
        step.offset = i == 1 ? 12 : 0;
        step.velocity = 100;
        engine.setStep (1, i, step);
    }

    const double sr = 48000.0;
    const double bpm = 145.0;
    const int tieAt = psyb::sampleAtPpq (0.25, 0.0, bpm, sr);
    const int total = tieAt + 4000;
    std::vector<float> audio (static_cast<size_t> (total));
    std::vector<float> env (static_cast<size_t> (total));
    psyb::GlideInfo heard {};
    bool sawGlide = false;
    psyb::MidiIn on = midi (0, 60, 100);
    int done = 0;
    while (done < total)
    {
        const int n = 1;
        psyb::Block block { sr, bpm, static_cast<double> (done) * bpm / (sr * 60.0), n, true };
        const bool send = on.sample == done;
        engine.process (block, send ? &on : nullptr, send ? 1 : 0, audio.data() + done);
        env[static_cast<size_t> (done)] = engine.voiceEnv (1);
        for (int i = 0; i < engine.triggerCount(); ++i)
        {
            const auto trigger = engine.trigger (i);
            if (trigger.engine == 1 && trigger.glide.applies)
            {
                heard = trigger.glide;
                sawGlide = true;
            }
        }
        ++done;
    }

    const float before = env[static_cast<size_t> (std::max (0, tieAt - 20))];
    float trough = before;
    for (int i = tieAt; i < tieAt + 48 && i < total; ++i)
        trough = std::min (trough, env[static_cast<size_t> (i)]);
    std::fprintf (stderr, "S2 env before %.4f trough %.4f\n", before, trough);
    CHECK (before > 0.5f);
    CHECK (trough > before * std::pow (10.f, -0.5f / 20.f));
    CHECK (sawGlide);

    const int glideEnd = tieAt + static_cast<int> (std::lround (heard.effectiveMs * 0.001 * sr));
    const int period = periodAt (audio, glideEnd);
    const float targetHz = 440.f * std::pow (2.f, (72.f - 69.f) / 12.f);
    const int expect = static_cast<int> (std::lround (sr / targetHz));
    std::fprintf (stderr, "S2 period %d expect %d glideEnd %d\n", period, expect, glideEnd);
    CHECK (period > 0);
    CHECK (std::abs (period - expect) <= 1);

    psyb::PsyEngine clamped;
    clamped.loadInit();
    silenceLane (clamped, 0);
    silenceLane (clamped, 2);
    auto cfg = clamped.lane (1);
    cfg.enabled = true;
    clamped.setLane (1, cfg);
    clamped.setEngineAudible (1, true);
    clamped.setLaneGlide (1, 1.f, 60.f);
    clamped.setSlideParams (params);
    for (int i = 0; i < 2; ++i)
    {
        psyb::Step step {};
        step.on = true;
        step.tie = i == 0;
        step.gate = 0.5f;
        step.offset = i == 1 ? 12 : 0;
        step.velocity = 100;
        clamped.setStep (1, i, step);
    }
    psyb::MidiIn held = midi (0, 48, 100);
    psyb::Block block { sr, bpm, 0.0, tieAt + 100, true };
    std::vector<float> scratch (static_cast<size_t> (block.numSamples));
    clamped.process (block, &held, 1, scratch.data());
    bool found = false;
    for (int i = 0; i < clamped.triggerCount(); ++i)
    {
        const auto trigger = clamped.trigger (i);
        if (! trigger.glide.applies)
            continue;
        found = true;
        const double stepMs = 0.25 * 60000.0 / bpm;
        std::fprintf (stderr, "S3 requested %.2f effective %.2f clamped %d\n",
                      trigger.glide.requestedMs, trigger.glide.effectiveMs, trigger.glide.clamped ? 1 : 0);
        CHECK (trigger.glide.clamped);
        CHECK (trigger.glide.requestedMs > trigger.glide.effectiveMs);
        CHECK (std::abs (trigger.glide.effectiveMs - 0.5 * stepMs) < 1.0);
    }
    CHECK (found);
}

void testS4()
{
    auto glideSamples = [] (double bpm)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        silenceLane (engine, 0);
        silenceLane (engine, 2);
        useMidi (engine, 1);
        engine.setEngineAudible (1, true);
        engine.setLaneGlide (1, 0.f, 60.f);
        engine.setSlideParams (slideParams());
        const int total = static_cast<int> (0.080 * 48000.0);
        const int offAt = static_cast<int> (0.020 * 48000.0);
        const std::vector<psyb::MidiIn> events { midi (0, 48, 100), midi (64, 60, 100),
                                                  midi (offAt, 60, 0), midi (offAt, 48, 0) };
        std::vector<float> audio (static_cast<size_t> (total));
        float ms = -1.f;
        int done = 0;
        while (done < total)
        {
            const int n = std::min (64, total - done);
            std::vector<psyb::MidiIn> here;
            for (auto event : events)
            {
                if (event.sample < done || event.sample >= done + n)
                    continue;
                event.sample -= done;
                here.push_back (event);
            }
            psyb::Block block { 48000.0, bpm, 0.0, n, false };
            engine.process (block, here.empty() ? nullptr : here.data(), static_cast<int> (here.size()), audio.data() + done);
            for (int i = 0; i < engine.triggerCount(); ++i)
            {
                const auto trigger = engine.trigger (i);
                if (! trigger.glide.applies)
                    continue;
                ms = trigger.glide.effectiveMs;
                CHECK (! trigger.glide.clamped);
            }
            done += n;
        }
        const int early = periodAt (audio, static_cast<int> (0.010 * 48000.0));
        const int late = periodAt (audio, static_cast<int> (0.050 * 48000.0));
        std::fprintf (stderr, "S4 bpm %.0f ms %.2f early %d late %d\n", bpm, ms, early, late);
        CHECK (std::abs (ms - 60.f) < 0.1f);
        CHECK (early > late && late > 0);
        return late;
    };
    const int at120 = glideSamples (120.0);
    const int at175 = glideSamples (175.0);
    CHECK (std::abs (at120 - at175) <= 2);
}

void testS5()
{
    auto hit = [] (bool accented, float amount)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        silenceLane (engine, 0);
        silenceLane (engine, 2);
        auto lane = engine.lane (1);
        lane.enabled = true;
        engine.setLane (1, lane);
        engine.setEngineAudible (1, true);
        engine.setSlideAccent (amount);
        auto params = slideParams();
        params.cutoffHz = 18000.f;
        params.asus = 0.f;
        params.adecMs = 80.f;
        engine.setSlideParams (params);
        psyb::Step step {};
        step.on = true;
        step.gate = 0.5f;
        step.accent = accented;
        step.velocity = 100;
        engine.setStep (1, 0, step);
        for (int i = 1; i < 16; ++i)
            engine.setStep (1, i, {});
        return render (engine, 3000, 48000.0, 145.0, { midi (0, 60, 100) }, true);
    };
    const auto plain = hit (false, 0.5f);
    const auto accent = hit (true, 0.5f);
    const auto zeroA = hit (true, 0.f);
    const auto zeroB = hit (false, 0.f);
    float peakPlain = 0.f;
    float peakAccent = 0.f;
    float diff = 0.f;
    for (size_t i = 0; i < plain.size(); ++i)
    {
        peakPlain = std::max (peakPlain, std::abs (plain[i]));
        peakAccent = std::max (peakAccent, std::abs (accent[i]));
        diff = std::max (diff, std::abs (zeroA[i] - zeroB[i]));
    }
    const float db = 20.f * std::log10 (peakAccent / std::max (peakPlain, 1.0e-8f));
    std::fprintf (stderr, "S5 accent %.2f dB\n", db);
    CHECK (std::abs (db - 4.f) <= 0.5f);
    CHECK (diff == 0.f);

    auto bright = [] (bool accented)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        silenceLane (engine, 0);
        silenceLane (engine, 2);
        auto lane = engine.lane (1);
        lane.enabled = true;
        engine.setLane (1, lane);
        engine.setEngineAudible (1, true);
        engine.setSlideAccent (0.5f);
        auto params = slideParams();
        params.cutoffHz = 700.f;
        params.envAmt = 0.40f;
        params.fsus = 1.f;
        params.asus = 1.f;
        engine.setSlideParams (params);
        psyb::Step step {};
        step.on = true;
        step.gate = 1.f;
        step.accent = accented;
        step.velocity = 100;
        engine.setStep (1, 0, step);
        for (int i = 1; i < 16; ++i)
            engine.setStep (1, i, {});
        const auto audio = render (engine, 8000, 48000.0, 120.0, { midi (0, 48, 100) }, true);
        return goertzel (audio, 4000, 2048, 3000.f, 48000.0);
    };
    const float dull = bright (false);
    const float open = bright (true);
    std::fprintf (stderr, "S5 high band %.3f vs %.3f\n", open, dull);
    CHECK (open > dull * 1.2f);
}

void testS6()
{
    auto probe = [] (psyb::RollParams params)
    {
        params.filterReset = false;
        params.drive = 1.f;
        params.sub = 0.7f;
        params.reso = 0.6f;
        params.cutoffHz = 1800.f;
        params.asus = 1.f;
        psyb::RollVoice previous;
        previous.prime (params);
        previous.start (true, true, nullptr);
        for (int i = 0; i < 24000; ++i)
            previous.process (params, 48000.0, 55.f);
        psyb::RollVoice born;
        born.copySmoothingFrom (previous);
        born.start (true, false, &previous);
        float peak = 0.f;
        for (int i = 0; i < psyb::kRollOversampleLatency; ++i)
            peak = std::max (peak, std::abs (born.process (params, 48000.0, 55.f)));
        return peak;
    };
    const float slidePeak = probe (slideParams());
    psyb::RollParams roll;
    const float rollPeak = probe (roll);
    std::fprintf (stderr, "S6 slide %.5f roll %.5f\n", slidePeak, rollPeak);
    CHECK (slidePeak < 0.01f);
    CHECK (rollPeak < 0.01f);
}

void testS7()
{
    auto take = [] (psyb::SubOwner owner, bool rollOn, bool slideOn)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        useMidi (engine, 0);
        useMidi (engine, 1);
        silenceLane (engine, 2);
        engine.setEngineAudible (0, rollOn);
        engine.setEngineAudible (1, slideOn);
        engine.setSubOwner (owner);
        psyb::RollParams roll;
        roll.sub = 0.70f;
        roll.cutoffHz = 4000.f;
        roll.reso = 0.f;
        roll.envAmt = 0.f;
        roll.drive = 0.f;
        roll.asus = 1.f;
        roll.adecMs = 4000.f;
        roll.fsus = 1.f;
        engine.setRollParams (roll);
        auto slide = roll;
        engine.setSlideParams (slide);
        const auto audio = render (engine, 12000, 48000.0, 120.0,
                                    { midi (0, 60, 100) }, false);
        return goertzel (audio, 4000, 4096, 130.8128f, 48000.0);
    };
    const float rollOnly = take (psyb::SubOwner::Roll, true, false);
    const float slideOnly = take (psyb::SubOwner::Slide, false, true);
    const float bothRoll = take (psyb::SubOwner::Roll, true, true);
    const float bothSlide = take (psyb::SubOwner::Slide, true, true);
    const float none = take (psyb::SubOwner::Off, true, true);
    std::fprintf (stderr, "S7 roll %.3f slide %.3f bothR %.3f bothS %.3f off %.3f\n",
                  rollOnly, slideOnly, bothRoll, bothSlide, none);
    CHECK (rollOnly > 1.0e-4f);
    CHECK (slideOnly > 1.0e-4f);
    CHECK (none < rollOnly * 0.05f);
    CHECK (bothRoll < rollOnly * 1.5f);
    CHECK (bothSlide < slideOnly * 1.5f);
}

void testS8()
{
    auto slideChanged = [] (float cut)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        useMidi (engine, 0);
        silenceLane (engine, 2);
        auto slideLane = engine.lane (1);
        slideLane.enabled = true;
        slideLane.source = psyb::SourceMode::Seq;
        engine.setLane (1, slideLane);
        for (int i = 0; i < psyb::kMaxSteps; ++i)
            engine.setStep (1, i, {});
        engine.setEngineAudible (0, true);
        engine.setEngineAudible (1, true);
        psyb::RollParams slide;
        slide.cutoffHz = cut;
        engine.setSlideParams (slide);
        return render (engine, 1200, 48000.0, 145.0, { midi (0, 60, 100) }, false);
    };
    const auto a = slideChanged (300.f);
    const auto b = slideChanged (9000.f);
    float diff = 0.f;
    for (size_t i = 0; i < a.size(); ++i)
        diff = std::max (diff, std::abs (a[i] - b[i]));
    CHECK (diff == 0.f);

    auto rollChanged = [] (float cut)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        auto rollLane = engine.lane (0);
        rollLane.enabled = true;
        rollLane.source = psyb::SourceMode::Seq;
        engine.setLane (0, rollLane);
        for (int i = 0; i < psyb::kMaxSteps; ++i)
            engine.setStep (0, i, {});
        useMidi (engine, 1);
        silenceLane (engine, 2);
        engine.setEngineAudible (0, true);
        engine.setEngineAudible (1, true);
        engine.setSlideParams (slideParams());
        psyb::RollParams roll;
        roll.cutoffHz = cut;
        engine.setRollParams (roll);
        return render (engine, 1200, 48000.0, 145.0, { midi (0, 60, 100) }, false);
    };
    const auto c = rollChanged (300.f);
    const auto d = rollChanged (9000.f);
    diff = 0.f;
    for (size_t i = 0; i < c.size(); ++i)
        diff = std::max (diff, std::abs (c[i] - d[i]));
    CHECK (diff == 0.f);
}

void testS9()
{
    auto onset = [] (bool rollOn, bool slideOn)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        useMidi (engine, 0);
        useMidi (engine, 1);
        silenceLane (engine, 2);
        engine.setEngineAudible (0, rollOn);
        engine.setEngineAudible (1, slideOn);
        psyb::RollParams params = slideParams();
        params.sub = 0.70f;
        engine.setSubOwner (slideOn && ! rollOn ? psyb::SubOwner::Slide : psyb::SubOwner::Roll);
        engine.setRollParams (params);
        engine.setSlideParams (params);
        const auto audio = render (engine, 400, 48000.0, 145.0, { midi (0, 60, 100) }, false);
        for (int i = 0; i < static_cast<int> (audio.size()); ++i)
            if (std::abs (audio[static_cast<size_t> (i)]) > 1.0e-3f)
                return i;
        return -1;
    };
    const int roll = onset (true, false);
    const int slide = onset (false, true);
    std::fprintf (stderr, "S9 roll %d slide %d latency %d\n", roll, slide, psyb::kRollOversampleLatency);
    CHECK (roll == slide);
    CHECK (roll == psyb::kRollOversampleLatency);
    CHECK (psyb::hostReportedLatencySamples() == psyb::kRollOversampleLatency);
}

void testS10()
{
    psyb::PsyEngine engine;
    engine.loadInit();
    silenceLane (engine, 0);
    useMidi (engine, 1);
    silenceLane (engine, 2);
    engine.setEngineAudible (1, true);
    auto params = slideParams();
    params.cutoffHz = 20.f;
    params.reso = 0.5f;
    engine.setSlideParams (params);
    const int at = 6000;
    std::vector<float> moving (static_cast<size_t> (at + 2000));
    int done = 0;
    while (done < at + 2000)
    {
        if (done == at)
        {
            params.cutoffHz = 20000.f;
            engine.setSlideParams (params);
        }
        const int n = std::min (128, at + 2000 - done);
        psyb::Block block { 48000.0, 145.0, 0.0, n, false };
        psyb::MidiIn on = midi (0, 50, 100);
        const bool send = done == 0;
        engine.process (block, send ? &on : nullptr, send ? 1 : 0, moving.data() + done);
        done += n;
    }
    float peak = 0.f;
    for (int i = at + 2; i < at + 1500; ++i)
    {
        const float d = moving[static_cast<size_t> (i)] - 2.f * moving[static_cast<size_t> (i - 1)]
                        + moving[static_cast<size_t> (i - 2)];
        peak = std::max (peak, std::abs (d));
    }
    std::fprintf (stderr, "S10 jump %.5f\n", peak);
    CHECK (peak < 0.05f);
}

void testS11()
{
    psyb::PsyEngine engine;
    engine.loadInit();
    useMidi (engine, 0);
    useMidi (engine, 1);
    silenceLane (engine, 2);
    engine.setEngineAudible (0, true);
    engine.setEngineAudible (1, true);
    engine.setSlideParams (slideParams());
    std::vector<float> blockOut (512);
    psyb::MidiIn on = midi (0, 48, 100);
    psyb::Block warm { 48000.0, 145.0, 0.0, 512, false };
    engine.process (warm, &on, 1, blockOut.data());
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kBlocks = 100;
    for (int i = 0; i < kBlocks; ++i)
    {
        psyb::Block block { 48000.0, 145.0, 0.0, 512, false };
        engine.process (block, nullptr, 0, blockOut.data());
    }
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / kBlocks;
    std::fprintf (stderr, "S11 %.3f ms per 512-sample block, Roll + Slide at 2x\n", ms);
    CHECK (ms < 5.0);
}
} // namespace

int runSlideTests()
{
    testS1();
    testS2andS3();
    testS4();
    testS5();
    testS6();
    testS7();
    testS8();
    testS9();
    testS10();
    testS11();
    return fails;
}
