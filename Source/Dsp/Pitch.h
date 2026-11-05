#pragma once

#include <algorithm>
#include <cmath>

namespace psyb
{

double midiFreq (double note);

inline int samplesFor (double seconds, double rate)
{
    return std::max (1, static_cast<int> (std::lround (seconds * rate)));
}

} // namespace psyb
