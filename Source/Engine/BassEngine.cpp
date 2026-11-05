#include "Engine/BassEngine.h"
#include "Dsp/Pitch.h"

#include <algorithm>
#include <cmath>

namespace psyb
{
namespace
{
constexpr double kTailSec = 0.002;
constexpr double kStealSec = 0.0003;
constexpr double kSilence = 1.0e-3;

RollParams slideInitParams()
{
    RollParams params;
    params.cutoffHz = 1200.f;
    params.reso = 0.45f;
    params.envAmt = 0.50f;
    params.fatkMs = 0.5f;
    params.fdecMs = 250.f;
    params.fsus = 0.f;
    params.aatkMs = 0.5f;
    params.adecMs = 300.f;
    params.asus = 0.f;
    params.arelMs = 40.f;
    params.drive = 0.15f;
    params.sub = 0.70f;
    params.phaseReset = true;
    params.filterReset = false;
    return params;
}
} // namespace

BassEngine::BassEngine (BassRole next)
    : role (next)
{
    target = role == BassRole::Slide ? slideInitParams() : RollParams {};
    audible = role == BassRole::Roll;
    gainTarget = audible ? 1.f : 0.f;
    gainSm = gainTarget;
    owns = role == BassRole::Roll;
}

void BassEngine::prepare (double rate, int)
{
    sampleRate = rate;
    gainCoef = 1.f - std::exp (-1.f / (0.008f * static_cast<float> (rate)));
    for (auto& slot : voice)
        slot.prepareBlock (target, rate);
}

void BassEngine::reset()
{
    target = role == BassRole::Slide ? slideInitParams() : RollParams {};
    primed = false;
    gainPrimed = false;
    audible = role == BassRole::Roll;
    levelDb = 0.f;
    accentAmount = 0.5f;
    owner = SubOwner::Roll;
    owns = role == BassRole::Roll;
    gainTarget = audible ? 1.f : 0.f;
    gainSm = gainTarget;
    duckGain = 1.f;
    active = {};
    tail = {};
    stolen = {};
    for (auto& slot : voice)
        slot.reset();
}

void BassEngine::setEnabled (bool on) { enabled = on; }

void BassEngine::setParams (const RollParams& params)
{
    if (! primed)
    {
        for (auto& slot : voice)
            slot.prime (params);
        primed = true;
    }
    target = params;
}

void BassEngine::updateGainTarget()
{
    float next = 0.f;
    if (audible && levelDb > -60.f)
        next = std::pow (10.f, levelDb / 20.f);
    gainTarget = next;
    if (! gainPrimed)
    {
        gainSm = next;
        gainPrimed = true;
    }
}

void BassEngine::setAudible (bool on)
{
    audible = on;
    updateGainTarget();
}

void BassEngine::setLevelDb (float db)
{
    levelDb = db;
    updateGainTarget();
}

void BassEngine::setSubOwner (SubOwner next)
{
    owner = next;
    if (owner == SubOwner::Off)
        owns = false;
    else if (owner == SubOwner::Slide)
        owns = role == BassRole::Slide;
    else
        owns = role == BassRole::Roll;
}

void BassEngine::setAccentAmount (float amount01)
{
    accentAmount = std::clamp (amount01, 0.f, 1.f);
}

bool BassEngine::ownsSub() const { return owns; }

void BassEngine::releaseSlot (VoiceSlot& slot)
{
    if (! slot.used || slot.releasing)
        return;
    slot.releasing = true;
    slot.releaseLife = 0;
    slot.releaseEnv = slot.env;
}

bool BassEngine::noteOn (int, int note, int, bool retrigger, float glideMs, bool accented)
{
    if (! retrigger && active.used && ! active.releasing)
    {
        active.gliding = glideMs > 0.f;
        active.glideFrom = static_cast<double> (active.note);
        active.glideTo = static_cast<double> (note);
        active.glidePos = 0;
        active.glideSamples = std::max (1, static_cast<int> (std::lround (glideMs * 0.001 * sampleRate)));
        active.note = note;
        return true;
    }

    const bool hadActive = active.used;
    if (tail.used)
    {
        stolen = tail;
        stolen.tail = true;
        stolen.releasing = true;
        stolen.releaseLife = 0;
        stolen.releaseEnv = stolen.env;
        stolen.tailLeft = samplesFor (kStealSec, sampleRate);
        stolen.fadeSpan = stolen.tailLeft;
        voice[2] = voice[1];
        voice[2].beginFade (stolen.tailLeft);
    }
    if (hadActive)
    {
        tail = active;
        tail.tail = true;
        tail.releasing = true;
        tail.releaseLife = 0;
        tail.releaseEnv = tail.env;
        tail.tailLeft = samplesFor (kTailSec, sampleRate);
        tail.fadeSpan = tail.tailLeft;
        voice[1] = voice[0];
        voice[1].beginFade (tail.tailLeft);
    }

    active = {};
    active.used = true;
    active.note = note;

    RollVoice born;
    if (hadActive)
        born.copySmoothingFrom (voice[1]);
    else
    {
        RollParams primedParams = target;
        if (! ownsSub())
            primedParams.sub = 0.f;
        born.prime (primedParams);
    }
    born.start (target.phaseReset, target.filterReset, hadActive ? &voice[1] : nullptr);
    if (role == BassRole::Slide && accented)
    {
        const float amount = accentAmount;
        born.setAccent (std::pow (10.f, amount * 8.f / 20.f), amount * 0.60f);
    }
    voice[0] = born;
    return false;
}

void BassEngine::noteOff() { releaseSlot (active); }

void BassEngine::releaseAll()
{
    releaseSlot (active);
    releaseSlot (tail);
    releaseSlot (stolen);
}

void BassEngine::allNotesOff (bool hard)
{
    if (hard)
    {
        active = {};
        tail = {};
        stolen = {};
        for (auto& slot : voice)
            slot.reset();
        return;
    }
    releaseAll();
}

void BassEngine::render (float* mono, int n)
{
    if (mono == nullptr || n <= 0)
        return;

    VoiceSlot* slots[3] = { &active, &tail, &stolen };
    for (int i = 0; i < n; ++i)
    {
        gainSm += (gainTarget - gainSm) * gainCoef;
        if (gainTarget == 0.f && std::abs (gainSm) < 1.0e-6f)
            gainSm = 0.f;

        const bool live = active.used || tail.used || stolen.used;
        const bool muted = ! audible && gainSm == 0.f;
        if ((muted && ! live) || (! enabled && ! live))
            continue;

        RollParams params = target;
        if (! ownsSub())
            params.sub = 0.f;
        for (int s = 0; s < 3; ++s)
        {
            auto* v = slots[s];
            if (! v->used)
                continue;
            auto& rv = voice[s];
            if (v->releasing && ! rv.isReleasing())
                rv.release();

            double note = static_cast<double> (v->note);
            if (v->gliding && v->glideSamples > 0)
            {
                const double t = std::min (1.0, static_cast<double> (v->glidePos) / static_cast<double> (v->glideSamples));
                note = v->glideFrom + (v->glideTo - v->glideFrom) * t;
                ++v->glidePos;
            }

            float sample = rv.process (params, sampleRate, static_cast<float> (midiFreq (note)));
            v->env = rv.ampLevel();
            if (rv.fadeFinished() || (v->releasing && v->env < kSilence))
            {
                *v = {};
                rv.reset();
                continue;
            }
            const float applied = duckGain == 1.f ? gainSm : gainSm * duckGain;
            if (applied != 0.f)
                mono[i] += sample * applied;
        }
    }
}

int BassEngine::latencySamples() const { return kRollOversampleLatency; }

bool BassEngine::isSilent() const
{
    const bool live = active.used || tail.used || stolen.used;
    if (live)
        return false;
    if (std::abs (gainSm - gainTarget) > 0.f)
        return false;
    const bool muted = ! audible && gainSm == 0.f;
    return (muted && gainSm == 0.f) || ! enabled;
}

int BassEngine::algorithmVersion() const { return 1; }
bool BassEngine::voiceActive() const { return active.used && ! active.releasing; }
bool BassEngine::voiceReleasing() const { return active.used && active.releasing; }
float BassEngine::voiceEnv() const { return static_cast<float> (active.env); }

int BassEngine::soundingCount() const
{
    int n = 0;
    if (active.used && active.env > kSilence)
        ++n;
    if (tail.used && tail.env > kSilence)
        ++n;
    return n;
}

} // namespace psyb
