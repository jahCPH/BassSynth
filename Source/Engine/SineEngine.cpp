#include "Engine/SineEngine.h"
#include "Dsp/Pitch.h"

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
} // namespace

void SineEngine::prepare (double rate, int) { sampleRate = rate; }

void SineEngine::reset()
{
    enabled = false;
    active = {};
    tail = {};
    stolen = {};
}

void SineEngine::setEnabled (bool on) { enabled = on; }
void SineEngine::setParams (const RollParams&) {}
void SineEngine::setAudible (bool) {}
void SineEngine::setLevelDb (float) {}
void SineEngine::setSubOwner (SubOwner) {}
void SineEngine::setAccentAmount (float) {}

void SineEngine::releaseSlot (VoiceSlot& slot)
{
    if (! slot.used || slot.releasing)
        return;
    slot.releasing = true;
    slot.releaseLife = 0;
    slot.releaseEnv = slot.env;
}

void SineEngine::stealInto (VoiceSlot& dest, VoiceSlot& src, double seconds)
{
    dest = src;
    dest.tail = true;
    dest.releasing = true;
    dest.releaseLife = 0;
    dest.releaseEnv = dest.env;
    dest.tailLeft = samplesFor (seconds, sampleRate);
    dest.fadeSpan = dest.tailLeft;
}

bool SineEngine::noteOn (int, int note, int, bool retrigger, float glideMs, bool)
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
        stealInto (stolen, tail, kStealSec);
    if (hadActive)
        stealInto (tail, active, kTailSec);

    active = {};
    active.used = true;
    active.note = note;
    return false;
}

void SineEngine::noteOff() { releaseSlot (active); }

void SineEngine::releaseAll()
{
    releaseSlot (active);
    releaseSlot (tail);
    releaseSlot (stolen);
}

void SineEngine::allNotesOff (bool hard)
{
    if (hard)
    {
        active = {};
        tail = {};
        stolen = {};
        return;
    }
    releaseAll();
}

void SineEngine::render (float* mono, int n)
{
    if (mono == nullptr || n <= 0 || ! enabled)
        return;

    const int attackN = samplesFor (kAttackSec, sampleRate);
    const int decayN = samplesFor (kDecaySec, sampleRate);
    const int releaseN = samplesFor (kReleaseSec, sampleRate);
    VoiceSlot* slots[3] = { &active, &tail, &stolen };

    for (int i = 0; i < n; ++i)
    {
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

            v->phase += midiFreq (note) / sampleRate;
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

            mono[i] += sample;
        }
    }
}

int SineEngine::latencySamples() const { return 0; }

bool SineEngine::isSilent() const
{
    if (! enabled)
        return true;
    return ! active.used && ! tail.used && ! stolen.used;
}

int SineEngine::algorithmVersion() const { return 1; }
bool SineEngine::voiceActive() const { return active.used && ! active.releasing; }
bool SineEngine::voiceReleasing() const { return active.used && active.releasing; }
float SineEngine::voiceEnv() const { return static_cast<float> (active.env); }

int SineEngine::soundingCount() const
{
    int n = 0;
    if (active.used && active.env > kSilence)
        ++n;
    if (tail.used && tail.env > kSilence)
        ++n;
    return n;
}

} // namespace psyb
