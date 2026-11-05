#include "Dsp/DuckEnvelope.h"

#include <algorithm>
#include <cmath>

namespace psyb
{

void DuckEnvelope::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    attackN = std::max (1, static_cast<int> (std::lround (0.001 * rate)));
    holdN = samplesForMs (holdMs);
    releaseN = std::max (1, samplesForMs (releaseMs));
}

int DuckEnvelope::samplesForMs (float ms) const noexcept
{
    return std::max (0, static_cast<int> (std::lround (static_cast<double> (ms) * 0.001 * rate)));
}

void DuckEnvelope::reset() noexcept
{
    trough_ = 1.f;
    gain_ = 1.f;
    from = 1.f;
    left = 0;
    stage = Stage::Idle;
    curve = DuckCurve::Exp;
    releaseMs = 120.f;
    holdMs = 0.f;
    attackN = std::max (1, static_cast<int> (std::lround (0.001 * rate)));
    holdN = 0;
    releaseN = std::max (1, samplesForMs (releaseMs));
}

void DuckEnvelope::setDepthPercent (float percent) noexcept
{
    const float next = 1.f - std::clamp (percent, 0.f, 100.f) * 0.01f;
    if (next == trough_)
        return;
    trough_ = next;
    if (stage == Stage::Attack || stage == Stage::Hold)
        beginAttack();
}

void DuckEnvelope::setReleaseMs (float ms) noexcept
{
    const float clamped = std::clamp (ms, 10.f, 500.f);
    if (clamped == releaseMs)
        return;
    releaseMs = clamped;
    releaseN = std::max (1, samplesForMs (releaseMs));
    if (stage == Stage::Release)
    {
        from = gain_;
        left = releaseN;
    }
}

void DuckEnvelope::setHoldMs (float ms) noexcept
{
    holdMs = std::clamp (ms, 0.f, 50.f);
    holdN = samplesForMs (holdMs);
}

void DuckEnvelope::setCurve (DuckCurve next) noexcept
{
    if (next == curve)
        return;
    curve = next;
    if (stage == Stage::Release)
    {
        from = gain_;
        left = releaseN;
    }
}

void DuckEnvelope::beginAttack() noexcept
{
    if (stage == Stage::Idle && trough_ >= 1.f && gain_ >= 1.f)
        return;
    from = gain_;
    left = attackN;
    stage = Stage::Attack;
}

void DuckEnvelope::beginRelease() noexcept
{
    if (gain_ >= 1.f)
    {
        gain_ = 1.f;
        stage = Stage::Idle;
        left = 0;
        return;
    }
    from = gain_;
    left = std::max (1, releaseN);
    stage = Stage::Release;
}

void DuckEnvelope::trigger() noexcept
{
    beginAttack();
}

void DuckEnvelope::releaseToUnity() noexcept
{
    beginRelease();
}

float DuckEnvelope::advance() noexcept
{
    if (stage == Stage::Idle)
    {
        gain_ = 1.f;
        return gain_;
    }

    if (stage == Stage::Attack)
    {
        const float p = 1.f - static_cast<float> (left - 1) / static_cast<float> (attackN);
        gain_ = from + (trough_ - from) * p;
        if (--left <= 0)
        {
            gain_ = trough_;
            if (holdN <= 0)
                beginRelease();
            else
            {
                stage = Stage::Hold;
                left = holdN;
            }
        }
        return gain_;
    }

    if (stage == Stage::Hold)
    {
        gain_ = trough_;
        if (--left <= 0)
            beginRelease();
        return gain_;
    }

    const float p = 1.f - static_cast<float> (left - 1) / static_cast<float> (std::max (1, releaseN));
    if (curve == DuckCurve::Exp)
        gain_ = 1.f - (1.f - from) * std::pow (0.001f, p);
    else
        gain_ = from + (1.f - from) * p;

    if (--left <= 0)
    {
        gain_ = 1.f;
        stage = Stage::Idle;
    }
    return gain_;
}

} // namespace psyb
