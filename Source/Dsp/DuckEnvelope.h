#pragma once

namespace psyb
{

enum class DuckCurve : int { Lin = 0, Exp = 1 };

/** One duck gain. Attack is 1 ms, then hold, then a LIN or EXP release.
    A new trigger continues from the current gain. */
class DuckEnvelope
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setDepthPercent (float percent) noexcept;
    void setReleaseMs (float ms) noexcept;
    void setHoldMs (float ms) noexcept;
    void setCurve (DuckCurve curve) noexcept;
    void trigger() noexcept;
    void releaseToUnity() noexcept;
    float advance() noexcept;

    float gain() const noexcept { return gain_; }
    float trough() const noexcept { return trough_; }
    int attackSamples() const noexcept { return attackN; }
    int releaseSamples() const noexcept { return releaseN; }
    int holdSamples() const noexcept { return holdN; }

private:
    void beginAttack() noexcept;
    void beginRelease() noexcept;
    int samplesForMs (float ms) const noexcept;

    double rate = 48000.0;
    DuckCurve curve = DuckCurve::Exp;
    float releaseMs = 120.f;
    float holdMs = 0.f;
    float trough_ = 1.f;
    float gain_ = 1.f;
    float from = 1.f;
    int attackN = 48;
    int holdN = 0;
    int releaseN = 5760;
    int left = 0;
    enum class Stage { Idle, Attack, Hold, Release };
    Stage stage = Stage::Idle;
};

} // namespace psyb
