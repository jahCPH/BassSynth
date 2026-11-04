#pragma once

namespace psyb
{

/** Samples reported to the host via setLatencySamples.

    This is the Rolling drive's 2x halfband delay. Scheduling delay is separate.
*/
int hostReportedLatencySamples();

} // namespace psyb
