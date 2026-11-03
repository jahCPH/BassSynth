#pragma once

namespace psyb
{

/** Samples reported to the host via setLatencySamples.

    Stays 0 until oversampling is wired. Oversampling latency and scheduling
    delay are different things; this value is only the former.
*/
int hostReportedLatencySamples();

} // namespace psyb
