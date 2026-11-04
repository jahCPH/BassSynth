#pragma once

#include <cstdint>

namespace psyb
{

constexpr int kEngineCount = 3;
constexpr int kMaxSteps = 16;
constexpr int kMaxHeld = 16;
constexpr int kMaxTriggers = 64;

enum class SourceMode : int { Seq = 0, Midi = 1 };
enum class ArpOrder : int { Up = 0, Down = 1, UpDown = 2, AsPlayed = 3, Random = 4 };
enum class DuckSource : int { Internal = 0, Midi = 1, Audio = 2 };

struct Step
{
    bool on = false;
    bool tie = false;
    float gate = 0.5f;
    int velocity = 100;
    bool accent = false;
    int offset = 0;
    bool glideOverride = false;
    float glidePercent = 0.f;
};

struct LaneConfig
{
    int length = 16;
    double rateQuarters = 0.25;
    bool swingOwn = false;
    double swing = 0.0;
    int transpose = 0;
    SourceMode source = SourceMode::Seq;
    float seqGlide = 0.f;
    float midiGlideMs = 0.f;
    bool enabled = true;
};

struct MidiIn
{
    int sample = 0;
    int channel = 0;
    int note = 0;
    int velocity = 0;
    int cc = -1;
};

struct Block
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double ppqStart = 0.0;
    int numSamples = 0;
    bool playing = false;
};

struct GlideInfo
{
    float requestedPercent = 0.f;
    float requestedMs = 0.f;
    float effectivePercent = 0.f;
    float effectiveMs = 0.f;
    bool clamped = false;
    bool applies = false;
};

struct Trigger
{
    int sample = 0;
    int engine = 0;
    int note = 60;
    bool retrigger = true;
    GlideInfo glide {};
};

/** Step onset in quarter-notes. Odd steps are delayed by swing * rate / 2. */
double stepStartPpq (int64_t stepIndex, double rateQuarters, double swing);

/** First sample index at or after a PPQ, measured from ppqOrigin. */
int sampleAtPpq (double ppq, double ppqOrigin, double bpm, double sampleRate);

int clampMidi (int note);

class PsyEngine
{
public:
    void reset();
    void loadInit();

    void setOrder (ArpOrder order);
    void setLatch (bool latch);
    void setDuckSource (DuckSource source);
    void setDuckNote (int note);
    void setGlobalSwing (double swing);
    void setLane (int engine, const LaneConfig& lane);
    void setStep (int engine, int index, const Step& step);
    LaneConfig lane (int engine) const;
    Step step (int engine, int index) const;

    void process (const Block& block, const MidiIn* midi, int midiCount, float* monoOut);

    int soundingVoices() const;
    int triggerCount() const;
    Trigger trigger (int index) const;
    bool routeHeld (int channel, int note) const;
    int heldCount() const;
    bool voiceActive (int engine) const;
    bool voiceReleasing (int engine) const;

private:
    struct Voice
    {
        bool used = false;
        bool releasing = false;
        int note = 60;
        double phase = 0.0;
        double env = 0.0;
        double releaseEnv = 0.0;
        int life = 0;
        int releaseLife = 0;
        bool gliding = false;
        double glideFrom = 60.0;
        double glideTo = 60.0;
        int glideSamples = 0;
        int glidePos = 0;
        bool tail = false;
        int tailLeft = 0;
    };

    struct Arp
    {
        int index = 0;
        int dir = 1;
        uint32_t rng = 1;
    };

    struct EngineLane
    {
        LaneConfig cfg {};
        Step steps[kMaxSteps] {};
        Arp arp {};
        Voice active {};
        Voice tail {};
        bool pendingOff = false;
        double pendingOffPpq = 0.0;
        SourceMode applied = SourceMode::Seq;
        int lastArpNote = 60;
        float stealEnv = 0.f;
        int stealLeft = 0;
    };

    double renderRate = 48000.0;

    void clearVoices();
    void noteOn (int sample, int channel, int note, int velocity);
    void noteOff (int sample, int channel, int note);
    void allNotesOff (int sample, bool hard);
    void fireStep (int engine, int64_t absoluteStep, int sample, double startPpq, double endPpq, const Block& block);
    void releaseEngine (int engine, int sample, bool seqOnly);
    void startVoice (int engine, int sample, int note, bool retrigger, const GlideInfo& glide, float glideMs);
    void renderSample (float& out);
    int pickNote (int engine);
    void advanceArp (int engine);
    void retargetArp (int engine);

    DuckSource duckSource = DuckSource::Internal;
    int duckNote = 36;
    ArpOrder order = ArpOrder::Up;
    bool latch = false;
    double globalSwing = 0.0;

    int held[kMaxHeld] {};
    int nHeld = 0;
    int down[kMaxHeld] {};
    int downCount = 0;

    uint8_t route[16][128] {};

    int midiStack[kEngineCount][8] {};
    int midiStackN[kEngineCount] {};

    EngineLane lanes[kEngineCount] {};
    Trigger triggers[kMaxTriggers] {};
    int triggersN = 0;
    bool transportWasPlaying = false;
};

} // namespace psyb
