#include "Engine/Sequencer.h"

#include <algorithm>
#include <cmath>

namespace psyb
{
namespace
{
constexpr double kAttackSec = 0.0005;
constexpr double kDecaySec = 0.070;
constexpr double kReleaseSec = 0.012;
constexpr double kTailSec = 0.002;
constexpr double kStealSec = 0.0003;
constexpr double kSilence = 1.0e-3;

double midiFreq (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}

int samplesFor (double seconds, double rate)
{
    return std::max (1, static_cast<int> (std::lround (seconds * rate)));
}
} // namespace

double stepStartPpq (int64_t stepIndex, double rateQuarters, double swing)
{
    const double clamped = std::clamp (swing, 0.0, 0.75);
    double ppq = static_cast<double> (stepIndex) * rateQuarters;
    if ((stepIndex & 1) != 0)
        ppq += clamped * rateQuarters * 0.5;
    return ppq;
}

int sampleAtPpq (double ppq, double ppqOrigin, double bpm, double sampleRate)
{
    const double samples = (ppq - ppqOrigin) * sampleRate * 60.0 / bpm;
    return static_cast<int> (std::floor (samples + 1.0e-7));
}

int clampMidi (int note)
{
    return std::clamp (note, 0, 127);
}

void PsyEngine::reset()
{
    duckSource = DuckSource::Internal;
    duckNote = 36;
    order = ArpOrder::Up;
    latch = false;
    globalSwing = 0.0;
    nHeld = 0;
    downCount = 0;
    triggersN = 0;
    transportWasPlaying = false;
    renderRate = 48000.0;
    rollTarget = {};
    rollPrimed = false;
    for (auto& voice : rollVoice)
        voice.reset();
    std::fill (&route[0][0], &route[0][0] + 16 * 128, static_cast<uint8_t> (0));
    for (int e = 0; e < kEngineCount; ++e)
    {
        lanes[e] = {};
        lanes[e].cfg.length = 16;
        lanes[e].cfg.rateQuarters = 0.25;
        midiStackN[e] = 0;
    }
}

void PsyEngine::loadInit()
{
    reset();
    for (int e = 0; e < kEngineCount; ++e)
    {
        lanes[e].cfg.enabled = (e == 0);
        for (int s = 0; s < kMaxSteps; ++s)
        {
            const int oneBased = s + 1;
            lanes[e].steps[s].on = (e == 0) && ((oneBased % 4) != 1);
            lanes[e].steps[s].gate = 0.5f;
            lanes[e].steps[s].velocity = 100;
        }
    }
}

void PsyEngine::setOrder (ArpOrder next) { order = next; }

void PsyEngine::setLatch (bool on)
{
    latch = on;
    if (latch)
        return;

    nHeld = 0;
    for (int i = 0; i < downCount && nHeld < kMaxHeld; ++i)
        held[nHeld++] = down[i];
    releaseOpenTies();
}

void PsyEngine::setDuckSource (DuckSource source) { duckSource = source; }
void PsyEngine::setDuckNote (int note) { duckNote = note; }
void PsyEngine::setGlobalSwing (double swing) { globalSwing = std::clamp (swing, 0.0, 0.75); }
void PsyEngine::setLane (int engine, const LaneConfig& lane) { lanes[engine].cfg = lane; }
void PsyEngine::setStep (int engine, int index, const Step& step) { lanes[engine].steps[index] = step; }

void PsyEngine::setRollParams (const RollParams& params)
{
    if (! rollPrimed)
    {
        for (auto& voice : rollVoice)
            voice.prime (params);
        rollPrimed = true;
    }
    rollTarget = params;
}
LaneConfig PsyEngine::lane (int engine) const { return lanes[engine].cfg; }
Step PsyEngine::step (int engine, int index) const { return lanes[engine].steps[index]; }

int PsyEngine::soundingVoices() const
{
    int n = 0;
    for (int e = 0; e < kEngineCount; ++e)
    {
        if (lanes[e].active.used && lanes[e].active.env > kSilence)
            ++n;
        if (lanes[e].tail.used && lanes[e].tail.env > kSilence)
            ++n;
    }
    return n;
}

int PsyEngine::triggerCount() const { return triggersN; }
Trigger PsyEngine::trigger (int index) const { return triggers[index]; }
bool PsyEngine::routeHeld (int channel, int note) const { return route[channel][note] != 0; }
int PsyEngine::heldCount() const { return nHeld; }

bool PsyEngine::voiceActive (int engine) const
{
    const auto& v = lanes[engine].active;
    return v.used && ! v.releasing;
}

bool PsyEngine::voiceReleasing (int engine) const
{
    return lanes[engine].active.used && lanes[engine].active.releasing;
}

void PsyEngine::clearVoices()
{
    for (int e = 0; e < kEngineCount; ++e)
    {
        lanes[e].active = {};
        lanes[e].tail = {};
        lanes[e].pendingOff = false;
        lanes[e].stolen = {};
        midiStackN[e] = 0;
    }
    for (auto& voice : rollVoice)
        voice.reset();
}

void PsyEngine::retargetArp (int engine)
{
    auto& arp = lanes[engine].arp;
    arp.dir = 1;
    if (nHeld <= 0)
    {
        arp.index = 0;
        return;
    }
    if (order == ArpOrder::Random)
    {
        arp.rng = arp.rng * 1664525u + 1013904223u;
        arp.index = static_cast<int> (arp.rng % static_cast<uint32_t> (nHeld));
    }
    else
        arp.index = 0;
}

void PsyEngine::noteOn (int sample, int channel, int note, int velocity)
{
    if (channel < 0 || channel > 15 || note < 0 || note > 127)
        return;

    const bool duck = duckSource == DuckSource::Midi && note == duckNote;
    route[channel][note] = static_cast<uint8_t> (duck ? 2 : 1);
    if (duck || velocity <= 0)
        return;

    const bool newGesture = downCount == 0;
    if (downCount < kMaxHeld)
        down[downCount++] = note;

    if (! latch || newGesture)
        nHeld = 0;

    if (! latch)
    {
        for (int i = 0; i < downCount && nHeld < kMaxHeld; ++i)
            held[nHeld++] = down[i];
    }
    else if (nHeld < kMaxHeld)
        held[nHeld++] = note;

    for (int e = 0; e < kEngineCount; ++e)
        retargetArp (e);

    for (int e = 0; e < kEngineCount; ++e)
    {
        if (lanes[e].cfg.source != SourceMode::Midi || ! lanes[e].cfg.enabled)
            continue;

        auto& nstack = midiStackN[e];
        const bool overlap = nstack > 0 && lanes[e].active.used && ! lanes[e].active.releasing;
        if (nstack < 8)
            midiStack[e][nstack++] = note;

        GlideInfo glide {};
        if (overlap)
        {
            glide.applies = true;
            glide.requestedMs = lanes[e].cfg.midiGlideMs;
            glide.effectiveMs = glide.requestedMs;
            glide.clamped = false;
        }
        startVoice (e, sample, note, ! overlap, glide, overlap ? lanes[e].cfg.midiGlideMs : 0.f);
    }
    (void) sample;
}

void PsyEngine::noteOff (int sample, int channel, int note)
{
    if (channel < 0 || channel > 15 || note < 0 || note > 127)
        return;

    const uint8_t routed = route[channel][note];
    route[channel][note] = 0;
    if (routed == 0)
        return;

    for (int i = 0; i < downCount; ++i)
    {
        if (down[i] != note)
            continue;
        for (int j = i + 1; j < downCount; ++j)
            down[j - 1] = down[j];
        --downCount;
        break;
    }

    if (! latch && routed == 1)
    {
        for (int i = 0; i < nHeld; ++i)
        {
            if (held[i] != note)
                continue;
            for (int j = i + 1; j < nHeld; ++j)
                held[j - 1] = held[j];
            --nHeld;
            break;
        }
    }

    if (routed != 1)
    {
        releaseOpenTies();
        return;
    }

    for (int e = 0; e < kEngineCount; ++e)
    {
        if (lanes[e].cfg.source != SourceMode::Midi)
            continue;

        auto& nstack = midiStackN[e];
        for (int i = 0; i < nstack; ++i)
        {
            if (midiStack[e][i] != note)
                continue;
            for (int j = i + 1; j < nstack; ++j)
                midiStack[e][j - 1] = midiStack[e][j];
            --nstack;
            break;
        }

        if (nstack > 0)
        {
            const int back = midiStack[e][nstack - 1];
            GlideInfo glide {};
            glide.applies = true;
            glide.requestedMs = lanes[e].cfg.midiGlideMs;
            glide.effectiveMs = glide.requestedMs;
            startVoice (e, sample, back, false, glide, lanes[e].cfg.midiGlideMs);
        }
        else if (lanes[e].active.used && ! lanes[e].active.releasing)
        {
            lanes[e].active.releasing = true;
            lanes[e].active.releaseLife = 0;
            lanes[e].active.releaseEnv = lanes[e].active.env;
        }
    }

    releaseOpenTies();
}

void PsyEngine::allNotesOff (int, bool hard)
{
    std::fill (&route[0][0], &route[0][0] + 16 * 128, static_cast<uint8_t> (0));
    nHeld = 0;
    downCount = 0;

    if (hard)
    {
        clearVoices();
        return;
    }

    for (int e = 0; e < kEngineCount; ++e)
    {
        lanes[e].pendingOff = false;
        midiStackN[e] = 0;
        Voice* slots[3] = { &lanes[e].active, &lanes[e].tail, &lanes[e].stolen };
        for (auto* v : slots)
        {
            if (! v->used || v->releasing)
                continue;
            v->releasing = true;
            v->releaseLife = 0;
            v->releaseEnv = v->env;
        }
    }
}

void PsyEngine::releaseEngine (int engine, int, bool seqOnly)
{
    auto& lane = lanes[engine];
    if (seqOnly && lane.cfg.source == SourceMode::Midi)
        return;

    lane.pendingOff = false;
    Voice* slots[3] = { &lane.active, &lane.tail, &lane.stolen };
    for (auto* v : slots)
    {
        if (! v->used || v->releasing)
            continue;
        v->releasing = true;
        v->releaseLife = 0;
        v->releaseEnv = v->env;
    }
}

void PsyEngine::releaseOpenTies()
{
    if (nHeld > 0)
        return;

    for (int e = 0; e < kEngineCount; ++e)
    {
        auto& lane = lanes[e];
        if (lane.cfg.source != SourceMode::Seq || lane.pendingOff)
            continue;
        if (! lane.active.used || lane.active.releasing)
            continue;
        lane.active.releasing = true;
        lane.active.releaseLife = 0;
        lane.active.releaseEnv = lane.active.env;
    }
}

int PsyEngine::pickNote (int engine)
{
    if (nHeld <= 0)
        return -1;

    int orderNotes[kMaxHeld];
    for (int i = 0; i < nHeld; ++i)
        orderNotes[i] = held[i];

    if (order == ArpOrder::Up || order == ArpOrder::Down || order == ArpOrder::UpDown)
        std::sort (orderNotes, orderNotes + nHeld);

    const int index = std::clamp (lanes[engine].arp.index, 0, nHeld - 1);
    if (order == ArpOrder::Down)
        return orderNotes[nHeld - 1 - index];
    return orderNotes[index];
}

void PsyEngine::advanceArp (int engine)
{
    if (nHeld <= 1)
        return;

    auto& arp = lanes[engine].arp;
    if (order == ArpOrder::Up || order == ArpOrder::Down || order == ArpOrder::AsPlayed)
        arp.index = (arp.index + 1) % nHeld;
    else if (order == ArpOrder::UpDown)
    {
        int next = arp.index + arp.dir;
        if (next >= nHeld || next < 0)
        {
            arp.dir = -arp.dir;
            next = arp.index + arp.dir;
        }
        arp.index = std::clamp (next, 0, nHeld - 1);
    }
    else
    {
        arp.rng = arp.rng * 1664525u + 1013904223u;
        arp.index = static_cast<int> (arp.rng % static_cast<uint32_t> (nHeld));
    }
}

void PsyEngine::startVoice (int engine, int sample, int note, bool retrigger, const GlideInfo& glide, float glideMs)
{
    auto& lane = lanes[engine];
    if (triggersN < kMaxTriggers)
    {
        Trigger t;
        t.sample = sample;
        t.engine = engine;
        t.note = note;
        t.retrigger = retrigger;
        t.glide = glide;
        triggers[triggersN++] = t;
    }

    if (! retrigger && lane.active.used && ! lane.active.releasing)
    {
        lane.active.gliding = glideMs > 0.f;
        lane.active.glideFrom = static_cast<double> (lane.active.note);
        lane.active.glideTo = static_cast<double> (note);
        lane.active.glidePos = 0;
        lane.active.glideSamples = std::max (1, static_cast<int> (std::lround (glideMs * 0.001 * renderRate)));
        lane.active.note = note;
        return;
    }

    // Two audible slots, plus the stolen tail for one short ramp.
    // The new voice still starts on this sample.
    const bool hadActive = lane.active.used;
    if (lane.tail.used)
    {
        lane.stolen = lane.tail;
        lane.stolen.tail = true;
        lane.stolen.releasing = true;
        lane.stolen.releaseLife = 0;
        lane.stolen.releaseEnv = lane.stolen.env;
        lane.stolen.tailLeft = samplesFor (kStealSec, renderRate);
        lane.stolen.fadeSpan = lane.stolen.tailLeft;
        if (engine == 0)
        {
            rollVoice[2] = rollVoice[1];
            rollVoice[2].beginFade (lane.stolen.tailLeft);
        }
    }
    if (hadActive)
    {
        lane.tail = lane.active;
        lane.tail.tail = true;
        lane.tail.releasing = true;
        lane.tail.releaseLife = 0;
        lane.tail.releaseEnv = lane.tail.env;
        lane.tail.tailLeft = samplesFor (kTailSec, renderRate);
        lane.tail.fadeSpan = lane.tail.tailLeft;
        if (engine == 0)
        {
            rollVoice[1] = rollVoice[0];
            rollVoice[1].beginFade (lane.tail.tailLeft);
        }
    }

    lane.active = {};
    lane.active.used = true;
    lane.active.note = note;
    lane.pendingOff = false;
    if (engine == 0)
    {
        RollVoice born;
        if (hadActive)
            born.copySmoothingFrom (rollVoice[1]);
        else
            born.prime (rollTarget);
        born.start (rollTarget.phaseReset, rollTarget.filterReset, hadActive ? &rollVoice[1] : nullptr);
        rollVoice[0] = born;
    }
    (void) sample;
}

void PsyEngine::fireStep (int engine, int64_t absoluteStep, int sample, double startPpq, double endPpq, const Block& block)
{
    auto& lane = lanes[engine];
    if (! lane.cfg.enabled || lane.cfg.source != SourceMode::Seq)
        return;

    const int length = std::clamp (lane.cfg.length, 1, kMaxSteps);
    const int patternIndex = static_cast<int> (absoluteStep % length);
    const Step& st = lane.steps[patternIndex];
    if (! st.on || nHeld <= 0)
        return;

    const int prevIndex = static_cast<int> ((absoluteStep - 1) % static_cast<int64_t> (length));
    const bool prevTie = absoluteStep > 0 && lane.steps[prevIndex].on && lane.steps[prevIndex].tie;

    int arpNote = -1;
    if (prevTie)
        arpNote = lane.lastArpNote;
    else
    {
        arpNote = pickNote (engine);
        if (arpNote < 0)
            return;
        lane.lastArpNote = arpNote;
        advanceArp (engine);
    }

    const int note = clampMidi (arpNote + st.offset + lane.cfg.transpose);
    const double stepLen = std::max (0.0, endPpq - startPpq);
    const double stepMs = stepLen * 60000.0 / block.bpm;
    const int nextIndex = (patternIndex + 1) % length;
    const bool tieHolds = st.tie && lane.steps[nextIndex].on && nHeld > 0;
    const float fraction = st.glideOverride ? st.glideFraction : lane.cfg.seqGlide;

    GlideInfo glide {};
    if (prevTie)
    {
        glide.applies = true;
        glide.requestedFraction = fraction;
        glide.requestedMs = static_cast<float> (fraction * stepMs);
        const float gateFraction = st.tie ? 1.f : std::clamp (st.gate, 0.05f, 1.f);
        const float gateMs = static_cast<float> (gateFraction * stepMs);
        glide.effectiveMs = std::min (glide.requestedMs, gateMs);
        glide.effectiveFraction = stepMs > 0.0 ? glide.effectiveMs / static_cast<float> (stepMs) : 0.f;
        glide.clamped = glide.requestedMs > gateMs + 0.01f;
    }

    startVoice (engine, sample, note, ! prevTie, glide, glide.effectiveMs);

    // A tie holds only when the next step is on. Otherwise the note ends
    // at this step's end, including a rest at the pattern wrap.
    if (! tieHolds)
    {
        const float gate = st.tie ? 1.f : std::clamp (st.gate, 0.05f, 1.f);
        lane.pendingOff = true;
        lane.pendingOffPpq = startPpq + stepLen * static_cast<double> (gate);
    }
}

void PsyEngine::renderSample (float& out)
{
    out = 0.f;
    const int attackN = samplesFor (kAttackSec, renderRate);
    const int decayN = samplesFor (kDecaySec, renderRate);
    const int releaseN = samplesFor (kReleaseSec, renderRate);

    for (int e = 0; e < kEngineCount; ++e)
    {
        auto& lane = lanes[e];
        if (! lane.cfg.enabled)
            continue;

        Voice* slots[3] = { &lane.active, &lane.tail, &lane.stolen };
        if (e == 0)
        {
            for (int s = 0; s < 3; ++s)
            {
                auto* v = slots[s];
                if (! v->used)
                    continue;
                auto& rv = rollVoice[s];
                if (v->releasing && ! rv.isReleasing())
                    rv.release();

                double note = static_cast<double> (v->note);
                if (v->gliding && v->glideSamples > 0)
                {
                    const double t = std::min (1.0, static_cast<double> (v->glidePos) / static_cast<double> (v->glideSamples));
                    note = v->glideFrom + (v->glideTo - v->glideFrom) * t;
                    ++v->glidePos;
                }

                float sample = rv.process (rollTarget, renderRate, static_cast<float> (midiFreq (note)));
                v->env = rv.ampLevel();
                if (rv.fadeFinished() || (v->releasing && v->env < kSilence))
                {
                    *v = {};
                    rv.reset();
                    continue;
                }
                out += sample;
            }
            continue;
        }

        for (auto* v : slots)
        {
            if (! v->used)
                continue;

            if (! v->releasing && v->life < attackN)
                v->env = static_cast<double> (v->life + 1) / static_cast<double> (attackN);
            else if (! v->releasing)
            {
                const double t = static_cast<double> (v->life - attackN) / static_cast<double> (decayN);
                v->env = std::pow (0.001, std::max (0.0, t));
            }
            else
            {
                v->env = v->releaseEnv * std::pow (0.001, static_cast<double> (v->releaseLife) / static_cast<double> (releaseN));
                ++v->releaseLife;
            }
            ++v->life;

            double note = static_cast<double> (v->note);
            if (v->gliding && v->glideSamples > 0)
            {
                const double t = std::min (1.0, static_cast<double> (v->glidePos) / static_cast<double> (v->glideSamples));
                note = v->glideFrom + (v->glideTo - v->glideFrom) * t;
                ++v->glidePos;
            }

            v->phase += midiFreq (note) / renderRate;
            v->phase -= std::floor (v->phase);
            float sample = static_cast<float> (std::sin (v->phase * 6.283185307179586) * v->env * 0.2);

            if (v->tail)
            {
                const int span = std::max (1, v->fadeSpan);
                const float g = std::clamp (static_cast<float> (v->tailLeft) / static_cast<float> (span), 0.f, 1.f);
                sample *= 0.5f * (1.f - std::cos (g * 3.14159265f));
                --v->tailLeft;
                if (v->tailLeft <= 0)
                {
                    *v = {};
                    continue;
                }
            }

            if (v->releasing && v->env < kSilence)
            {
                *v = {};
                continue;
            }

            out += sample;
        }
    }

    const float outputGain = std::pow (10.f, rollTarget.outputDb / 20.f);
    out *= outputGain;
}

void PsyEngine::process (const Block& block, const MidiIn* midi, int midiCount, float* monoOut)
{
    triggersN = 0;
    if (block.numSamples <= 0 || block.bpm <= 0.0 || block.sampleRate <= 0.0)
        return;

    renderRate = block.sampleRate;

    for (int e = 0; e < kEngineCount; ++e)
    {
        if (lanes[e].applied == lanes[e].cfg.source)
            continue;
        releaseEngine (e, 0, false);
        midiStackN[e] = 0;
        lanes[e].applied = lanes[e].cfg.source;
    }

    const bool stop = transportWasPlaying && ! block.playing;
    transportWasPlaying = block.playing;
    const double ppqEnd = block.ppqStart
                          + static_cast<double> (block.numSamples) * block.bpm / (block.sampleRate * 60.0);

    for (int s = 0; s < block.numSamples; ++s)
    {
        if (stop && s == 0)
        {
            nHeld = 0;
            for (int i = 0; i < downCount && nHeld < kMaxHeld; ++i)
                held[nHeld++] = down[i];
            for (int e = 0; e < kEngineCount; ++e)
                if (lanes[e].cfg.source == SourceMode::Seq)
                    releaseEngine (e, 0, true);
        }

        if (midi != nullptr)
        {
            for (int i = 0; i < midiCount; ++i)
            {
                if (midi[i].sample != s)
                    continue;
                if (midi[i].cc == 123 || midi[i].cc == 120)
                    allNotesOff (s, midi[i].cc == 120);
                else if (midi[i].cc < 0 && midi[i].velocity > 0)
                    noteOn (s, midi[i].channel, midi[i].note, midi[i].velocity);
                else if (midi[i].cc < 0)
                    noteOff (s, midi[i].channel, midi[i].note);
            }
        }

        if (block.playing)
        {
            for (int e = 0; e < kEngineCount; ++e)
            {
                auto& lane = lanes[e];
                if (lane.pendingOff)
                {
                    const int offSample = sampleAtPpq (lane.pendingOffPpq, block.ppqStart, block.bpm, block.sampleRate);
                    if (offSample <= s)
                    {
                        lane.pendingOff = false;
                        if (lane.cfg.source == SourceMode::Seq && lane.active.used && ! lane.active.releasing)
                        {
                            lane.active.releasing = true;
                            lane.active.releaseLife = 0;
                            lane.active.releaseEnv = lane.active.env;
                        }
                    }
                }

                const double swing = lane.cfg.swingOwn ? lane.cfg.swing : globalSwing;
                const double rate = std::max (lane.cfg.rateQuarters, 1.0 / 64.0);
                int64_t k = static_cast<int64_t> (std::floor (block.ppqStart / rate)) - 2;
                if (k < 0)
                    k = 0;

                for (; stepStartPpq (k, rate, swing) < ppqEnd; ++k)
                {
                    const double start = stepStartPpq (k, rate, swing);
                    if (start < block.ppqStart)
                        continue;
                    if (sampleAtPpq (start, block.ppqStart, block.bpm, block.sampleRate) != s)
                        continue;
                    fireStep (e, k, s, start, stepStartPpq (k + 1, rate, swing), block);
                }
            }
        }

        float sample = 0.f;
        renderSample (sample);
        if (monoOut != nullptr)
            monoOut[s] = sample;
    }
}

} // namespace psyb
