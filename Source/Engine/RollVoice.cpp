#include "Engine/RollVoice.h"

#include <algorithm>
#include <cmath>

namespace psyb
{
namespace
{
constexpr int kFirN = 129;
constexpr float kHalf[kFirN] = {
#include "HalfbandCoeffs.inc"
};

constexpr int kAttack = 0;
constexpr int kDecay = 1;
constexpr int kSustain = 2;
constexpr int kRelease = 3;

float coef60 (float ms, double sr)
{
    const float n = std::max (1.f, static_cast<float> (ms * 0.001 * sr));
    return std::pow (0.001f, 1.f / n);
}

float polyblep (float t, float dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if (t > 1.f - dt)
    {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}

float fir (float* z, int& pos, float x)
{
    z[pos] = x;
    float y = 0.f;
    int j = pos;
    for (int i = 0; i < kFirN; ++i)
    {
        y += kHalf[i] * z[j];
        if (--j < 0)
            j = kFirN - 1;
    }
    if (++pos >= kFirN)
        pos = 0;
    return y;
}

float asymmetric (float x, float drive)
{
    // Gain 4 keeps a 2x drive's folded energy under the R4 floor.
    // Drive 0 stays exactly linear.
    const float amount = std::clamp (drive, 0.f, 1.f);
    const float gain = 1.f + amount * 4.f;
    const float shaped = x >= 0.f ? std::tanh (x * gain) : std::tanh (x * gain * 0.6f);
    return x + amount * (shaped - x);
}
} // namespace

void RollVoice::reset()
{
    *this = {};
    subPhase = 0.25;
    logCut = std::log (900.f);
}

void RollVoice::clearFilter()
{
    z[0] = z[1] = z[2] = z[3] = 0.f;
    dcX = dcY = 0.f;
}

void RollVoice::clearFir()
{
    std::fill (up, up + kFirN, 0.f);
    std::fill (down, down + kFirN, 0.f);
    upPos = downPos = 0;
}

void RollVoice::prime (const RollParams& params)
{
    logCut = std::log (std::clamp (params.cutoffHz, 20.f, 20000.f));
    smReso = std::clamp (params.reso, 0.f, 1.f);
    smDrive = std::clamp (params.drive, 0.f, 1.f);
    smEnv = std::clamp (params.envAmt, -1.f, 1.f);
    smSub = std::clamp (params.sub, 0.f, 1.f);
}

void RollVoice::copySmoothingFrom (const RollVoice& other)
{
    logCut = other.logCut;
    smReso = other.smReso;
    smDrive = other.smDrive;
    smEnv = other.smEnv;
    smSub = other.smSub;
}

void RollVoice::prepareBlock (const RollParams& target, double sampleRate)
{
    const float sr = static_cast<float> (sampleRate);
    cSmooth = 1.f - std::exp (-1.f / (0.010f * sr));
    cAStep = 1.f / std::max (1.f, target.aatkMs * 0.001f * sr);
    cACoef = coef60 (target.adecMs, sampleRate);
    cRCoef = coef60 (target.arelMs, sampleRate);
    cFStep = 1.f / std::max (1.f, target.fatkMs * 0.001f * sr);
    cFCoef = coef60 (target.fdecMs, sampleRate);
    cAsus = std::clamp (target.asus, 0.f, 1.f);
    cFsus = std::clamp (target.fsus, 0.f, 1.f);
    cDcR = std::exp (-2.f * 3.14159265f * 20.f / (sr * 2.f));
    coefReady = true;
}

void RollVoice::setAccent (float gain, float envAdd)
{
    accentGain = gain;
    accentEnv = envAdd;
}

void RollVoice::start (bool phaseReset, bool filterReset, const RollVoice* inherit)
{
    if (inherit != nullptr && ! phaseReset)
    {
        phase = inherit->phase;
        subPhase = inherit->subPhase;
    }
    else
    {
        phase = 0.0;
        subPhase = 0.25;
    }

    if (inherit != nullptr && ! filterReset)
    {
        for (int i = 0; i < 4; ++i)
            z[i] = inherit->z[i];
    }
    else
        clearFilter();

    // The FIR is empty, so a copied DC blocker would emit a -dcX step.
    dcX = 0.f;
    dcY = 0.f;

    clearFir();
    amp = 0.f;
    fenv = 0.f;
    ampStage = kAttack;
    fStage = kAttack;
    releasing = false;
    finished = false;
    fadeLeft = 0;
    fadeSpan = 0;
    alive = true;
}

void RollVoice::release()
{
    if (releasing)
        return;
    releasing = true;
    ampStage = kRelease;
}

void RollVoice::beginFade (int samples)
{
    fadeSpan = std::max (1, samples);
    fadeLeft = fadeSpan;
}

float RollVoice::process (const RollParams& target, double sampleRate, float freqHz)
{
    if (! alive || finished || sampleRate <= 0.0)
        return 0.f;

    const float sr = static_cast<float> (sampleRate);
    const float smooth = coefReady ? cSmooth : 1.f - std::exp (-1.f / (0.010f * sr));
    const float targetCut = std::clamp (target.cutoffHz, 20.f, 20000.f);
    logCut += (std::log (targetCut) - logCut) * smooth;
    smReso += (std::clamp (target.reso, 0.f, 1.f) - smReso) * smooth;
    smDrive += (std::clamp (target.drive, 0.f, 1.f) - smDrive) * smooth;
    smEnv += (std::clamp (target.envAmt, -1.f, 1.f) - smEnv) * smooth;
    smSub += (std::clamp (target.sub, 0.f, 1.f) - smSub) * smooth;

    const float aStep = coefReady ? cAStep : 1.f / std::max (1.f, target.aatkMs * 0.001f * sr);
    const float aCoef = coefReady ? cACoef : coef60 (target.adecMs, sampleRate);
    const float rCoef = coefReady ? cRCoef : coef60 (target.arelMs, sampleRate);
    const float fStep = coefReady ? cFStep : 1.f / std::max (1.f, target.fatkMs * 0.001f * sr);
    const float fCoef = coefReady ? cFCoef : coef60 (target.fdecMs, sampleRate);
    const float asus = coefReady ? cAsus : std::clamp (target.asus, 0.f, 1.f);
    const float fsus = coefReady ? cFsus : std::clamp (target.fsus, 0.f, 1.f);

    if (ampStage == kAttack)
    {
        amp += aStep;
        if (amp >= 1.f)
        {
            amp = 1.f;
            ampStage = kDecay;
        }
    }
    else if (ampStage == kDecay)
    {
        amp = asus + (amp - asus) * aCoef;
        if (std::abs (amp - asus) < 0.0005f)
        {
            amp = asus;
            ampStage = kSustain;
        }
    }
    else if (ampStage == kRelease)
        amp *= rCoef;

    if (fStage == kAttack)
    {
        fenv += fStep;
        if (fenv >= 1.f)
        {
            fenv = 1.f;
            fStage = kDecay;
        }
    }
    else if (fStage == kDecay || fStage == kRelease)
        fenv = fsus + (fenv - fsus) * fCoef;

    if (amp < 1.0e-5f && ampStage == kRelease)
    {
        finished = true;
        amp = 0.f;
        return 0.f;
    }

    const float dt = std::clamp (freqHz / sr, 1.0e-5f, 0.45f);
    const float saw = (2.f * static_cast<float> (phase) - 1.f) - polyblep (static_cast<float> (phase), dt);
    phase += static_cast<double> (dt);
    phase -= std::floor (phase);

    const float envMod = std::clamp (smEnv + accentEnv, -1.5f, 1.5f);
    const float fc = std::clamp (std::exp (logCut) * std::pow (2.f, envMod * fenv * 4.f), 20.f, sr * 0.42f);
    const float g = std::tan (3.14159265f * fc / sr);
    const float G = g / (1.f + g);
    const float G2 = G * G;
    const float G3 = G2 * G;
    const float G4 = G2 * G2;
    // Stay under the ZDF ladder's k=4 limit. 3.2 still self-oscillates.
    const float k = std::clamp (smReso, 0.f, 1.f) * 3.2f;
    const float S = G3 * z[0] + G2 * z[1] + G * z[2] + z[3];
    const float denom = 1.f + k * G4;
    // tanh on the feedback keeps self-oscillation bounded without clipping the output.
    float u = (saw * 0.22f - k * std::tanh (S)) / denom;
    for (int i = 0; i < 4; ++i)
    {
        const float v = G * (u - z[i]);
        const float y = v + z[i];
        z[i] = y + v;
        u = y;
    }
    if (! std::isfinite (u))
    {
        clearFilter();
        u = 0.f;
    }

    const float subDt = 0.5f * dt;
    const float sub = smSub * 0.35f * std::sin (static_cast<float> (subPhase * 6.283185307179586));
    subPhase += static_cast<double> (subDt);
    subPhase -= std::floor (subPhase);

    const float shaped = (u + sub) * amp;
    const float dcR = coefReady ? cDcR : std::exp (-2.f * 3.14159265f * 20.f / (sr * 2.f));
    auto driveSample = [&] (float x)
    {
        const float clip = asymmetric (x, smDrive);
        const float y = clip - dcX + dcR * dcY;
        dcX = clip;
        dcY = y;
        return y;
    };
    const float up0 = driveSample (2.f * fir (up, upPos, shaped));
    const float up1 = driveSample (2.f * fir (up, upPos, 0.f));
    float out = fir (down, downPos, up0);
    fir (down, downPos, up1);

    if (fadeSpan > 0)
    {
        const float g = std::clamp (static_cast<float> (fadeLeft) / static_cast<float> (fadeSpan), 0.f, 1.f);
        out *= 0.5f * (1.f - std::cos (g * 3.14159265f));
        if (--fadeLeft <= 0)
        {
            finished = true;
            return 0.f;
        }
    }

    return out * accentGain;
}

} // namespace psyb
