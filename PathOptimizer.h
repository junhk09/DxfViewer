#pragma once

#include "DxfParser.h"

#include <functional>

class TravelModel
{
public:
    bool lift = false;
    double safeZ = 0;

    double Cost(const Vec3& a, const Vec3& b) const;
};

bool SamePoint(const Vec3& a, const Vec3& b);

class CutOrderOptions
{
public:
    const std::vector<int>* layerRank = nullptr;

    TravelModel travel;

    std::function<bool(const wchar_t* phase, double fraction)> progress;
};

class CutOrderReport
{
public:
    int groups = 0;
    bool cancelled = false;
};

CutOrderReport OptimizeCutOrder(DxfDrawing& dwg, const Vec3& start, const CutOrderOptions& options);
