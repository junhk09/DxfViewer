#include "pch.h"
#include "DxfParser.h"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iterator>

// 이 파일 안에서만 쓰는 도우미 함수와 클래스 (이름 없는 namespace)
namespace {

const double kPi = 3.14159265358979323846;

// 두 벡터의 외적 (두 벡터에 모두 수직인 벡터). Ocs의 축을 만들 때 씀
Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

// 길이를 1로 맞춘 벡터. 길이가 0에 가까우면 (0, 0, 1)
Vec3 Normalize(const Vec3& v)
{
    double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-12)
        return { 0, 0, 1 };
    return { v.x / len, v.y / len, v.z / len };
}

// 도형 좌표계(OCS) → 도면 좌표계(WCS) 변환. 법선(210/220/230)이 기울어진 원·호를 3D 공간에 바르게 놓을 때 씀
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

// 그룹 코드와 값 한 쌍 (예: 8, "OUTER")
using Group = std::pair<int, std::wstring>;

// 레코드 하나: 그룹 코드 0의 값(type, 예: "LINE")과 그 뒤에 오는 (코드, 값) 목록
class Record
{
public:
    std::wstring type;
    std::vector<Group> groups;
};

// 레코드에서 그룹 코드 code의 값을 찾음. 없으면 nullptr
const std::wstring* Find(const Record& r, int code)
{
    for (const auto& g : r.groups)
        if (g.first == code)
            return &g.second;
    return nullptr;
}

// 그룹 코드 값을 실수로 읽음 (없으면 def)
double Dbl(const Record& r, int code, double def = 0.0)
{
    const std::wstring* v = Find(r, code);
	return v ? _wtof(v->c_str()) : def; //wstring을 double로 변환
}

// 그룹 코드 값을 정수로 읽음 (없으면 def)
int Int(const Record& r, int code, int def = 0)
{
    const std::wstring* v = Find(r, code);
    return v ? _wtoi(v->c_str()) : def;
}

// 그룹 코드 값을 문자열로 읽음 (없으면 def)
std::wstring Str(const Record& r, int code, const wchar_t* def = L"")
{
    const std::wstring* v = Find(r, code);
    return v ? *v : std::wstring(def);
}

// 법선 벡터(210/220/230)를 읽음. 없으면 (0, 0, 1) = XY 평면에 평평한 도형
Vec3 Extrusion(const Record& r)
{
    return { Dbl(r, 210, 0), Dbl(r, 220, 0), Dbl(r, 230, 1) };
}

// 바이트 문자열(std::string)용: 그룹 코드 줄을 숫자로 바꾸기 전에 다듬을 때 씀
std::string TrimA(const std::string& s) // 공백, 탭, \r 제거
{
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

// 유니코드 문자열(std::wstring)용: 레코드 종류·헤더 변수 이름을 다듬을 때 씀
std::wstring TrimW(const std::wstring& s) // 공백, 탭, \r 제거
{
    size_t b = s.find_first_not_of(L" \t\r");
    if (b == std::wstring::npos)
        return {};
    size_t e = s.find_last_not_of(L" \t\r");
    return s.substr(b, e - b + 1);
}

// 값 속의 \U+XXXX 표기를 실제 유니코드 글자로 바꿈 (예: \U+D55C → 한)
std::wstring UnescapeUnicode(const std::wstring& s)
{
    if (s.find(L"\\U+") == std::wstring::npos && s.find(L"\\u+") == std::wstring::npos)
        return s;
    std::wstring out;
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

// 파일에서 읽은 바이트 문자열을 코드페이지 cp로 유니코드(wstring)로 바꿈
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

// 호를 몇 개의 짧은 선분으로 나눌지 (한 바퀴에 96개 기준, 4~1024개)
int SegmentsFor(double sweep)
{
    int n = static_cast<int>(std::ceil(std::fabs(sweep) / (2 * kPi) * 96));
    return std::clamp(n, 4, 1024);
}

// 중심·반지름·시작각·사잇각으로 원/호 위의 점들을 계산해 out 뒤에 추가
void AppendArc(std::vector<Vec3>& out, const Ocs& ocs, double cx, double cy, double z,
               double r, double a0, double sweep)
{
    int n = SegmentsFor(sweep);
    for (int i = 0; i <= n; ++i)
    {
        double a = a0 + sweep * i / n;
        out.push_back(ocs.ToWcs(cx + r * std::cos(a), cy + r * std::sin(a), z));
    }
}

// printf처럼 서식 문자열로 wstring을 만듦 (로그 detail 글자용)
std::wstring Format(const wchar_t* fmt, ...)
{
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    return buf;
}

// 좌표 하나를 "(x, y, z)" 글자로 만듦 (그룹 코드 xCode, xCode+10, xCode+20)
std::wstring Pt(const Record& r, int xCode)
{
    return Format(L"(%.4f, %.4f, %.4f)", Dbl(r, xCode), Dbl(r, xCode + 10), Dbl(r, xCode + 20));
}

// ENTITIES 레코드 하나 처리: 로그 한 줄은 항상 추가, 그릴 수 있으면(LINE·CIRCLE·ARC·POINT) 도형도 추가.
// 딸린 레코드(VERTEX·ATTRIB … SEQEND)를 건너뛰었으면 마지막으로 처리한 레코드 번호(last)를 돌려줌
size_t ParseEntity(const std::vector<Record>& recs, size_t i, std::vector<DxfEntity>& out,
                   std::vector<DxfLogEntry>& log, const std::wstring& section)
{
    const std::wstring& t = recs[i].type;
    size_t last = i;

    DxfEntity e;
    e.layer = Str(recs[i], 8, L"0");
    e.color = Int(recs[i], 62, 256);

    log.push_back({ section, t, Str(recs[i], 5), e.layer, e.color });
    e.sourceLog = static_cast<int>(log.size()) - 1;
    if (Find(recs[i], 210))
        log.back().detail = L"법선 " + Pt(recs[i], 210) + L"  ";

	const bool invisible = Int(recs[i], 60, 0) == 1; // 그룹코드 60번의 값이 1이면 안그림.
    auto keep = [&]() {
        if (!invisible)
            return true;
        log.back().drawn = false;
        log.back().detail += L"  (숨김: 그룹 코드 60)";
        return false;
    };

    if (t == L"LINE")
    {
        e.type = EntType::Line;
        e.pts ={ { Dbl(recs[i], 10), Dbl(recs[i], 20), Dbl(recs[i], 30) }, { Dbl(recs[i], 11), Dbl(recs[i], 21), Dbl(recs[i], 31) } };
        double dx = e.pts[1].x - e.pts[0].x, dy = e.pts[1].y - e.pts[0].y, dz = e.pts[1].z - e.pts[0].z;
        log.back().detail += Format(L"시작 %s  끝 %s  길이 %.4f", Pt(recs[i], 10).c_str(), Pt(recs[i], 11).c_str(),
                               std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    else if (t == L"POINT")
    {
        e.type = EntType::Point;
        e.pts = { { Dbl(recs[i], 10), Dbl(recs[i], 20), Dbl(recs[i], 30) } };
        log.back().detail += L"위치 " + Pt(recs[i], 10);
    }
    else if (t == L"CIRCLE" || t == L"ARC")
    {
        e.type = (t == L"CIRCLE") ? EntType::Circle : EntType::Arc;
        double a0 = 0, sweep = 2 * kPi;
        if (t == L"ARC")
        {
            a0 = Dbl(recs[i], 50) * kPi / 180;
            sweep = Dbl(recs[i], 51) * kPi / 180 - a0;
            while (sweep <= 0)
                sweep += 2 * kPi;
        }
        AppendArc(e.pts, Ocs(Extrusion(recs[i])), Dbl(recs[i], 10), Dbl(recs[i], 20), Dbl(recs[i], 30), Dbl(recs[i], 40), a0, sweep);
        log.back().detail += Format(L"중심 %s  반지름 %.4f", Pt(recs[i], 10).c_str(), Dbl(recs[i], 40));
        if (t == L"ARC")
            log.back().detail += Format(L"  각도 %.2f° ~ %.2f°", Dbl(recs[i], 50), Dbl(recs[i], 51));
    }
    else                                                 // 그 밖의 엔티티 전부: 그릴 수 없음 → 로그에 "해석 불가"만 남김
    {
        log.back().drawn = false;
        log.back().detail = L"해석 불가";
        if (t == L"POLYLINE" || (t == L"INSERT" && Int(recs[i], 66) == 1)) 
        {                                                              
            size_t j = i + 1; //recs[j].type이 recs[10] POLYLINE, recs[11] VERTEX이런식인데, POLYLINE, INSERT,등을 만나면
            //  VERTEX, ATTRIB이 딸려나온다. recs[12]=VERTEX이런식으로, 따라서 그만큼을 skip해버림.
            while (j < recs.size() && (recs[j].type == L"VERTEX" || recs[j].type == L"ATTRIB")) 
                ++j;
            last = (j < recs.size() && recs[j].type == L"SEQEND") ? j : j - 1; // 마지막엔 SEQEND가 오기때문에 끝나면 -1해줌.
        }
        return last;
    }

    if (!e.pts.empty() && keep())
        out.push_back(std::move(e));
    return last;
}

// HEADER 섹션에서 도면 단위($INSUNITS)만 읽음
void ParseHeader(const Record& section, DxfDrawing& dwg)
{
    std::wstring var;
    for (const auto& g : section.groups)
    {
        if (g.first == 9)
            var = TrimW(g.second);
        else if (var == L"$INSUNITS" && g.first == 70) // 도면단위만 읽는다.
            dwg.insUnits = _wtoi(g.second.c_str());
    }
}

// 레코드를 만들기 전에 줄을 훑어 글자 인코딩을 정함 ($ACADVER가 2007 이상이면 UTF-8, 아니면 $DWGCODEPAGE)
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

// 도면을 빈 상태로 초기화
void DxfDrawing::Clear()
{
    *this = DxfDrawing();
}

// 레이어 이름으로 layers에서 몇 번째인지 찾음. 없으면 -1
int DxfDrawing::FindLayer(const std::wstring& name) const
{
    for (size_t i = 0; i < layers.size(); ++i)
        if (layers[i].name == name)
            return static_cast<int>(i);
    return -1;
}

// DXF 파일을 읽어 dwg(단위·레이어·도형·로그·범위)를 채움. 실패하면 false와 error 메시지
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

	size_t pos = (data.size() >= 3 && data.compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3 : 0; // UTF-8 쓸거다하는 정보 제거 하고읽기
    std::vector<std::string> lines;
    while (pos < data.size())
    {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos)
            end = data.size();
        size_t len = end - pos;
		if (len && data[pos + len - 1] == '\r') // Windows 줄바꿈이면 \r 제거
            --len;
        lines.emplace_back(data, pos, len);
        pos = end + 1;
    }

    UINT cp = DetectCodePage(lines);
    std::vector<Record> recs; // 선언
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
        if (recs[i].type == L"SECTION")
        {
            std::wstring name = TrimW(Str(recs[i], 2));
            section = name == L"HEADER" ? Section::Header
                    : name == L"TABLES" ? Section::Tables
                    : name == L"ENTITIES" ? Section::Entities
                    : Section::Other;
            if (section == Section::Header)
                ParseHeader(recs[i], dwg);
            continue;
        }
        if (recs[i].type == L"ENDSEC")
        {
            section = Section::None;
            continue;
        }
        if (recs[i].type == L"EOF")
        {
            sawEof = true;
            break;
        }

        switch (section)
        {
        case Section::Tables:
            if (recs[i].type == L"LAYER")
            {
                DxfLayer layer;
				layer.name = Str(recs[i], 2); // 레이어 이름
				int color = Int(recs[i], 62, 7); //   색상 코드 (0~256, 0이면 숨김)
                layer.off = color < 0; // 색이 음수면 꺼진 레이어
                layer.color = std::abs(color); // 색상 코드는 양수로
                layer.frozen = (Int(recs[i], 70) & 1) != 0; // 70번의 값은 2진수로 오는데, 1번 비트가 켜져 있으면 안그림.
				layer.visible = !layer.off && !layer.frozen; // 꺼지거나 얼어붙은 레이어는 안보이게 -> 실질적으로 그림/안그림 관리
                if (dwg.FindLayer(layer.name) < 0)
                    dwg.layers.push_back(layer);
            }
            break;

        case Section::Entities:
            i = ParseEntity(recs, i, dwg.entities, dwg.log, L"ENTITIES");
            break;

        default:
            break;
        }
    }

    if (!sawEof && recs.empty())
    {
        error = L"DXF 데이터가 없습니다.";
        return false;
    }

    for (DxfEntity& e : dwg.entities)
    {
        int k = dwg.FindLayer(e.layer);
        if (k >= 0)
            dwg.layers[k].entityCount++;
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

// AutoCAD 색 번호(ACI 1~255)를 RGB 값(0xRRGGBB)으로 바꿈
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
