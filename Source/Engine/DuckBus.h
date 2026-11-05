#pragma once

#include "Dsp/DuckEnvelope.h"
#include "Dsp/KickDetector.h"

#include <atomic>
#include <cstdint>

namespace psyb
{

enum class DuckSource : int { Internal = 0, Midi = 1, Audio = 2 };


enum class DuckState : int { Active = 0, NoBus = 1, Silent = 2, Waiting = 3, Stopped = 4 };

/** Bus-level duck. Triggers are delayed by the shared engine latency so the
    dip lands on the kick after the host compensates for the reported latency. */
class DuckBus
{
public:
    void prepare (double sampleRate, int latencySamples) noexcept;
    void reset() noexcept;

    void setSource (DuckSource source) noexcept;
    void setDepthPercent (float percent) noexcept;
    void setReleaseMs (float ms) noexcept;
    void setHoldMs (float ms) noexcept;
    void setCurve (DuckCurve curve) noexcept;
    void setTarget (int engine, bool on) noexcept;
    void setBusConnected (bool connected) noexcept;

    DuckSource source() const noexcept { return source_; }
    bool target (int engine) const noexcept;
    int latencySamples() const noexcept { return latency; }

    /** Schedule a trigger that the listener hears `latency` samples later. */
    void schedule() noexcept;
    void hearMidi() noexcept { heardMidi = true; }
    bool feedAudio (float sample) noexcept;
    void setPlaying (bool playing) noexcept;

    float advance() noexcept;
    float appliedGain (int engine) const noexcept;

    DuckState state() const noexcept;
    int publishedState() const noexcept
    {
        return publishedState_.load (std::memory_order_relaxed);
    }
    float publishedGain() const noexcept;
    int triggerCount() const noexcept { return triggers; }

private:
    void retargetMix (int engine) noexcept;
    void refreshState() noexcept;
    void cancelPending() noexcept;

    DuckEnvelope envelope;
    KickDetector detector;
    DuckSource source_ = DuckSource::Internal;
    bool playing = false;
    bool connected = false;
    bool heardMidi = false;
    int latency = 0;
    int64_t pos = 0;
    int64_t pending[32] {};
    int pendingN = 0;
    int silentSamples = 0;
    int silentLimit = 96000;
    int triggers = 0;
    bool targets[3] = { true, true, false };
    float mix[3] = { 1.f, 1.f, 0.f };
    int mixLeft[3] = { 0, 0, 0 };
    float mixStep[3] = { 0.f, 0.f, 0.f };
    float applied[3] = { 1.f, 1.f, 1.f };
    DuckState current = DuckState::Stopped;

    std::atomic<int> publishedState_ { static_cast<int> (DuckState::Stopped) };
    std::atomic<float> publishedGain_ { 1.f };
};

} // namespace psyb
