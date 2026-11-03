#include <juce_core/juce_core.h>

#include "Dsp/ReportedLatency.h"

int main()
{
    if (psyb::hostReportedLatencySamples() != 0)
    {
        juce::Logger::writeToLog ("hostReportedLatencySamples must stay 0 until oversampling exists");
        return 1;
    }

    return 0;
}
