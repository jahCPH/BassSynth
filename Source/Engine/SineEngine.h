#pragma once

#include "Engine/EngineModule.h"
#include "Engine/VoiceSlot.h"

namespace psyb
{

/** Placeholder FX voice. Same interface as the bass engines. */
class SineEngine : public EngineModule
{
public:
    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void setEnabled (bool on) override;
    void setParams (const RollParams&) override;
    void setAudible (bool) override;
    void setLevelDb (float) override;
    void setSubOwner (SubOwner) override;
    void setAccentAmount (float) override;
    void setDuckGain (float gain) override { duckGain = gain; }
    bool noteOn (int sample, int note, int velocity, bool retrigger, float glideMs, bool accented) override;
    void noteOff() override;
    void releaseAll() override;
    void allNotesOff (bool hard) override;
    void render (float* mono, int n) override;
    int latencySamples() const override;
    bool isSilent() const override;
    int algorithmVersion() const override;
    bool voiceActive() const override;
    bool voiceReleasing() const override;
    float voiceEnv() const override;
    int soundingCount() const override;

private:
    void releaseSlot (VoiceSlot& slot);
    void stealInto (VoiceSlot& dest, VoiceSlot& src, double seconds);

    double sampleRate = 48000.0;
    bool enabled = false;
    float duckGain = 1.f;
    VoiceSlot active {};
    VoiceSlot tail {};
    VoiceSlot stolen {};
};

} // namespace psyb
