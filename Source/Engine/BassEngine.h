#pragma once

#include "Engine/EngineModule.h"
#include "Engine/VoiceSlot.h"

namespace psyb
{

enum class BassRole { Roll, Slide };

/** Rolling and Sliding are two instances of this module. */
class BassEngine : public EngineModule
{
public:
    explicit BassEngine (BassRole role);

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void setEnabled (bool on) override;
    void setParams (const RollParams& params) override;
    void setAudible (bool on) override;
    void setLevelDb (float db) override;
    void setSubOwner (SubOwner owner) override;
    void setAccentAmount (float amount01) override;
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
    void updateGainTarget();
    bool ownsSub() const;
    void releaseSlot (VoiceSlot& slot);

    BassRole role = BassRole::Roll;
    double sampleRate = 48000.0;
    bool enabled = true;
    bool audible = true;
    bool owns = true;
    bool primed = false;
    bool gainPrimed = false;
    float levelDb = 0.f;
    float accentAmount = 0.5f;
    float gainTarget = 1.f;
    float gainSm = 1.f;
    float gainCoef = 0.f;
    float duckGain = 1.f;
    SubOwner owner = SubOwner::Roll;
    RollParams target {};
    RollVoice voice[3] {};
    VoiceSlot active {};
    VoiceSlot tail {};
    VoiceSlot stolen {};
};

} // namespace psyb
