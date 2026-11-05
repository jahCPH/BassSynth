#pragma once

namespace psyb
{

/** Onset detector for the AUDIO duck source.
    The low-pass and envelope are the Vizuator TransientDetector (sub-100 Hz),
    copied and renamed. An onset is the rising crossing only, then a hold-off
    so one kick cannot fire twice. */
class KickDetector
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    bool processSample (float sample) noexcept;

private:
    float lpCoeff = 0.f;
    float lpState = 0.f;
    float envelope = 0.f;
    float threshold = 0.08f;
    int holdoffSamples = 1;
    int holdoffLeft = 0;
    bool armed = true;
};

} // namespace psyb
