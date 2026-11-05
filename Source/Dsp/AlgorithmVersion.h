#pragma once

namespace psyb
{

/** Saved per engine. Missing fields load as 1. Nothing selects an algorithm from these yet. */
struct AlgorithmVersions
{
    int roll = 1;
    int slide = 1;
    int fx = 1;
};

struct AlgorithmVersionFields
{
    bool hasRoll = false;
    bool hasSlide = false;
    bool hasFx = false;
    int roll = 1;
    int slide = 1;
    int fx = 1;
};

inline AlgorithmVersions resolveAlgorithmVersions (AlgorithmVersionFields fields)
{
    AlgorithmVersions versions;
    if (fields.hasRoll)
        versions.roll = fields.roll;
    if (fields.hasSlide)
        versions.slide = fields.slide;
    if (fields.hasFx)
        versions.fx = fields.fx;
    return versions;
}

} // namespace psyb
