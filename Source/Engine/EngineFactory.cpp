#include "Engine/EngineFactory.h"
#include "Engine/BassEngine.h"
#include "Engine/SineEngine.h"

namespace psyb
{

std::vector<std::unique_ptr<EngineModule>> createEngines()
{
    std::vector<std::unique_ptr<EngineModule>> engines;
    engines.push_back (std::make_unique<BassEngine> (BassRole::Roll));
    engines.push_back (std::make_unique<BassEngine> (BassRole::Slide));
    engines.push_back (std::make_unique<SineEngine>());
    return engines;
}

} // namespace psyb
