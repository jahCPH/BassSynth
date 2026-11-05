#include "Dsp/KickDetector.h"

#include <algorithm>
#include <cmath>

namespace psyb
{

void KickDetector::prepare (double sampleRate) noexcept
{
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    constexpr double cutoffHz = 85.0;
    const double omega = 2.0 * 3.14159265358979323846 * cutoffHz / rate;
    lpCoeff = static_cast<float> (1.0 - std::exp (-omega));
    holdoffSamples = std::max (1, static_cast<int> (std::lround (0.040 * rate)));
}

void KickDetector::reset() noexcept
{
    lpState = 0.f;
    envelope = 0.f;
    holdoffLeft = 0;
    armed = true;
}

bool KickDetector::processSample (float sample) noexcept
{
    constexpr float attack = 0.35f;
    constexpr float release = 0.002f;

    lpState += lpCoeff * (sample - lpState);
    const float rect = std::abs (lpState);
    const float coeff = rect > envelope ? attack : release;
    envelope += coeff * (rect - envelope);

    if (holdoffLeft > 0)
        --holdoffLeft;

    if (! armed)
    {
        if (holdoffLeft <= 0 && envelope < threshold * 0.5f)
            armed = true;
        return false;
    }

    if (envelope <= threshold || holdoffLeft > 0)
        return false;

    armed = false;
    holdoffLeft = holdoffSamples;
    return true;
}

} // namespace psyb
