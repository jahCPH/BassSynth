#include "Dsp/ReportedLatency.h"

#include <cstdio>

int main()
{
    if (psyb::hostReportedLatencySamples() != 0)
    {
        std::fputs("hostReportedLatencySamples must stay 0 until oversampling exists\n", stderr);
        return 1;
    }

    return 0;
}
