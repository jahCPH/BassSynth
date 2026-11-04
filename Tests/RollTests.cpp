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

psyb::LaneConfig midiLane()
{
    psyb::LaneConfig lane;
    lane.source = psyb::SourceMode::Midi;
    lane.enabled = true;
    return lane;
}

void arm (psyb::PsyEngine& engine, const psyb::RollParams& params)
{
    engine.loadInit();
    engine.setLane (0, midiLane());
    psyb::LaneConfig off;
    off.enabled = false;
    engine.setLane (1, off);
    engine.setLane (2, off);
    engine.setRollParams (params);
}

std::vector<float> render (psyb::PsyEngine& engine, int total, double sr,
                           const std::vector<psyb::MidiIn>& events, bool playing)
{
    std::vector<float> out (static_cast<size_t> (std::max (0, total)));
    int done = 0;
    while (done < total)
    {
        const int n = std::min (512, total - done);
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
        block.bpm = 145.0;
        block.numSamples = n;
        block.playing = playing;
        block.ppqStart = static_cast<double> (done) * block.bpm / (sr * 60.0);
        engine.process (block, here.empty() ? nullptr : here.data(), static_cast<int> (here.size()), out.data() + done);
        done += n;
    }
    return out;
}

psyb::MidiIn note (int sample, int pitch, int velocity)
{
    psyb::MidiIn event {};
    event.sample = sample;
    event.note = pitch;
    event.velocity = velocity;
    return event;
}

float peakOf (const std::vector<float>& x)
{
    float peak = 0.f;
    for (float s : x)
        peak = std::max (peak, std::abs (s));
    return peak;
}

int firstAbove (const std::vector<float>& x, float thresh)
{
    for (int i = 0; i < static_cast<int> (x.size()); ++i)
        if (std::abs (x[static_cast<size_t> (i)]) > thresh)
            return i;
    return -1;
}

float secondDiffPeak (const std::vector<float>& x, int begin, int end)
{
    float peak = 0.f;
    begin = std::max (begin, 2);
    end = std::min (end, static_cast<int> (x.size()));
    for (int i = begin; i < end; ++i)
    {
        const float d = x[static_cast<size_t> (i)] - 2.f * x[static_cast<size_t> (i - 1)] + x[static_cast<size_t> (i - 2)];
        peak = std::max (peak, std::abs (d));
    }
    return peak;
}

void fft (std::vector<float>& re, std::vector<float>& im)
{
    const int n = static_cast<int> (re.size());
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap (re[static_cast<size_t> (i)], re[static_cast<size_t> (j)]);
            std::swap (im[static_cast<size_t> (i)], im[static_cast<size_t> (j)]);
        }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const float ang = -6.283185307179586f / static_cast<float> (len);
        const float wlenRe = std::cos (ang);
        const float wlenIm = std::sin (ang);
        for (int i = 0; i < n; i += len)
        {
            float wRe = 1.f;
            float wIm = 0.f;
            for (int j = 0; j < len / 2; ++j)
            {
                const size_t a = static_cast<size_t> (i + j);
                const size_t b = static_cast<size_t> (i + j + len / 2);
                const float vRe = re[b] * wRe - im[b] * wIm;
                const float vIm = re[b] * wIm + im[b] * wRe;
                re[b] = re[a] - vRe;
                im[b] = im[a] - vIm;
                re[a] += vRe;
                im[a] += vIm;
                const float nextRe = wRe * wlenRe - wIm * wlenIm;
                wIm = wRe * wlenIm + wIm * wlenRe;
                wRe = nextRe;
            }
        }
    }
}

void spectrum (const std::vector<float>& frame, std::vector<float>& re, std::vector<float>& im)
{
    const int n = static_cast<int> (frame.size());
    re.assign (static_cast<size_t> (n), 0.f);
    im.assign (static_cast<size_t> (n), 0.f);
    for (int i = 0; i < n; ++i)
        re[static_cast<size_t> (i)] = frame[static_cast<size_t> (i)];
    fft (re, im);
}

float binPower (const std::vector<float>& re, const std::vector<float>& im, int bin)
{
    return re[static_cast<size_t> (bin)] * re[static_cast<size_t> (bin)]
           + im[static_cast<size_t> (bin)] * im[static_cast<size_t> (bin)];
}

float bandPower (const std::vector<float>& re, const std::vector<float>& im, int first, int last)
{
    const int n = static_cast<int> (re.size());
    float sum = 0.f;
    last = std::min (last, n / 2 - 1);
    for (int k = std::max (1, first); k <= last; ++k)
        sum += re[static_cast<size_t> (k)] * re[static_cast<size_t> (k)] + im[static_cast<size_t> (k)] * im[static_cast<size_t> (k)];
    return sum;
}

std::vector<float> voiceTone (const psyb::RollParams& params, float freq, double sr, int settle, int capture)
{
    psyb::RollVoice voice;
    voice.prime (params);
    voice.start (true, true, nullptr);
    for (int i = 0; i < settle; ++i)
        voice.process (params, sr, freq);
    std::vector<float> out (static_cast<size_t> (capture));
    for (int i = 0; i < capture; ++i)
        out[static_cast<size_t> (i)] = voice.process (params, sr, freq);
    return out;
}

void testR1()
{
    psyb::RollParams params;
    params.phaseReset = true;
    params.filterReset = true;
    params.reso = 0.8f;
    params.drive = 0.5f;

    psyb::PsyEngine a;
    psyb::PsyEngine b;
    arm (a, params);
    arm (b, params);
    const auto left = render (a, 512, 48000.0, { note (0, 60, 100) }, false);
    const auto right = render (b, 512, 48000.0, { note (0, 60, 100) }, false);
    float maxDiff = 0.f;
    for (int i = 0; i < 64; ++i)
        maxDiff = std::max (maxDiff, std::abs (left[static_cast<size_t> (i)] - right[static_cast<size_t> (i)]));
    CHECK (maxDiff < 1.0e-6f);
    CHECK (peakOf (left) > 1.0e-3f);

    params.asus = 1.f;
    params.adecMs = 4000.f;
    params.fsus = 1.f;
    params.fdecMs = 4000.f;
    params.phaseReset = false;
    params.filterReset = false;
    psyb::PsyEngine carried;
    arm (carried, params);
    const int again = 9600;
    auto warm = render (carried, again + 256, 48000.0,
                        { note (0, 48, 100), note (again, 48, 0), note (again + 64, 48, 100) }, false);
    psyb::PsyEngine cold;
    arm (cold, params);
    auto fresh = render (cold, 256, 48000.0, { note (0, 48, 100) }, false);
    float carryDiff = 0.f;
    for (int i = 0; i < 256; ++i)
        carryDiff = std::max (carryDiff, std::abs (warm[static_cast<size_t> (again + 64 + i)] - fresh[static_cast<size_t> (i)]));
    CHECK (carryDiff > 1.0e-3f);

    params.asus = 1.f;
    params.adecMs = 4000.f;
    params.fsus = 1.f;
    params.fdecMs = 4000.f;
    params.phaseReset = true;
    params.filterReset = false;
    psyb::PsyEngine inherit;
    psyb::PsyEngine cleared;
    arm (inherit, params);
    params.filterReset = true;
    arm (cleared, params);
    const auto inherited = render (inherit, again + 512, 48000.0,
                                   { note (0, 36, 100), note (again, 36, 0), note (again + 64, 36, 100) }, false);
    const auto reset = render (cleared, again + 512, 48000.0,
                               { note (0, 36, 100), note (again, 36, 0), note (again + 64, 36, 100) }, false);
    float inheritDiff = 0.f;
    for (int i = 64; i < 512; ++i)
        inheritDiff = std::max (inheritDiff, std::abs (inherited[static_cast<size_t> (again + 64 + i)] - reset[static_cast<size_t> (again + 64 + i)]));
    CHECK (inheritDiff > 1.0e-4f);
}

void testR2()
{
    psyb::PsyEngine engine;
    engine.loadInit();
    const double sr = 48000.0;
    const double bpm = 145.0;
    const int expect = psyb::sampleAtPpq (psyb::stepStartPpq (1, 0.25, 0.0), 0.0, bpm, sr);
    const int total = expect + 400;
    std::vector<float> out (static_cast<size_t> (total));
    psyb::MidiIn on = note (0, 60, 100);
    int done = 0;
    while (done < total)
    {
        const int n = std::min (256, total - done);
        psyb::Block block;
        block.sampleRate = sr;
        block.bpm = bpm;
        block.numSamples = n;
        block.playing = true;
        block.ppqStart = static_cast<double> (done) * bpm / (sr * 60.0);
        const bool send = on.sample >= done && on.sample < done + n;
        if (send)
            on.sample -= done;
        engine.process (block, send ? &on : nullptr, send ? 1 : 0, out.data() + done);
        if (send)
            on.sample = -1;
        done += n;
    }
    const int onset = firstAbove (out, 1.0e-3f);
    std::fprintf (stderr, "R2 onset %d scheduled %d latency %d delta %d\n",
                  onset, expect, psyb::kRollOversampleLatency, onset - expect);
    CHECK (onset >= expect);
    CHECK (std::abs ((onset - expect) - psyb::kRollOversampleLatency) <= 8);
}

void testR3()
{
    psyb::PsyEngine engine;
    arm (engine, {});
    const double sr = 48000.0;
    const int off = psyb::sampleAtPpq (0.125, 0.0, 145.0, sr);
    const int total = static_cast<int> (sr * 0.2);
    const auto out = render (engine, total, sr, { note (0, 60, 100), note (off, 60, 0) }, false);
    const float peak = peakOf (out);
    CHECK (peak > 1.0e-3f);
    const float gate = peak * 0.001f;
    int last = 0;
    for (int i = 0; i < total; ++i)
        if (std::abs (out[static_cast<size_t> (i)]) > gate)
            last = i;
    const double ms = 1000.0 * static_cast<double> (last) / sr;
    std::fprintf (stderr, "R3 quiet by %.2f ms (gate off %d)\n", ms, off);
    CHECK (ms <= 66.0);
}

void testR4()
{
    constexpr int n = 4096;
    const double sr = 48000.0;
    psyb::RollParams params;
    params.cutoffHz = 18000.f;
    params.reso = 0.f;
    params.envAmt = 0.f;
    params.drive = 1.f;
    params.sub = 0.f;
    params.asus = 1.f;
    params.fsus = 1.f;
    params.adecMs = 4000.f;
    params.fdecMs = 4000.f;
    const int bins[] = { 85, 171, 256, 341, 427 };
    for (int bin : bins)
    {
        const float freq = static_cast<float> (bin) * static_cast<float> (sr) / static_cast<float> (n);
            const auto frame = voiceTone (params, freq, sr, static_cast<int> (sr * 0.1), n);
        std::vector<float> re, im;
        spectrum (frame, re, im);
        const float fund = binPower (re, im, bin);
        // Bin-centered, unwindowed. The fundamental's own bin is excluded.
        const int below = static_cast<int> (1000.0 / (sr / n)) - 1;
        const float low = bandPower (re, im, 1, std::min (below, bin - 1));
        const float db = 10.f * std::log10 (std::max (low, 1.0e-20f) / std::max (fund, 1.0e-20f));
        std::fprintf (stderr, "R4 %.0f Hz folded %.1f dB\n", freq, db);
        CHECK (db <= -50.f);
    }
}

void testR5()
{
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const float cuts[] = { 20.f, 100.f, 900.f, 5000.f, 20000.f };
    const float reson[] = { 0.f, 0.5f, 1.f };
    for (double sr : rates)
    {
        for (float cut : cuts)
        {
            for (float reso : reson)
            {
                psyb::RollParams params;
                params.cutoffHz = cut;
                params.reso = reso;
                params.drive = 1.f;
                params.envAmt = 1.f;
                psyb::PsyEngine engine;
                arm (engine, params);
                const int total = static_cast<int> (sr * 10.0);
                const int gap = static_cast<int> (sr * 0.25);
                std::vector<psyb::MidiIn> events;
                for (int t = 0; t + 64 < total; t += gap)
                {
                    events.push_back (note (t, 40 + (t / gap) % 24, 100));
                    events.push_back (note (t + gap / 2, 40 + (t / gap) % 24, 0));
                }
                const auto out = render (engine, total, sr, events, false);
                float peak = 0.f;
                bool bad = false;
                for (float s : out)
                {
                    if (! std::isfinite (s))
                        bad = true;
                    peak = std::max (peak, std::abs (s));
                }
                if (bad || peak > 4.f)
                    std::fprintf (stderr, "R5 sr %.0f cut %.0f reso %.2f peak %.3f bad %d\n", sr, cut, reso, peak, bad ? 1 : 0);
                CHECK (! bad);
                CHECK (peak <= 4.f);
            }
        }
    }
}

void testR6()
{
    // Onset roughness is the peak |second difference| in the 4 ms after the
    // retrigger, divided by the same measure on a settled cycle. A factor of
    // 4 covers the blep + filter reset without counting a click. R7 uses it.
    constexpr float kClick = 4.f;
    psyb::RollParams params;
    params.reso = 1.f;
    params.drive = 1.f;
    params.cutoffHz = 900.f;
    params.asus = 1.f;
    params.fsus = 1.f;
    params.adecMs = 4000.f;
    params.fdecMs = 4000.f;
    psyb::PsyEngine engine;
    arm (engine, params);
    const int settle = 12000;
    const int again = settle;
    const auto out = render (engine, again + 4000, 48000.0,
                             { note (0, 48, 100), note (again - 80, 48, 0), note (again, 48, 100) }, false);
    const float steady = secondDiffPeak (out, settle - 2000, settle - 500);
    const float onset = secondDiffPeak (out, again + psyb::kRollOversampleLatency, again + psyb::kRollOversampleLatency + 192);
    std::fprintf (stderr, "R6 onset %.5f steady %.5f ratio %.2f\n", onset, steady, onset / std::max (steady, 1.0e-8f));
    CHECK (steady > 0.f);
    CHECK (onset <= steady * kClick);
}

void testR7()
{
    constexpr float kClick = 4.f;
    psyb::RollParams low;
    low.cutoffHz = 20.f;
    low.reso = 0.8f;
    low.drive = 0.4f;
    low.asus = 1.f;
    low.adecMs = 4000.f;
    psyb::PsyEngine jump;
    arm (jump, low);
    const int at = 8000;
    std::vector<float> moving (static_cast<size_t> (at + 4000));
    {
        int done = 0;
        const auto events = std::vector<psyb::MidiIn> { note (0, 45, 100) };
        while (done < at + 4000)
        {
            if (done == at)
            {
                auto high = low;
                high.cutoffHz = 20000.f;
                jump.setRollParams (high);
            }
            const int n = std::min (512, at + 4000 - done);
            psyb::Block block { 48000.0, 145.0, 0.0, n, false };
            std::vector<psyb::MidiIn> here;
            for (auto event : events)
                if (event.sample >= done && event.sample < done + n)
                {
                    event.sample -= done;
                    here.push_back (event);
                }
            jump.process (block, here.empty() ? nullptr : here.data(), static_cast<int> (here.size()), moving.data() + done);
            done += n;
        }
    }
    psyb::RollParams high = low;
    high.cutoffHz = 20000.f;
    psyb::PsyEngine steady;
    arm (steady, high);
    const auto held = render (steady, at + 4000, 48000.0, { note (0, 45, 100) }, false);
    const float dest = secondDiffPeak (held, at, at + 2000);
    const float edge = secondDiffPeak (moving, at, at + 2000);
    std::fprintf (stderr, "R7 jump %.5f steady %.5f\n", edge, dest);
    CHECK (edge <= std::max (dest, 1.0e-6f) * kClick);
}

void testR8()
{
    constexpr int n = 8192;
    const double sr = 48000.0;
    const float freq = 200.f * static_cast<float> (sr) / static_cast<float> (n);
    const int fund = 200;
    const int sub = 100;
    psyb::RollParams dry;
    dry.sub = 0.f;
    dry.drive = 0.f;
    dry.cutoffHz = 8000.f;
    dry.reso = 0.f;
    dry.envAmt = 0.f;
    dry.asus = 1.f;
    dry.fsus = 1.f;
    dry.adecMs = 4000.f;
    auto off = voiceTone (dry, freq, sr, 4000, n);
    std::vector<float> re, im;
    spectrum (off, re, im);
    const float offSub = binPower (re, im, sub);
    const float offFund = binPower (re, im, fund);
    std::fprintf (stderr, "R8 sub off ratio %.2f dB\n", 10.f * std::log10 (std::max (offSub, 1.0e-20f) / std::max (offFund, 1.0e-20f)));
    CHECK (offSub < offFund * 1.0e-4f);

    dry.sub = 0.70f;
    auto on = voiceTone (dry, freq, sr, 4000, n);
    spectrum (on, re, im);
    const float onSub = binPower (re, im, sub);
    const float onFund = binPower (re, im, fund);
    CHECK (onSub > onFund * 1.0e-3f);

    dry.cutoffHz = 20.f;
    dry.sub = 0.70f;
    auto low = voiceTone (dry, freq, sr, 8000, n);
    spectrum (low, re, im);
    const float lowSub = binPower (re, im, sub);
    const float lowFund = binPower (re, im, fund);
    std::fprintf (stderr, "R8 low cut sub/fund %.2f dB\n", 10.f * std::log10 (std::max (lowSub, 1.0e-20f) / std::max (lowFund, 1.0e-20f)));
    CHECK (lowSub > lowFund);
}

void testR9()
{
    psyb::PsyEngine engine;
    arm (engine, {});
    std::vector<float> blockOut (512);
    psyb::MidiIn on = note (0, 48, 100);
    psyb::Block warm { 48000.0, 145.0, 0.0, 512, false };
    engine.process (warm, &on, 1, blockOut.data());
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kBlocks = 200;
    for (int i = 0; i < kBlocks; ++i)
    {
        psyb::Block block { 48000.0, 145.0, 0.0, 512, false };
        engine.process (block, nullptr, 0, blockOut.data());
    }
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / kBlocks;
    std::fprintf (stderr, "R9 %.3f ms per 512-sample block at 2x\n", ms);
    CHECK (ms < 5.0);
}
} // namespace

int runRollTests()
{
    testR1();
    testR2();
    testR3();
    testR4();
    testR5();
    testR6();
    testR7();
    testR8();
    testR9();
    return fails;
}
