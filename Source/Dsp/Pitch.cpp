#include "Dsp/Pitch.h"

#include <cmath>

namespace psyb
{

double midiFreq (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}

} // namespace psyb
