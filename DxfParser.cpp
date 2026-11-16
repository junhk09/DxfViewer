#include "pch.h"
#include "DxfParser.h"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace {

const double kPi = 3.14159265358979323846;

Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

Vec3 Normalize(const Vec3& v)
{
    double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-12)
        return { 0, 0, 1 };
    return { v.x / len, v.y / len, v.z / len };
}

class Ocs
{
public:
    explicit Ocs(const Vec3& normal)
    {
        m_az = Normalize(normal);
        m_identity = std::fabs(m_az.x) < 1e-12 && std::fabs(m_az.y) < 1e-12 && m_az.z > 0;
        const double limit = 1.0 / 64.0;
        if (std::fabs(m_az.x) < limit && std::fabs(m_az.y) < limit)
            m_ax = Normalize(Cross({ 0, 1, 0 }, m_az));
        else
            m_ax = Normalize(Cross({ 0, 0, 1 }, m_az));
        m_ay = Normalize(Cross(m_az, m_ax));
    }

    Vec3 ToWcs(double x, double y, double z) const
    {
        if (m_identity)
            return { x, y, z };
        return { x * m_ax.x + y * m_ay.x + z * m_az.x,
                 x * m_ax.y + y * m_ay.y + z * m_az.y,
                 x * m_ax.z + y * m_ay.z + z * m_az.z };
    }

private:
    Vec3 m_ax, m_ay, m_az;
    bool m_identity = true;
};

using Group = std::pair<int, std::wstring>;

class Record
{
public:
    std::wstring type;
    std::vector<Group> groups;
};

const std::wstring* Find(const Record& r, int code)
{
    for (const auto& g : r.groups)
        if (g.first == code)
            return &g.second;
    return nullptr;
}

double Dbl(const Record& r, int code, double def = 0.0)
{
    const std::wstring* v = Find(r, code);
    return v ? _wtof(v->c_str()) : def;
}

int Int(const Record& r, int code, int def = 0)
{
    const std::wstring* v = Find(r, code);
    return v ? _wtoi(v->c_str()) : def;
}

std::wstring Str(const Record& r, int code, const wchar_t* def = L"")
{
    const std::wstring* v = Find(r, code);
    return v ? *v : std::wstring(def);
}

Vec3 Extrusion(const Record& r)
{
    return { Dbl(r, 210, 0), Dbl(r, 220, 0), Dbl(r, 230, 1) };
}

std::string TrimA(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

std::wstring TrimW(const std::wstring& s)
{
    size_t b = s.find_first_not_of(L" \t\r");
    if (b == std::wstring::npos)
        return {};
    size_t e = s.find_last_not_of(L" \t\r");
    return s.substr(b, e - b + 1);
}

std::wstring UnescapeUnicode(const std::wstring& s)
{
    if (s.find(L"\\U+") == std::wstring::npos && s.find(L"\\u+") == std::wstring::npos)
        return s;
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == L'\\' && i + 6 < s.size() && (s[i + 1] == L'U' || s[i + 1] == L'u') && s[i + 2] == L'+'
            && iswxdigit(s[i + 3]) && iswxdigit(s[i + 4]) && iswxdigit(s[i + 5]) && iswxdigit(s[i + 6]))
        {
            out += static_cast<wchar_t>(wcstol(s.substr(i + 3, 4).c_str(), nullptr, 16));
            i += 6;
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

std::wstring Decode(const std::string& s, UINT cp)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return std::wstring(s.begin(), s.end());
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return UnescapeUnicode(w);
}

int SegmentsFor(double sweep)
{
    int n = static_cast<int>(std::ceil(std::fabs(sweep) / (2 * kPi) * 96));
    return std::clamp(n, 4, 1024);
}

void AppendArc(std::vector<Vec3>& out, const Ocs& ocs, double cx, double cy, double z,
               double r, double a0, double sweep, bool skipFirst)
{
    int n = SegmentsFor(sweep);
    for (int i = skipFirst ? 1 : 0; i <= n; ++i)
    {
        double a = a0 + sweep * i / n;
        out.push_back(ocs.ToWcs(cx + r * std::cos(a), cy + r * std::sin(a), z));
    }
}

class PVert
{
public:
    double x = 0, y = 0, z = 0, bulge = 0;
};

void AppendBulgePolyline(std::vector<Vec3>& out, const Ocs& ocs, const std::vector<PVert>& v, bool closed, double z)
{
    if (v.empty())
        return;
    out.push_back(ocs.ToWcs(v[0].x, v[0].y, z + v[0].z));
    size_t segs = closed ? v.size() : v.size() - 1;
    for (size_t i = 0; i < segs; ++i)
    {
        const PVert& a = v[i];
        const PVert& b = v[(i + 1) % v.size()];
        double dx = b.x - a.x, dy = b.y - a.y;
        double chord = std::hypot(dx, dy);
        if (std::fabs(a.bulge) < 1e-10 || chord < 1e-12)
        {
            out.push_back(ocs.ToWcs(b.x, b.y, z + b.z));
            continue;
        }
        double theta = 4 * std::atan(a.bulge);
        double h = chord / (2 * std::tan(theta / 2));
        double cx = (a.x + b.x) / 2 - dy / chord * h;
        double cy = (a.y + b.y) / 2 + dx / chord * h;
        double r = std::hypot(a.x - cx, a.y - cy);
        double a0 = std::atan2(a.y - cy, a.x - cx);
        AppendArc(out, ocs, cx, cy, z, r, a0, theta, true);
    }
}

Vec3 DeBoor(int p, const std::vector<double>& U, const std::vector<Vec3>& P, const std::vector<double>& W, double t)
{
    int n = static_cast<int>(P.size());
    int k = p;
    while (k < n - 1 && t >= U[k + 1])
        ++k;
    std::vector<double> dx(p + 1), dy(p + 1), dz(p + 1), dw(p + 1);
    for (int j = 0; j <= p; ++j)
    {
        int idx = k - p + j;
        dx[j] = P[idx].x * W[idx];
        dy[j] = P[idx].y * W[idx];
        dz[j] = P[idx].z * W[idx];
        dw[j] = W[idx];
    }
    for (int r = 1; r <= p; ++r)
    {
        for (int j = p; j >= r; --j)
        {
            int i = k - p + j;
            double den = U[i + p - r + 1] - U[i];
            double a = den > 1e-15 ? (t - U[i]) / den : 0.0;
            dx[j] = (1 - a) * dx[j - 1] + a * dx[j];
            dy[j] = (1 - a) * dy[j - 1] + a * dy[j];
            dz[j] = (1 - a) * dz[j - 1] + a * dz[j];
            dw[j] = (1 - a) * dw[j - 1] + a * dw[j];
        }
    }
    if (std::fabs(dw[p]) < 1e-15)
        return { dx[p], dy[p], dz[p] };
    return { dx[p] / dw[p], dy[p] / dw[p], dz[p] / dw[p] };
}

void AppendSpline(std::vector<Vec3>& out, const Record& r)
{
    int degree = Int(r, 71, 3);
    std::vector<double> knots, weights;
    std::vector<Vec3> ctrl, fit;
    for (const auto& g : r.groups)
    {
        double v = _wtof(g.second.c_str());
        switch (g.first)
        {
        case 40: knots.push_back(v); break;
        case 41: weights.push_back(v); break;
        case 10: ctrl.push_back({ v, 0, 0 }); break;
        case 20: if (!ctrl.empty()) ctrl.back().y = v; break;
        case 30: if (!ctrl.empty()) ctrl.back().z = v; break;
        case 11: fit.push_back({ v, 0, 0 }); break;
        case 21: if (!fit.empty()) fit.back().y = v; break;
        case 31: if (!fit.empty()) fit.back().z = v; break;
        }
    }
    int n = static_cast<int>(ctrl.size());
    if (n >= 2 && degree >= 1 && degree < n && static_cast<int>(knots.size()) == n + degree + 1)
    {
        if (static_cast<int>(weights.size()) != n)
            weights.assign(n, 1.0);
        double t0 = knots[degree], t1 = knots[n];
        int samples = std::clamp(n * 16, 32, 4000);
        for (int s = 0; s <= samples; ++s)
            out.push_back(DeBoor(degree, knots, ctrl, weights, t0 + (t1 - t0) * s / samples));
        return;
    }
    for (const Vec3& p : (n >= 2 ? ctrl : fit))
        out.push_back(p);
}

void AppendEllipse(std::vector<Vec3>& out, const Record& r)
{
    Vec3 c{ Dbl(r, 10), Dbl(r, 20), Dbl(r, 30) };
    Vec3 major{ Dbl(r, 11), Dbl(r, 21), Dbl(r, 31) };
    double ratio = Dbl(r, 40, 1);
    Vec3 minor = Cross(Normalize(Extrusion(r)), major);
    double t0 = Dbl(r, 41, 0), t1 = Dbl(r, 42, 2 * kPi);
    double sweep = t1 - t0;
    while (sweep <= 0)
        sweep += 2 * kPi;
    int n = SegmentsFor(sweep);
    for (int i = 0; i <= n; ++i)
    {
        double t = t0 + sweep * i / n;
        out.push_back({ c.x + major.x * std::cos(t) + minor.x * ratio * std::sin(t),
                        c.y + major.y * std::cos(t) + minor.y * ratio * std::sin(t),
                        c.z + major.z * std::cos(t) + minor.z * ratio * std::sin(t) });
    }
}

std::wstring Format(const wchar_t* fmt, ...)
{
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    return buf;
}

std::wstring Pt(const Record& r, int xCode)
{
    return Format(L"(%.4f, %.4f, %.4f)", Dbl(r, xCode), Dbl(r, xCode + 10), Dbl(r, xCode + 20));
}

int CountCode(const Record& r, int code)
{
    return static_cast<int>(std::count_if(r.groups.begin(), r.groups.end(),
                                          [code](const Group& g) { return g.first == code; }));
}

size_t ParseEntity(const std::vector<Record>& recs, size_t i, std::vector<DxfEntity>& out,
                   bool& paperSpace,
                   std::vector<DxfLogEntry>& log, const std::wstring& section)
{
    const Record& r = recs[i];
    const std::wstring& t = r.type;
    size_t last = i;
    paperSpace = Int(r, 67, 0) == 1;

    DxfEntity e;
    e.layer = Str(r, 8, L"0");
    e.color = Int(r, 62, 256);

    log.push_back({ section, t, Str(r, 5), e.layer, e.color });
    DxfLogEntry& entry = log.back();
    e.sourceLog = static_cast<int>(log.size()) - 1;
    if (Find(r, 210))
        entry.detail = L"법선 " + Pt(r, 210) + L"  ";

    const bool invisible = Int(r, 60, 0) == 1;
    auto keep = [&]() {
        if (!invisible)
            return true;
        entry.drawn = false;
        entry.detail += L"  (숨김: 그룹 코드 60)";
        return false;
    };

    if (t == L"LINE")
    {
        e.type = EntType::Line;
        e.pts ={ { Dbl(r, 10), Dbl(r, 20), Dbl(r, 30) }, { Dbl(r, 11), Dbl(r, 21), Dbl(r, 31) } };
        double dx = e.pts[1].x - e.pts[0].x, dy = e.pts[1].y - e.pts[0].y, dz = e.pts[1].z - e.pts[0].z;
        entry.detail += Format(L"시작 %s  끝 %s  길이 %.4f", Pt(r, 10).c_str(), Pt(r, 11).c_str(),
                               std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    else if (t == L"POINT")
    {
        e.type = EntType::Point;
        e.pts = { { Dbl(r, 10), Dbl(r, 20), Dbl(r, 30) } };
        entry.detail += L"위치 " + Pt(r, 10);
    }
    else if (t == L"CIRCLE" || t == L"ARC")
    {
        e.type = (t == L"CIRCLE") ? EntType::Circle : EntType::Arc;
        double a0 = 0, sweep = 2 * kPi;
        if (t == L"ARC")
        {
            a0 = Dbl(r, 50) * kPi / 180;
            sweep = Dbl(r, 51) * kPi / 180 - a0;
            while (sweep <= 0)
                sweep += 2 * kPi;
        }
        AppendArc(e.pts, Ocs(Extrusion(r)), Dbl(r, 10), Dbl(r, 20), Dbl(r, 30), Dbl(r, 40), a0, sweep, false);
        entry.detail += Format(L"중심 %s  반지름 %.4f", Pt(r, 10).c_str(), Dbl(r, 40));
        if (t == L"ARC")
            entry.detail += Format(L"  각도 %.2f° ~ %.2f°", Dbl(r, 50), Dbl(r, 51));
    }
    else if (t == L"ELLIPSE")
    {
        e.type = EntType::Ellipse;
        AppendEllipse(e.pts, r);
        entry.detail += Format(L"중심 %s  장축 %s  비율 %.4f", Pt(r, 10).c_str(), Pt(r, 11).c_str(), Dbl(r, 40, 1));
    }
    else if (t == L"LWPOLYLINE")
    {
        e.type = EntType::Polyline;
        std::vector<PVert> v;
        for (const auto& g : r.groups)
        {
            if (g.first == 10)
                v.push_back({ _wtof(g.second.c_str()), 0, 0 });
            else if (g.first == 20 && !v.empty())
                v.back().y = _wtof(g.second.c_str());
            else if (g.first == 42 && !v.empty())
                v.back().bulge = _wtof(g.second.c_str());
        }
        AppendBulgePolyline(e.pts, Ocs(Extrusion(r)), v, (Int(r, 70) & 1) != 0, Dbl(r, 38));
        int arcs = static_cast<int>(std::count_if(v.begin(), v.end(), [](const PVert& p) { return p.bulge != 0; }));
        entry.detail += Format(L"꼭짓점 %d개  %s  호 구간 %d개  고도 %.4f", static_cast<int>(v.size()),
                               (Int(r, 70) & 1) ? L"닫힘" : L"열림", arcs, Dbl(r, 38));
    }
    else if (t == L"POLYLINE")
    {
        int flags = Int(r, 70);
        std::vector<PVert> v;
        size_t j = i + 1;
        for (; j < recs.size() && recs[j].type == L"VERTEX"; ++j)
        {
            if (Int(recs[j], 70) & (16 | 128))
                continue;
            v.push_back({ Dbl(recs[j], 10), Dbl(recs[j], 20), Dbl(recs[j], 30), Dbl(recs[j], 42) });
        }
        last = (j < recs.size() && recs[j].type == L"SEQEND") ? j : j - 1;
        if (flags & (16 | 64))
        {
            entry.detail += Format(L"메시/폴리페이스  VERTEX %d개", static_cast<int>(j - i - 1));
            entry.drawn = false;
            return last;
        }
        entry.detail += Format(L"%s  꼭짓점 %d개  %s", (flags & 8) ? L"3D" : L"2D", static_cast<int>(v.size()),
                               (flags & 1) ? L"닫힘" : L"열림");
        e.type = EntType::Polyline;
        if (flags & 8)
        {
            for (auto& p : v)
                p.bulge = 0;
            AppendBulgePolyline(e.pts, Ocs({ 0, 0, 1 }), v, (flags & 1) != 0, 0);
        }
        else
        {
            for (auto& p : v)
                p.z = 0;
            AppendBulgePolyline(e.pts, Ocs(Extrusion(r)), v, (flags & 1) != 0, Dbl(r, 30));
        }
    }
    else if (t == L"SPLINE")
    {
        e.type = EntType::Spline;
        AppendSpline(e.pts, r);
        entry.detail += Format(L"차수 %d  제어점 %d개  맞춤점 %d개  매듭 %d개", Int(r, 71, 3),
                               CountCode(r, 10), CountCode(r, 11), CountCode(r, 40));
    }
    else if (t == L"SOLID" || t == L"TRACE" || t == L"3DFACE")
    {
        e.type = EntType::Face;
        Vec3 p[4] = { { Dbl(r, 10), Dbl(r, 20), Dbl(r, 30) }, { Dbl(r, 11), Dbl(r, 21), Dbl(r, 31) },
                      { Dbl(r, 12), Dbl(r, 22), Dbl(r, 32) }, { Dbl(r, 13), Dbl(r, 23), Dbl(r, 33) } };
        entry.detail += Format(L"꼭짓점 %s %s %s %s", Pt(r, 10).c_str(), Pt(r, 11).c_str(),
                               Pt(r, 12).c_str(), Pt(r, 13).c_str());
        if (t == L"3DFACE")
        {
            e.pts = { p[0], p[1], p[2], p[3], p[0] };
        }
        else
        {
            Ocs ocs(Extrusion(r));
            double z = Dbl(r, 30);
            for (int k : { 0, 1, 3, 2, 0 })
                e.pts.push_back(ocs.ToWcs(p[k].x, p[k].y, z));
        }
    }
    else if (t == L"INSERT" || t == L"DIMENSION")
    {
        entry.drawn = false;
        entry.detail += Format(L"블록 %s  위치 %s  (블록은 그리지 않음)", Str(r, 2).c_str(), Pt(r, 10).c_str());
        if (t == L"INSERT" && Int(r, 66) == 1)
        {
            size_t j = i + 1;
            while (j < recs.size() && recs[j].type == L"ATTRIB")
                ++j;
            last = (j < recs.size() && recs[j].type == L"SEQEND") ? j : j - 1;
        }
        return last;
    }
    else
    {
        entry.drawn = false;
        if (t == L"TEXT" || t == L"ATTDEF")
            entry.detail += Format(L"내용 \"%s\"  위치 %s  높이 %.4f", Str(r, 1).c_str(), Pt(r, 10).c_str(), Dbl(r, 40));
        else if (t == L"MTEXT")
        {
            std::wstring text;
            for (const auto& g : r.groups)
                if (g.first == 3 || g.first == 1)
                    text += g.second;
            entry.detail += Format(L"내용 \"%s\"  위치 %s  높이 %.4f", text.c_str(), Pt(r, 10).c_str(), Dbl(r, 40));
        }
        else if (t == L"HATCH")
            entry.detail += Format(L"패턴 %s  경계 %d개", Str(r, 2).c_str(), Int(r, 91));
        else
            entry.detail += L"뷰어에서 그리지 않는 엔티티";
        return last;
    }

    if (!e.pts.empty() && keep())
        out.push_back(std::move(e));
    return last;
}

void ParseHeader(const Record& section, DxfDrawing& dwg)
{
    std::wstring var;
    for (const auto& g : section.groups)
    {
        if (g.first == 9)
            var = TrimW(g.second);
        else if (var == L"$INSUNITS" && g.first == 70)
            dwg.insUnits = _wtoi(g.second.c_str());
    }
}

UINT DetectCodePage(const std::vector<std::string>& lines)
{
    UINT cp = CP_ACP;
    std::string version;
    for (size_t i = 0; i + 2 < lines.size() && i < 20000; ++i)
    {
        std::string s = TrimA(lines[i]);
        if (s == "$ACADVER")
            version = TrimA(lines[i + 2]);
        else if (s == "$DWGCODEPAGE")
        {
            std::string name = TrimA(lines[i + 2]);
            if (name.size() > 5 && _strnicmp(name.c_str(), "ANSI_", 5) == 0)
            {
                UINT n = static_cast<UINT>(atoi(name.c_str() + 5));
                if (n && IsValidCodePage(n))
                    cp = n;
            }
        }
        else if (s == "ENDSEC")
            break;
    }
    if (version >= "AC1021")
        cp = CP_UTF8;
    return cp;
}

}

void DxfDrawing::Clear()
{
    *this = DxfDrawing();
}

int DxfDrawing::FindLayer(const std::wstring& name) const
{
    for (size_t i = 0; i < layers.size(); ++i)
        if (layers[i].name == name)
            return static_cast<int>(i);
    return -1;
}

bool LoadDxf(const std::wstring& path, DxfDrawing& dwg, std::wstring& error)
{
    dwg.Clear();

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = L"파일을 열 수 없습니다.";
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.compare(0, 18, "AutoCAD Binary DXF") == 0)
    {
        error = L"바이너리 DXF는 아직 지원하지 않습니다.";
        return false;
    }

    size_t pos = (data.size() >= 3 && data.compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3 : 0;
    std::vector<std::string> lines;
    lines.reserve(data.size() / 8);
    while (pos < data.size())
    {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos)
            end = data.size();
        size_t len = end - pos;
        if (len && data[pos + len - 1] == '\r')
            --len;
        lines.emplace_back(data, pos, len);
        pos = end + 1;
    }

    UINT cp = DetectCodePage(lines);
    std::vector<Record> recs;
    for (size_t i = 0; i + 1 < lines.size(); i += 2)
    {
        std::string codeText = TrimA(lines[i]);
        char* endp = nullptr;
        long code = strtol(codeText.c_str(), &endp, 10);
        if (codeText.empty() || *endp)
        {
            error = L"그룹 코드를 읽을 수 없습니다 (" + std::to_wstring(i + 1) + L"번째 줄).";
            return false;
        }
        std::wstring value = Decode(lines[i + 1], cp);
        if (code == 0)
            recs.push_back({ TrimW(value), {} });
        else if (!recs.empty())
            recs.back().groups.emplace_back(static_cast<int>(code), std::move(value));
    }

    enum class Section { None, Header, Tables, Entities, Other } section = Section::None;
    bool sawEof = false;

    for (size_t i = 0; i < recs.size(); ++i)
    {
        const Record& r = recs[i];
        if (r.type == L"SECTION")
        {
            std::wstring name = TrimW(Str(r, 2));
            section = name == L"HEADER" ? Section::Header
                    : name == L"TABLES" ? Section::Tables
                    : name == L"ENTITIES" ? Section::Entities
                    : Section::Other;
            if (section == Section::Header)
                ParseHeader(r, dwg);
            continue;
        }
        if (r.type == L"ENDSEC")
        {
            section = Section::None;
            continue;
        }
        if (r.type == L"EOF")
        {
            sawEof = true;
            break;
        }

        bool paperSpace = false;
        switch (section)
        {
        case Section::Tables:
            if (r.type == L"LAYER")
            {
                DxfLayer layer;
                layer.name = Str(r, 2);
                int color = Int(r, 62, 7);
                layer.off = color < 0;
                layer.color = std::abs(color);
                layer.frozen = (Int(r, 70) & 1) != 0;
                layer.visible = !layer.off && !layer.frozen;
                if (dwg.FindLayer(layer.name) < 0)
                    dwg.layers.push_back(layer);
            }
            break;

        case Section::Entities:
        {
            size_t before = dwg.entities.size();
            i = ParseEntity(recs, i, dwg.entities, paperSpace, dwg.log, L"ENTITIES");
            if (paperSpace && dwg.entities.size() > before)
            {
                dwg.entities.resize(before);
                dwg.log.back().drawn = false;
                dwg.log.back().detail += L"  (배치 공간)";
            }
            break;
        }

        default:
            break;
        }
    }

    if (!sawEof && recs.empty())
    {
        error = L"DXF 데이터가 없습니다.";
        return false;
    }

    std::unordered_map<std::wstring, int> index;
    for (size_t k = 0; k < dwg.layers.size(); ++k)
        index[dwg.layers[k].name] = static_cast<int>(k);
    for (DxfEntity& e : dwg.entities)
    {
        auto it = index.find(e.layer);
        if (it == index.end())
        {
            DxfLayer layer;
            layer.name = e.layer;
            dwg.layers.push_back(layer);
            it = index.emplace(e.layer, static_cast<int>(dwg.layers.size() - 1)).first;
        }
        e.layerIndex = it->second;
        dwg.layers[e.layerIndex].entityCount++;
        for (const Vec3& p : e.pts)
        {
            if (!dwg.hasExtents)
            {
                dwg.extMin = dwg.extMax = p;
                dwg.hasExtents = true;
            }
            dwg.extMin.x = std::min(dwg.extMin.x, p.x);
            dwg.extMin.y = std::min(dwg.extMin.y, p.y);
            dwg.extMin.z = std::min(dwg.extMin.z, p.z);
            dwg.extMax.x = std::max(dwg.extMax.x, p.x);
            dwg.extMax.y = std::max(dwg.extMax.y, p.y);
            dwg.extMax.z = std::max(dwg.extMax.z, p.z);
        }
    }
    return true;
}

unsigned int AciToRgb(int aci)
{
    static const unsigned int basic[10] = { 0x000000, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF,
                                            0x0000FF, 0xFF00FF, 0xFFFFFF, 0x808080, 0xC0C0C0 };
    aci = std::abs(aci);
    if (aci >= 1 && aci <= 9)
        return basic[aci];
    if (aci >= 250 && aci <= 255)
    {
        static const unsigned int gray[6] = { 0x33, 0x5B, 0x84, 0xAD, 0xD6, 0xFF };
        unsigned int g = gray[aci - 250];
        return (g << 16) | (g << 8) | g;
    }
    if (aci >= 10 && aci <= 249)
    {
        static const double value[5] = { 1.0, 0.65, 0.5, 0.3, 0.15 };
        double h = ((aci - 10) / 10) * 15.0;
        int sub = aci % 10;
        double v = value[sub / 2];
        double s = (sub % 2) ? 0.5 : 1.0;
        double c = v * s;
        double x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1));
        double m = v - c;
        double r = 0, g = 0, b = 0;
        if (h < 60)       { r = c; g = x; }
        else if (h < 120) { r = x; g = c; }
        else if (h < 180) { g = c; b = x; }
        else if (h < 240) { g = x; b = c; }
        else if (h < 300) { r = x; b = c; }
        else              { r = c; b = x; }
        auto to8 = [](double f) { return static_cast<unsigned int>(std::lround(std::clamp(f, 0.0, 1.0) * 255)); };
        return (to8(r + m) << 16) | (to8(g + m) << 8) | to8(b + m);
    }
    return 0xFFFFFF;
}
