#pragma once

namespace psyb
{

/** Base-rate delay of the 2x drive stage. Both halfband filters
    contribute 32 samples, so the host latency is 64. */
constexpr int kRollOversampleLatency = 64;

struct RollParams
{
    float cutoffHz = 900.f;
    float reso = 0.20f;
    float envAmt = 0.60f;
    float fatkMs = 0.5f;
    float fdecMs = 60.f;
    float fsus = 0.f;
    float aatkMs = 0.5f;
    float adecMs = 70.f;
    float asus = 0.f;
    float arelMs = 12.f;
    float drive = 0.15f;
    float sub = 0.70f;
    bool phaseReset = true;
    bool filterReset = true;
    float outputDb = 0.f;
};

/** One Rolling slot: polyBLEP saw, sub, TPT ladder, amp/filter env, 2x drive.
    Slide and FX do not use this. */
class RollVoice
{
public:
    void reset();
    void prime (const RollParams& params);
    void copySmoothingFrom (const RollVoice& other);
    void start (bool phaseReset, bool filterReset, const RollVoice* inherit);
    void release();
    void beginFade (int samples);

    bool isReleasing() const { return releasing; }
    bool fadeFinished() const { return finished; }
    float ampLevel() const { return amp; }

    /** One base-rate sample, before the master output gain. */
    float process (const RollParams& target, double sampleRate, float freqHz);

private:
    void clearFilter();
    void clearFir();

    double phase = 0.0;
    double subPhase = 0.25;
    float amp = 0.f;
    float fenv = 0.f;
    int ampStage = 0;
    int fStage = 0;
    bool releasing = false;
    bool alive = false;
    bool finished = false;
    int fadeLeft = 0;
    int fadeSpan = 0;

    float logCut = 6.8f;
    float smReso = 0.2f;
    float smDrive = 0.15f;
    float smEnv = 0.6f;
    float smSub = 0.7f;

    float z[4] {};
    float dcX = 0.f;
    float dcY = 0.f;
    float up[129] {};
    float down[129] {};
    int upPos = 0;
    int downPos = 0;
};

} // namespace psyb
