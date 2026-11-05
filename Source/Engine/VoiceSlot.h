#pragma once

namespace psyb
{

/** Scheduling state for one slot. The bass and sine modules both use this layout. */
struct VoiceSlot
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
    int fadeSpan = 1;
};

} // namespace psyb
