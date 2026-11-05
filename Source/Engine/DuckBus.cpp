#include "Engine/DuckBus.h"

#include <algorithm>
#include <cmath>

namespace psyb
{

void DuckBus::prepare (double sampleRate, int latencySamples) noexcept
{
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    latency = std::max (0, latencySamples);
    envelope.prepare (rate);
    detector.prepare (rate);
    silentLimit = std::max (1, static_cast<int> (std::lround (2.0 * rate)));
}

void DuckBus::reset() noexcept
{
    envelope.reset();
    detector.reset();
    source_ = DuckSource::Internal;
    playing = false;
    connected = false;
    heardMidi = false;
    latency = 0;
    pos = 0;
    pendingN = 0;
    silentSamples = 0;
    triggers = 0;
    targets[0] = targets[1] = true;
    targets[2] = false;
    mix[0] = mix[1] = 1.f;
    mix[2] = 0.f;
    mixLeft[0] = mixLeft[1] = mixLeft[2] = 0;
    applied[0] = applied[1] = applied[2] = 1.f;
    current = DuckState::Stopped;
    publishedState_.store (static_cast<int> (current), std::memory_order_relaxed);
    publishedGain_.store (1.f, std::memory_order_relaxed);
}

void DuckBus::setSource (DuckSource source) noexcept
{
    if (source == source_)
        return;
    source_ = source;
    cancelPending();
    heardMidi = false;
    silentSamples = 0;
    detector.reset();
    envelope.releaseToUnity();
}

void DuckBus::setDepthPercent (float percent) noexcept { envelope.setDepthPercent (percent); }
void DuckBus::setReleaseMs (float ms) noexcept { envelope.setReleaseMs (ms); }
void DuckBus::setHoldMs (float ms) noexcept { envelope.setHoldMs (ms); }
void DuckBus::setCurve (DuckCurve curve) noexcept { envelope.setCurve (curve); }

void DuckBus::setTarget (int engine, bool on) noexcept
{
    if (engine < 0 || engine >= 3 || targets[engine] == on)
        return;
    targets[engine] = on;
    if (envelope.gain() >= 1.f)
    {
        mix[engine] = on ? 1.f : 0.f;
        mixLeft[engine] = 0;
        return;
    }
    const float dest = on ? 1.f : 0.f;
    const int n = std::max (1, envelope.attackSamples());
    mixStep[engine] = (dest - mix[engine]) / static_cast<float> (n);
    mixLeft[engine] = n;
}

void DuckBus::setBusConnected (bool on) noexcept
{
    if (connected == on)
        return;
    connected = on;
    if (! on && source_ == DuckSource::Audio)
    {
        cancelPending();
        envelope.releaseToUnity();
    }
}

bool DuckBus::target (int engine) const noexcept
{
    return engine >= 0 && engine < 3 && targets[engine];
}

void DuckBus::schedule() noexcept
{
    if (source_ == DuckSource::Internal && ! playing)
        return;
    if (source_ == DuckSource::Audio && ! connected)
        return;
    const int64_t due = pos + static_cast<int64_t> (std::max (0, latency));
    for (int i = 0; i < pendingN; ++i)
        if (pending[i] == due)
            return;
    if (pendingN >= 32)
        return;
    pending[pendingN++] = due;
}

bool DuckBus::feedAudio (float sample) noexcept
{
    if (source_ != DuckSource::Audio || ! connected)
        return false;
    if (detector.processSample (sample))
    {
        silentSamples = 0;
        schedule();
        return true;
    }
    if (silentSamples < silentLimit)
        ++silentSamples;
    return false;
}

void DuckBus::setPlaying (bool on) noexcept
{
    if (playing == on)
        return;
    playing = on;
    if (! on && source_ == DuckSource::Internal)
    {
        cancelPending();
        envelope.releaseToUnity();
    }
}

float DuckBus::advance() noexcept
{
    for (int i = 0; i < pendingN;)
    {
        if (pending[i] != pos)
        {
            ++i;
            continue;
        }
        envelope.trigger();
        ++triggers;
        pending[i] = pending[pendingN - 1];
        --pendingN;
    }

    const float env = envelope.advance();
    for (int e = 0; e < 3; ++e)
    {
        if (mixLeft[e] > 0)
        {
            mix[e] += mixStep[e];
            if (--mixLeft[e] <= 0)
                mix[e] = targets[e] ? 1.f : 0.f;
        }
        float g = 1.f;
        if (mix[e] == 1.f)
            g = env;
        else if (mix[e] != 0.f && env != 1.f)
            g = 1.f + (env - 1.f) * mix[e];
        applied[e] = g;
    }

    ++pos;
    refreshState();
    publishedGain_.store (env, std::memory_order_relaxed);
    publishedState_.store (static_cast<int> (current), std::memory_order_relaxed);
    return env;
}

float DuckBus::appliedGain (int engine) const noexcept
{
    if (engine < 0 || engine >= 3)
        return 1.f;
    return applied[engine];
}

DuckState DuckBus::state() const noexcept { return current; }

float DuckBus::publishedGain() const noexcept
{
    return publishedGain_.load (std::memory_order_relaxed);
}

void DuckBus::refreshState() noexcept
{
    if (source_ == DuckSource::Audio && ! connected)
        current = DuckState::NoBus;
    else if (source_ == DuckSource::Internal && ! playing)
        current = DuckState::Stopped;
    else if (source_ == DuckSource::Midi && ! heardMidi)
        current = DuckState::Waiting;
    else if (source_ == DuckSource::Audio && silentSamples >= silentLimit)
        current = DuckState::Silent;
    else
        current = DuckState::Active;
}

void DuckBus::cancelPending() noexcept { pendingN = 0; }

} // namespace psyb
