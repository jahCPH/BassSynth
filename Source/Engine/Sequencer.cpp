#include "Engine/Sequencer.h"
#include "Engine/EngineFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
 #include <pmmintrin.h>
#elif defined (__aarch64__)
 // FPCR is set with an instruction below.
#endif

namespace psyb
{
namespace
{
/** Flush denormals for the audio callback. JUCE-free so the DSP lib and the
    headless tests get the same protection as the plugin's ScopedNoDenormals. */
struct FlushDenormals
{
#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
    unsigned old;
    FlushDenormals() noexcept : old (static_cast<unsigned> (_mm_getcsr()))
    {
        _mm_setcsr (old | 0x8040u);
    }
    ~FlushDenormals() noexcept { _mm_setcsr (old); }
#elif defined (__aarch64__)
    std::uint64_t old;
    FlushDenormals() noexcept
    {
        std::uint64_t fpcr = 0;
        asm volatile ("mrs %0, fpcr" : "=r"(fpcr));
        old = fpcr;
        asm volatile ("msr fpcr, %0" :: "r"(fpcr | (std::uint64_t (1) << 24)));
    }
    ~FlushDenormals() noexcept
    {
        asm volatile ("msr fpcr, %0" :: "r"(old));
    }
#else
    FlushDenormals() noexcept {}
#endif
};

} // namespace

PsyEngine::PsyEngine()
{
    auto made = createEngines();
    const int n = std::min (kEngineCount, static_cast<int> (made.size()));
    for (int i = 0; i < n; ++i)
        engines[static_cast<size_t> (i)] = std::move (made[static_cast<size_t> (i)]);
    for (int i = 0; i < kEngineCount; ++i)
        if (engines[static_cast<size_t> (i)] != nullptr)
            busLatency = std::max (busLatency, engines[static_cast<size_t> (i)]->latencySamples());
}

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
    duckChannel = 0;
    duckPassthru = false;
    duck.reset();
    order = ArpOrder::Up;
    latch = false;
    globalSwing = 0.0;
    nHeld = 0;
    downCount = 0;
    triggersN = 0;
    transportWasPlaying = false;
    renderRate = 48000.0;
    subOwner = SubOwner::Roll;
    busOutputDb = 0.f;
    for (int i = 0; i < kEngineCount; ++i)
    {
        algoStored[i] = 1;
        alignPos[i] = 0;
        for (int s = 0; s < kAlignCap; ++s)
            alignBuf[i][s] = 0.f;
        if (engines[static_cast<size_t> (i)] != nullptr)
        {
            engines[static_cast<size_t> (i)]->reset();
            engines[static_cast<size_t> (i)]->setAccentAmount (0.5f);
            engines[static_cast<size_t> (i)]->setSubOwner (subOwner);
        }
    }
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
            lanes[e].steps[s].on = (e <= 1) && ((oneBased % 4) != 1);
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

void PsyEngine::setDuckSource (DuckSource source)
{
    duckSource = source;
    duck.setSource (source);
}
void PsyEngine::setDuckNote (int note) { duckNote = clampMidi (note); }
void PsyEngine::setDuckDepth (float percent) { duck.setDepthPercent (percent); }
void PsyEngine::setDuckReleaseMs (float ms) { duck.setReleaseMs (ms); }
void PsyEngine::setDuckHoldMs (float ms) { duck.setHoldMs (ms); }
void PsyEngine::setDuckCurve (DuckCurve curve) { duck.setCurve (curve); }
void PsyEngine::setDuckChannel (int midiChannelOrAny) { duckChannel = std::clamp (midiChannelOrAny, 0, 16); }
void PsyEngine::setDuckPassthru (bool on) { duckPassthru = on; }
void PsyEngine::setDuckTarget (int engine, bool on) { duck.setTarget (engine, on); }
void PsyEngine::setGlobalSwing (double swing) { globalSwing = std::clamp (swing, 0.0, 0.75); }
void PsyEngine::setLane (int engine, const LaneConfig& lane)
{
    lanes[engine].cfg = lane;
    if (engine >= 0 && engine < kEngineCount && engines[static_cast<size_t> (engine)] != nullptr)
        engines[static_cast<size_t> (engine)]->setEnabled (lane.enabled);
}
void PsyEngine::setStep (int engine, int index, const Step& step) { lanes[engine].steps[index] = step; }

void PsyEngine::setRollParams (const RollParams& params)
{
    busOutputDb = params.outputDb;
    if (engines[0] != nullptr)
        engines[0]->setParams (params);
}

void PsyEngine::setSlideParams (const RollParams& params)
{
    if (engines[1] != nullptr)
        engines[1]->setParams (params);
}

void PsyEngine::setEngineAudible (int engine, bool on)
{
    if (engine < 0 || engine >= kEngineCount || engines[static_cast<size_t> (engine)] == nullptr)
        return;
    engines[static_cast<size_t> (engine)]->setAudible (on);
}

void PsyEngine::setEngineLevelDb (int engine, float db)
{
    if (engine < 0 || engine >= kEngineCount || engines[static_cast<size_t> (engine)] == nullptr)
        return;
    engines[static_cast<size_t> (engine)]->setLevelDb (db);
}

void PsyEngine::setSubOwner (SubOwner owner)
{
    subOwner = owner;
    for (int e = 0; e < kEngineCount; ++e)
        if (engines[static_cast<size_t> (e)] != nullptr)
            engines[static_cast<size_t> (e)]->setSubOwner (owner);
}

void PsyEngine::setSlideAccent (float amount01)
{
    for (int e = 0; e < kEngineCount; ++e)
        if (engines[static_cast<size_t> (e)] != nullptr)
            engines[static_cast<size_t> (e)]->setAccentAmount (amount01);
}

void PsyEngine::setStoredAlgorithmVersion (int engine, int version)
{
    if (engine >= 0 && engine < kEngineCount)
        algoStored[engine] = version;
}

int PsyEngine::storedAlgorithmVersion (int engine) const
{
    if (engine < 0 || engine >= kEngineCount)
        return 1;
    return algoStored[engine];
}

void PsyEngine::setLaneGlide (int engine, float seqFraction, float midiMs)
{
    if (engine < 0 || engine >= kEngineCount)
        return;
    lanes[engine].cfg.seqGlide = std::clamp (seqFraction, 0.f, 1.f);
    lanes[engine].cfg.midiGlideMs = std::max (0.f, midiMs);
}

LaneConfig PsyEngine::lane (int engine) const { return lanes[engine].cfg; }
Step PsyEngine::step (int engine, int index) const { return lanes[engine].steps[index]; }

int PsyEngine::soundingVoices() const
{
    int n = 0;
    for (int e = 0; e < kEngineCount; ++e)
        if (engines[static_cast<size_t> (e)] != nullptr)
            n += engines[static_cast<size_t> (e)]->soundingCount();
    return n;
}

int PsyEngine::triggerCount() const { return triggersN; }
Trigger PsyEngine::trigger (int index) const { return triggers[index]; }
bool PsyEngine::routeHeld (int channel, int note) const { return route[channel][note] != 0; }
int PsyEngine::heldCount() const { return nHeld; }

bool PsyEngine::voiceActive (int engine) const
{
    return engines[static_cast<size_t> (engine)] != nullptr && engines[static_cast<size_t> (engine)]->voiceActive();
}

bool PsyEngine::voiceReleasing (int engine) const
{
    return engines[static_cast<size_t> (engine)] != nullptr && engines[static_cast<size_t> (engine)]->voiceReleasing();
}

float PsyEngine::voiceEnv (int engine) const
{
    if (engines[static_cast<size_t> (engine)] == nullptr)
        return 0.f;
    return engines[static_cast<size_t> (engine)]->voiceEnv();
}

void PsyEngine::clearVoices()
{
    for (int e = 0; e < kEngineCount; ++e)
    {
        lanes[e].pendingOff = false;
        midiStackN[e] = 0;
        if (engines[static_cast<size_t> (e)] != nullptr)
            engines[static_cast<size_t> (e)]->allNotesOff (true);
    }
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

    const bool channelOk = duckChannel == 0 || channel + 1 == duckChannel;
    const bool duckHit = duckSource == DuckSource::Midi && note == duckNote && channelOk;
    if (duckHit)
    {
        duck.hearMidi();
        duck.schedule();
    }
    const bool consumed = duckHit && ! duckPassthru;
    route[channel][note] = static_cast<uint8_t> (consumed ? 2 : 1);
    if (consumed || velocity <= 0)
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
        const bool overlap = nstack > 0 && voiceActive (e);
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
        startVoice (e, sample, note, velocity, ! overlap, glide, overlap ? lanes[e].cfg.midiGlideMs : 0.f, false);
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
            startVoice (e, sample, back, 0, false, glide, lanes[e].cfg.midiGlideMs, false);
        }
        else if (voiceActive (e) && engines[static_cast<size_t> (e)] != nullptr)
            engines[static_cast<size_t> (e)]->noteOff();
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
        if (engines[static_cast<size_t> (e)] != nullptr)
            engines[static_cast<size_t> (e)]->releaseAll();
    }
}

void PsyEngine::releaseEngine (int engine, int, bool seqOnly)
{
    auto& lane = lanes[engine];
    if (seqOnly && lane.cfg.source == SourceMode::Midi)
        return;

    lane.pendingOff = false;
    if (engines[static_cast<size_t> (engine)] != nullptr)
        engines[static_cast<size_t> (engine)]->releaseAll();
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
        if (! voiceActive (e))
            continue;
        if (engines[static_cast<size_t> (e)] != nullptr)
            engines[static_cast<size_t> (e)]->noteOff();
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

void PsyEngine::startVoice (int engine, int sample, int note, int velocity, bool retrigger, const GlideInfo& glide, float glideMs, bool accented)
{
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

    if (engines[static_cast<size_t> (engine)] == nullptr)
        return;
    const bool legato = engines[static_cast<size_t> (engine)]->noteOn (sample, note, velocity, retrigger, glideMs, accented);
    if (! legato)
        lanes[engine].pendingOff = false;
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

    startVoice (engine, sample, note, st.velocity, ! prevTie, glide, glide.effectiveMs, st.accent);

    // A tie holds only when the next step is on. Otherwise the note ends
    // at this step's end, including a rest at the pattern wrap.
    if (! tieHolds)
    {
        const float gate = st.tie ? 1.f : std::clamp (st.gate, 0.05f, 1.f);
        lane.pendingOff = true;
        lane.pendingOffPpq = startPpq + stepLen * static_cast<double> (gate);
    }
}

float PsyEngine::takeAligned (int engine, float sample)
{
    const int delay = busLatency - (engines[static_cast<size_t> (engine)] != nullptr
                                         ? engines[static_cast<size_t> (engine)]->latencySamples()
                                         : 0);
    if (delay <= 0)
        return sample;
    const int pos = alignPos[engine];
    const float delayed = alignBuf[engine][pos];
    alignBuf[engine][pos] = sample;
    alignPos[engine] = pos + 1 < delay ? pos + 1 : 0;
    return delayed;
}

void PsyEngine::renderSample (float& out)
{
    out = 0.f;
    for (int e = 0; e < kEngineCount; ++e)
    {
        auto* engine = engines[static_cast<size_t> (e)].get();
        if (engine == nullptr)
            continue;

        // A zero-delay engine multiplies into the running sum, same fused
        // multiply-add as the pre-module mixer. Rounding into a private
        // buffer first moves the sum by one float when the gain is not 1.
        const int delay = busLatency - engine->latencySamples();
        if (delay <= 0)
        {
            if (! engine->isSilent())
                engine->render (&out, 1);
            continue;
        }

        float part = 0.f;
        if (! engine->isSilent())
            engine->render (&part, 1);
        out += takeAligned (e, part);
    }

    const float outputGain = std::pow (10.f, busOutputDb / 20.f);
    out *= outputGain;
}

void PsyEngine::process (const Block& block, const MidiIn* midi, int midiCount, float* monoOut,
                        const float* const* sidechain, int sidechainChannels, float* duckGainOut)
{
    triggersN = 0;
    if (block.numSamples <= 0 || block.bpm <= 0.0 || block.sampleRate <= 0.0)
        return;

    const FlushDenormals flushDenormals;
    renderRate = block.sampleRate;
    duck.prepare (block.sampleRate, busLatency);
    duck.setPlaying (block.playing);
    const bool sidechainLive = sidechain != nullptr && sidechainChannels > 0;
    duck.setBusConnected (sidechainLive);
    for (int e = 0; e < kEngineCount; ++e)
    {
        if (engines[static_cast<size_t> (e)] == nullptr)
            continue;
        engines[static_cast<size_t> (e)]->setEnabled (lanes[e].cfg.enabled);
        engines[static_cast<size_t> (e)]->prepare (block.sampleRate, block.numSamples);
    }

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

    int quarterAt[64] {};
    int quarterN = 0;
    if (block.playing && duckSource == DuckSource::Internal)
    {
        int64_t quarter = static_cast<int64_t> (std::floor (block.ppqStart));
        if (static_cast<double> (quarter) < block.ppqStart - 1.0e-9)
            ++quarter;
        for (; static_cast<double> (quarter) < ppqEnd - 1.0e-9 && quarterN < 64; ++quarter)
        {
            const int at = sampleAtPpq (static_cast<double> (quarter), block.ppqStart, block.bpm, block.sampleRate);
            if (at >= 0 && at < block.numSamples)
                quarterAt[quarterN++] = at;
        }
    }
    int quarterIndex = 0;

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
                        if (lane.cfg.source == SourceMode::Seq && voiceActive (e))
                            engines[static_cast<size_t> (e)]->noteOff();
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

        while (quarterIndex < quarterN && quarterAt[quarterIndex] < s)
            ++quarterIndex;
        if (quarterIndex < quarterN && quarterAt[quarterIndex] == s)
        {
            duck.schedule();
            ++quarterIndex;
        }

        if (sidechainLive && duckSource == DuckSource::Audio)
        {
            float sum = 0.f;
            int heard = 0;
            for (int c = 0; c < sidechainChannels; ++c)
            {
                if (sidechain[c] == nullptr)
                    continue;
                sum += sidechain[c][s];
                ++heard;
            }
            if (heard > 0)
                duck.feedAudio (sum / static_cast<float> (heard));
        }

        const float env = duck.advance();
        for (int e = 0; e < kEngineCount; ++e)
            if (engines[static_cast<size_t> (e)] != nullptr)
                engines[static_cast<size_t> (e)]->setDuckGain (duck.appliedGain (e));
        if (duckGainOut != nullptr)
            duckGainOut[s] = env;

        float sample = 0.f;
        renderSample (sample);
        if (monoOut != nullptr)
            monoOut[s] = sample;
    }
}

} // namespace psyb
