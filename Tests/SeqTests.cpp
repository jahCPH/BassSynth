#include "Dsp/ReportedLatency.h"
#include "Engine/Sequencer.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace
{
int gFails = 0;

void check (bool ok, const char* text, int line)
{
    if (ok)
        return;
    std::fprintf (stderr, "FAIL %d: %s\n", line, text);
    ++gFails;
}

#define CHECK(cond) check ((cond), #cond, __LINE__)

void noteOn (psyb::PsyEngine& engine, int note, int sample = 0)
{
    psyb::MidiIn ev {};
    ev.sample = sample;
    ev.note = note;
    ev.velocity = 100;
    psyb::Block block { 48000.0, 145.0, 0.0, sample + 1, false };
    engine.process (block, &ev, 1, nullptr);
}

void allOn (psyb::PsyEngine& engine, int lane = 0)
{
    for (int i = 0; i < psyb::kMaxSteps; ++i)
    {
        psyb::Step step {};
        step.on = true;
        step.gate = 0.5f;
        step.velocity = 100;
        engine.setStep (lane, i, step);
    }
}

std::vector<psyb::Trigger> playSteps (psyb::PsyEngine& engine, int steps, int blockSize, double sr, double bpm, double swing = 0.0)
{
    engine.setGlobalSwing (swing);
    std::vector<psyb::Trigger> out;
    const double rate = 0.25;
    const double last = psyb::stepStartPpq (steps - 1, rate, swing);
    const int lastSample = psyb::sampleAtPpq (last, 0.0, bpm, sr);
    int global = 0;
    while (global <= lastSample + blockSize)
    {
        psyb::Block block;
        block.sampleRate = sr;
        block.bpm = bpm;
        block.numSamples = blockSize;
        block.playing = true;
        block.ppqStart = static_cast<double> (global) * bpm / (sr * 60.0);
        engine.process (block, nullptr, 0, nullptr);
        for (int i = 0; i < engine.triggerCount(); ++i)
        {
            auto t = engine.trigger (i);
            t.sample += global;
            out.push_back (t);
        }
        global += blockSize;
    }
    return out;
}
} // namespace

int main()
{
    if (psyb::hostReportedLatencySamples() != 0)
    {
        std::fputs ("hostReportedLatencySamples must stay 0 until oversampling exists\n", stderr);
        return 1;
    }

    CHECK (psyb::sampleAtPpq (0.25, 0.0, 145.0, 48000.0) == 4965);

    // A1 / A2. Onset sample is the PPQ sample, for every block size and rate.
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int blocks[] = { 1, 7, 64, 512, 2048 };
    for (double sr : rates)
    {
        for (int block : blocks)
        {
            psyb::PsyEngine engine;
            engine.loadInit();
            allOn (engine);
            noteOn (engine, 60);
            const auto got = playSteps (engine, 8, block, sr, 145.0);
            CHECK (got.size() == 8);
            for (int n = 0; n < 8 && n < static_cast<int> (got.size()); ++n)
            {
                const int expect = psyb::sampleAtPpq (psyb::stepStartPpq (n, 0.25, 0.0), 0.0, 145.0, sr);
                CHECK (got[static_cast<size_t> (n)].sample == expect);
                CHECK (got[static_cast<size_t> (n)].retrigger);
            }
        }
    }

    {
        psyb::PsyEngine engine;
        engine.loadInit();
        allOn (engine);
        noteOn (engine, 60);
        const auto got = playSteps (engine, 4, 64, 48000.0, 145.0, 0.5);
        CHECK (got.size() == 4);
        const int odd = psyb::sampleAtPpq (psyb::stepStartPpq (1, 0.25, 0.5), 0.0, 145.0, 48000.0);
        if (got.size() > 1)
            CHECK (got[1].sample == odd);
    }

    // Loop jump and tempo change stay on the block's own PPQ clock.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        allOn (engine);
        noteOn (engine, 60);
        psyb::Block block { 48000.0, 145.0, 0.0, 64, true };
        engine.process (block, nullptr, 0, nullptr);
        block.ppqStart = 0.50;
        block.bpm = 90.0;
        engine.process (block, nullptr, 0, nullptr);
        bool found = false;
        const double start = psyb::stepStartPpq (2, 0.25, 0.0);
        const int expect = psyb::sampleAtPpq (start, block.ppqStart, block.bpm, block.sampleRate);
        for (int i = 0; i < engine.triggerCount(); ++i)
            if (engine.trigger (i).sample == expect)
                found = true;
        CHECK (found);
    }

    // A5.
    const int chord[] = { 60, 64, 67 };
    auto runOrder = [&] (psyb::ArpOrder order, int count)
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        engine.setOrder (order);
        allOn (engine);
        for (int note : chord)
            noteOn (engine, note);
        return playSteps (engine, count, 128, 48000.0, 145.0);
    };

    {
        const auto up = runOrder (psyb::ArpOrder::Up, 6);
        const int expect[] = { 60, 64, 67, 60, 64, 67 };
        CHECK (up.size() == 6);
        for (int i = 0; i < 6 && i < static_cast<int> (up.size()); ++i)
            CHECK (up[static_cast<size_t> (i)].note == expect[i]);
    }
    {
        const auto down = runOrder (psyb::ArpOrder::Down, 6);
        const int expect[] = { 67, 64, 60, 67, 64, 60 };
        CHECK (down.size() == 6);
        for (int i = 0; i < 6 && i < static_cast<int> (down.size()); ++i)
            CHECK (down[static_cast<size_t> (i)].note == expect[i]);
    }
    {
        const auto upDown = runOrder (psyb::ArpOrder::UpDown, 6);
        const int expect[] = { 60, 64, 67, 64, 60, 64 };
        CHECK (upDown.size() == 6);
        for (int i = 0; i < 6 && i < static_cast<int> (upDown.size()); ++i)
            CHECK (upDown[static_cast<size_t> (i)].note == expect[i]);
    }
    {
        const auto played = runOrder (psyb::ArpOrder::AsPlayed, 6);
        const int expect[] = { 60, 64, 67, 60, 64, 67 };
        CHECK (played.size() == 6);
        for (int i = 0; i < 6 && i < static_cast<int> (played.size()); ++i)
            CHECK (played[static_cast<size_t> (i)].note == expect[i]);
    }
    {
        const auto a = runOrder (psyb::ArpOrder::Random, 12);
        const auto b = runOrder (psyb::ArpOrder::Random, 12);
        CHECK (a.size() == 12 && b.size() == 12);
        int distinct = 0;
        for (int i = 0; i < 12 && i < static_cast<int> (a.size()); ++i)
        {
            CHECK (a[static_cast<size_t> (i)].note == b[static_cast<size_t> (i)].note);
            CHECK (a[static_cast<size_t> (i)].note == 60 || a[static_cast<size_t> (i)].note == 64 || a[static_cast<size_t> (i)].note == 67);
            if (i > 0 && a[static_cast<size_t> (i)].note != a[0].note)
                ++distinct;
        }
        CHECK (distinct > 0);
    }

    // Rests and ties do not advance.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        engine.setOrder (psyb::ArpOrder::Up);
        for (int i = 0; i < 4; ++i)
        {
            psyb::Step step {};
            step.on = (i % 2) == 0;
            step.gate = 0.5f;
            step.velocity = 100;
            engine.setStep (0, i, step);
        }
        for (int note : chord)
            noteOn (engine, note);
        const auto got = playSteps (engine, 4, 128, 48000.0, 145.0);
        CHECK (got.size() == 2);
        if (got.size() == 2)
        {
            CHECK (got[0].note == 60);
            CHECK (got[1].note == 64);
        }
    }
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        engine.setOrder (psyb::ArpOrder::Up);
        for (int i = 0; i < 3; ++i)
        {
            psyb::Step step {};
            step.on = true;
            step.tie = (i == 0);
            step.gate = 0.5f;
            step.velocity = 100;
            engine.setStep (0, i, step);
        }
        for (int note : chord)
            noteOn (engine, note);
        const auto got = playSteps (engine, 3, 128, 48000.0, 145.0);
        CHECK (got.size() == 3);
        if (got.size() == 3)
        {
            CHECK (got[0].note == 60 && got[0].retrigger);
            CHECK (got[1].note == 60 && ! got[1].retrigger);
            CHECK (got[2].note == 64 && got[2].retrigger);
        }
    }

    // Per-lane index.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        auto cfg = engine.lane (1);
        cfg.enabled = true;
        cfg.length = 4;
        engine.setLane (1, cfg);
        allOn (engine, 0);
        for (int i = 0; i < 4; ++i)
        {
            psyb::Step step {};
            step.on = (i == 0 || i == 3);
            step.gate = 0.5f;
            step.velocity = 100;
            engine.setStep (1, i, step);
        }
        for (int note : chord)
            noteOn (engine, note);
        const auto got = playSteps (engine, 4, 128, 48000.0, 145.0);
        int lane1[4] = {};
        int n1 = 0;
        for (const auto& t : got)
            if (t.engine == 1 && n1 < 4)
                lane1[n1++] = t.note;
        CHECK (n1 == 2);
        CHECK (lane1[0] == 60 && lane1[1] == 64);
    }

    // A6.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        auto cfg = engine.lane (0);
        cfg.transpose = 24;
        engine.setLane (0, cfg);
        psyb::Step step {};
        step.on = true;
        step.gate = 1.f;
        step.offset = 24;
        step.velocity = 100;
        engine.setStep (0, 0, step);
        noteOn (engine, 60);
        psyb::Block block { 48000.0, 145.0, 0.0, 32, true };
        engine.process (block, nullptr, 0, nullptr);
        CHECK (engine.triggerCount() >= 1);
        if (engine.triggerCount() >= 1)
            CHECK (engine.trigger (0).note == 108);

        step.offset = 80;
        engine.setStep (0, 0, step);
        cfg.transpose = 20;
        engine.setLane (0, cfg);
        engine.process (block, nullptr, 0, nullptr);
        CHECK (engine.triggerCount() >= 1);
        if (engine.triggerCount() >= 1)
            CHECK (engine.trigger (0).note == 127);

        engine.reset();
        engine.loadInit();
        cfg = engine.lane (0);
        cfg.transpose = -20;
        engine.setLane (0, cfg);
        step.offset = -60;
        engine.setStep (0, 0, step);
        noteOn (engine, 10);
        engine.process (block, nullptr, 0, nullptr);
        CHECK (engine.triggerCount() >= 1);
        if (engine.triggerCount() >= 1)
            CHECK (engine.trigger (0).note == 0);
    }

    // A8. SEQ glide clamps to the destination gate. Direct MIDI glide does not.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        auto cfg = engine.lane (0);
        cfg.seqGlide = 0.8f;
        engine.setLane (0, cfg);
        psyb::Step tied {};
        tied.on = true;
        tied.tie = true;
        tied.gate = 0.5f;
        tied.velocity = 100;
        engine.setStep (0, 0, tied);
        psyb::Step next = tied;
        next.tie = false;
        next.gate = 0.5f;
        engine.setStep (0, 1, next);
        noteOn (engine, 60);
        const auto got = playSteps (engine, 2, 128, 48000.0, 145.0);
        CHECK (got.size() == 2);
        if (got.size() == 2)
        {
            const auto& g = got[1].glide;
            CHECK (g.applies);
            CHECK (g.clamped);
            CHECK (g.requestedFraction > g.effectiveFraction);
            CHECK (g.effectiveMs < g.requestedMs);
            CHECK (! got[0].glide.applies);
        }

        engine.loadInit();
        cfg.seqGlide = 0.1f;
        engine.setLane (0, cfg);
        engine.setStep (0, 0, tied);
        engine.setStep (0, 1, next);
        noteOn (engine, 60);
        const auto loose = playSteps (engine, 2, 128, 48000.0, 145.0);
        CHECK (loose.size() == 2);
        if (loose.size() == 2)
            CHECK (loose[1].glide.applies && ! loose[1].glide.clamped);

        engine.loadInit();
        cfg = engine.lane (0);
        cfg.source = psyb::SourceMode::Midi;
        cfg.midiGlideMs = 5000.f;
        engine.setLane (0, cfg);
        psyb::MidiIn pair[2] = {};
        pair[0].note = 60;
        pair[0].velocity = 100;
        pair[1].sample = 8;
        pair[1].note = 67;
        pair[1].velocity = 100;
        psyb::Block block { 48000.0, 145.0, 0.0, 16, false };
        engine.process (block, pair, 2, nullptr);
        CHECK (engine.triggerCount() == 2);
        if (engine.triggerCount() == 2)
        {
            CHECK (! engine.trigger (1).glide.clamped);
            CHECK (engine.trigger (1).glide.effectiveMs == 5000.f);
            CHECK (engine.trigger (1).glide.applies);
        }
    }

    // A4.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        engine.setLatch (true);
        allOn (engine);
        noteOn (engine, 60);
        psyb::MidiIn off {};
        off.note = 60;
        psyb::Block idle { 48000.0, 145.0, 0.0, 1, false };
        engine.process (idle, &off, 1, nullptr);
        const auto held = playSteps (engine, 4, 128, 48000.0, 145.0);
        CHECK (held.size() == 4);

        noteOn (engine, 67);
        const auto replaced = playSteps (engine, 1, 128, 48000.0, 145.0);
        CHECK (! replaced.empty() && replaced[0].note == 67);

        psyb::MidiIn release {};
        release.note = 67;
        engine.process (idle, &release, 1, nullptr);
        engine.setLatch (false);
        const auto cleared = playSteps (engine, 2, 128, 48000.0, 145.0);
        CHECK (cleared.empty());
    }
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        engine.setLatch (true);
        allOn (engine);
        noteOn (engine, 60);
        psyb::MidiIn off {};
        off.note = 60;
        psyb::Block idle { 48000.0, 145.0, 0.0, 1, false };
        engine.process (idle, &off, 1, nullptr);
        psyb::MidiIn cc {};
        cc.cc = 123;
        engine.process (idle, &cc, 1, nullptr);
        CHECK (engine.heldCount() == 0);
        psyb::Block release { 48000.0, 145.0, 0.0, 8000, false };
        engine.process (release, nullptr, 0, nullptr);
        CHECK (engine.soundingVoices() == 0);
    }
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        engine.setLatch (true);
        allOn (engine);
        noteOn (engine, 60);
        psyb::MidiIn off {};
        off.note = 60;
        psyb::Block idle { 48000.0, 145.0, 0.0, 1, false };
        engine.process (idle, &off, 1, nullptr);
        psyb::Block playing { 48000.0, 145.0, 0.0, 32, true };
        engine.process (playing, nullptr, 0, nullptr);
        psyb::Block stopped { 48000.0, 145.0, 0.5, 32, false };
        engine.process (stopped, nullptr, 0, nullptr);
        CHECK (engine.heldCount() == 0);
        const auto after = playSteps (engine, 2, 128, 48000.0, 145.0);
        CHECK (after.empty());
    }

    // A7. Stop releases SEQ notes and leaves direct MIDI and the route.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        psyb::Step step {};
        step.on = true;
        step.gate = 1.f;
        step.velocity = 100;
        engine.setStep (0, 0, step);
        auto midiLane = engine.lane (1);
        midiLane.enabled = true;
        midiLane.source = psyb::SourceMode::Midi;
        engine.setLane (1, midiLane);

        psyb::MidiIn on {};
        on.note = 60;
        on.velocity = 100;
        psyb::Block playing { 48000.0, 145.0, 0.0, 64, true };
        engine.process (playing, &on, 1, nullptr);
        CHECK (engine.voiceActive (0));
        CHECK (engine.voiceActive (1));
        CHECK (engine.routeHeld (0, 60));

        psyb::Block stopped { 48000.0, 145.0, 1.0, 32, false };
        engine.process (stopped, nullptr, 0, nullptr);
        CHECK (engine.voiceReleasing (0));
        CHECK (engine.voiceActive (1));
        CHECK (engine.routeHeld (0, 60));
        CHECK (engine.triggerCount() == 0);
    }

    // INIT Roll is K-B-B-B. Slide and FX stay silent.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        noteOn (engine, 60);
        const auto got = playSteps (engine, 16, 128, 48000.0, 145.0);
        CHECK (got.size() == 12);
        for (const auto& t : got)
            CHECK (t.engine == 0);
    }

    // A tie into a rest, including the pattern wrap, ends the note.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        for (int i = 0; i < psyb::kMaxSteps; ++i)
        {
            psyb::Step step {};
            step.on = (i == 0);
            step.tie = (i == 0);
            step.gate = 0.5f;
            step.velocity = 100;
            engine.setStep (0, i, step);
        }
        noteOn (engine, 60);
        psyb::Block first { 48000.0, 145.0, 0.0, 64, true };
        engine.process (first, nullptr, 0, nullptr);
        CHECK (engine.voiceActive (0));
        psyb::Block rest { 48000.0, 145.0, 0.25, 64, true };
        engine.process (rest, nullptr, 0, nullptr);
        CHECK (! engine.voiceActive (0));
    }
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        auto cfg = engine.lane (0);
        cfg.length = 4;
        engine.setLane (0, cfg);
        for (int i = 0; i < 4; ++i)
        {
            psyb::Step step {};
            step.on = (i == 3);
            step.tie = (i == 3);
            step.gate = 0.5f;
            step.velocity = 100;
            engine.setStep (0, i, step);
        }
        noteOn (engine, 60);
        const auto got = playSteps (engine, 5, 128, 48000.0, 145.0);
        CHECK (! got.empty());
        CHECK (! engine.voiceActive (0));
    }
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        for (int i = 0; i < 2; ++i)
        {
            psyb::Step step {};
            step.on = true;
            step.tie = (i == 0);
            step.gate = 1.f;
            step.velocity = 100;
            engine.setStep (0, i, step);
        }
        noteOn (engine, 60);
        psyb::Block first { 48000.0, 145.0, 0.0, 64, true };
        engine.process (first, nullptr, 0, nullptr);
        CHECK (engine.voiceActive (0));
        psyb::MidiIn off {};
        off.note = 60;
        engine.process (first, &off, 1, nullptr);
        CHECK (! engine.voiceActive (0));
    }

    // A3. PPQ advances, and the run ends on matching note-offs only.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        allOn (engine);
        psyb::Step tied {};
        tied.on = true;
        tied.tie = true;
        tied.gate = 0.5f;
        tied.velocity = 100;
        engine.setStep (0, 4, tied);
        psyb::Step rest {};
        rest.velocity = 100;
        engine.setStep (0, 5, rest);

        struct Key { int channel; int note; };
        std::vector<Key> live;
        auto remember = [&] (int channel, int note)
        {
            for (const auto& key : live)
                if (key.channel == channel && key.note == note)
                    return;
            live.push_back ({ channel, note });
        };
        auto forget = [&] (int channel, int note)
        {
            live.erase (std::remove_if (live.begin(), live.end(), [&] (const Key& key)
            {
                return key.channel == channel && key.note == note;
            }), live.end());
        };

        uint32_t rng = 1;
        double ppq = 0.0;
        const double sr = 48000.0;
        const double bpm = 145.0;
        const int blockSize = 32;
        for (int i = 0; i < 10000; ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            const int kind = static_cast<int> (rng % 7u);
            psyb::Block block;
            block.sampleRate = sr;
            block.bpm = bpm;
            block.ppqStart = ppq;
            block.numSamples = blockSize;
            block.playing = (rng & 8u) != 0;

            psyb::MidiIn ev {};
            ev.sample = static_cast<int> ((rng >> 8) % 32u);
            ev.channel = static_cast<int> ((rng >> 4) % 16u);
            ev.note = static_cast<int> ((rng >> 12) % 128u);
            ev.velocity = 100;

            if (kind == 0)
            {
                remember (ev.channel, ev.note);
                engine.process (block, &ev, 1, nullptr);
            }
            else if (kind == 1)
            {
                ev.velocity = 0;
                forget (ev.channel, ev.note);
                engine.process (block, &ev, 1, nullptr);
            }
            else if (kind == 2)
            {
                ev.cc = 123;
                live.clear();
                engine.process (block, &ev, 1, nullptr);
            }
            else if (kind == 3)
            {
                auto cfg = engine.lane (0);
                cfg.source = (cfg.source == psyb::SourceMode::Seq) ? psyb::SourceMode::Midi : psyb::SourceMode::Seq;
                engine.setLane (0, cfg);
                engine.process (block, nullptr, 0, nullptr);
            }
            else if (kind == 4)
            {
                engine.setDuckSource (engine.routeHeld (0, 36) ? psyb::DuckSource::Internal : psyb::DuckSource::Midi);
                engine.process (block, nullptr, 0, nullptr);
            }
            else if (kind == 5)
            {
                engine.setLatch ((rng & 1u) != 0);
                engine.process (block, nullptr, 0, nullptr);
            }
            else
            {
                block.playing = false;
                engine.process (block, nullptr, 0, nullptr);
            }

            ppq += static_cast<double> (blockSize) * bpm / (sr * 60.0);
        }

        engine.setLatch (false);
        for (size_t i = 0; i < live.size();)
        {
            psyb::MidiIn offs[32] = {};
            int count = 0;
            while (i < live.size() && count < 32)
            {
                offs[count].channel = live[i].channel;
                offs[count].note = live[i].note;
                ++count;
                ++i;
            }
            psyb::Block block { sr, bpm, ppq, blockSize, true };
            engine.process (block, offs, count, nullptr);
            ppq += static_cast<double> (blockSize) * bpm / (sr * 60.0);
        }

        const int runoutSamples = static_cast<int> (sr * 60.0 * 4.0 / bpm);
        psyb::Block runout { sr, bpm, ppq, runoutSamples, true };
        engine.process (runout, nullptr, 0, nullptr);

        CHECK (engine.heldCount() == 0);
        for (int e = 0; e < psyb::kEngineCount; ++e)
            CHECK (! engine.voiceActive (e));
        bool anyRoute = false;
        for (int ch = 0; ch < 16 && ! anyRoute; ++ch)
            for (int n = 0; n < 128; ++n)
                if (engine.routeHeld (ch, n))
                    anyRoute = true;
        CHECK (! anyRoute);
    }

    // CC120 cuts sounding voices in the same block. CC123 releases them.
    {
        psyb::PsyEngine engine;
        engine.loadInit();
        auto cfg = engine.lane (0);
        cfg.source = psyb::SourceMode::Midi;
        engine.setLane (0, cfg);
        noteOn (engine, 60);
        psyb::Block drain { 48000.0, 145.0, 0.0, 64, false };
        engine.process (drain, nullptr, 0, nullptr);
        CHECK (engine.soundingVoices() > 0);
        psyb::MidiIn cut {};
        cut.cc = 120;
        engine.process (drain, &cut, 1, nullptr);
        CHECK (engine.soundingVoices() == 0);
        CHECK (! engine.routeHeld (0, 60));
    }

    if (gFails != 0)
    {
        std::fprintf (stderr, "%d checks failed\n", gFails);
        return 1;
    }
    std::puts ("seq tests passed");
    return 0;
}
