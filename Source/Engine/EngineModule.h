#pragma once

#include "Dsp/RollVoice.h"

namespace psyb
{

enum class SubOwner : int { Roll = 0, Slide = 1, Off = 2 };

/** One synth engine. The sequencer talks to every engine through this, and
    never branches on which slot it is. */
class EngineModule
{
public:
    virtual ~EngineModule() = default;

    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void setEnabled (bool on) = 0;
    virtual void setParams (const RollParams& params) = 0;
    virtual void setAudible (bool on) = 0;
    virtual void setLevelDb (float db) = 0;
    virtual void setSubOwner (SubOwner owner) = 0;
    virtual void setAccentAmount (float amount01) = 0;

    /** Returns true when the previous note keeps sounding and only the pitch moves. */
    virtual bool noteOn (int sample, int note, int velocity, bool retrigger, float glideMs, bool accented) = 0;
    virtual void noteOff() = 0;
    virtual void releaseAll() = 0;
    virtual void allNotesOff (bool hard) = 0;

    /** Adds this engine into mono. Events are already applied. */
    virtual void render (float* mono, int n) = 0;

    virtual int latencySamples() const = 0;
    virtual bool isSilent() const = 0;
    virtual int algorithmVersion() const = 0;

    virtual bool voiceActive() const = 0;
    virtual bool voiceReleasing() const = 0;
    virtual float voiceEnv() const = 0;
    virtual int soundingCount() const = 0;
};

} // namespace psyb
