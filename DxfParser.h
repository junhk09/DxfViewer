#pragma once

#include <string>
#include <vector>

class Vec3
{
public:
    double x = 0, y = 0, z = 0;
};

enum class EntType { Line, Circle, Arc, Point };   // 읽는 엔티티는 LINE, CIRCLE, ARC, POINT 네 가지뿐 (나머지는 로그에 "해석 불가")

class DxfEntity
{
public:
    EntType type = EntType::Line;
    std::wstring layer = L"0";
    int color = 256;
    std::vector<Vec3> pts;
    int sourceLog = -1;
};

class DxfLogEntry
{
public:
    std::wstring section;
    std::wstring type;
    std::wstring handle;
    std::wstring layer;
    int color = 256;
    std::wstring detail;
    bool drawn = true;
};

class DxfLayer
{
public:
    std::wstring name;
    int color = 7;
    bool off = false;
    bool frozen = false;
    bool visible = true;
    int entityCount = 0;
};

class DxfDrawing
{
public:
    int insUnits = 0;
    std::vector<DxfLayer> layers;
    std::vector<DxfEntity> entities;
    std::vector<DxfLogEntry> log;
    bool hasExtents = false;
    Vec3 extMin, extMax;

    void Clear();
    int FindLayer(const std::wstring& name) const;
};

bool LoadDxf(const std::wstring& path, DxfDrawing& out, std::wstring& error);

unsigned int AciToRgb(int aci);
