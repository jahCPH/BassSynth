#pragma once

namespace psyb
{

/** Router, sequencer, arp, and voices land in this library.

    The third-note retrigger strategy is not decided here. This header only
    marks the target so the plugin and the headless tests share one DSP lib.
*/
struct EngineSlot
{
};

} // namespace psyb
