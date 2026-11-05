#pragma once

#include "Engine/EngineModule.h"

#include <memory>
#include <vector>

namespace psyb
{

/** The only place engines are listed. Tests can drop one and still render the rest. */
std::vector<std::unique_ptr<EngineModule>> createEngines();

} // namespace psyb
