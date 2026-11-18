#include "pch.h"
#include "PathOptimizer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>

namespace {

using Clock = std::chrono::steady_clock;

const int kMaxGridCells = 1024;

double Dist(const Vec3& a, const Vec3& b)
{
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool IsVisible(const DxfEntity& e, const DxfDrawing& dwg)
{
    int k = dwg.FindLayer(e.layer);
    return k >= 0 && dwg.layers[k].visible;
}

bool IsClosed(const DxfEntity& e)
{
    return e.pts.size() >= 3 && Dist(e.pts.front(), e.pts.back()) < 1e-9;
}

void Extend(Vec3& lo, Vec3& hi, const Vec3& p)
{
    lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
    hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
}

class Cancelled
{
};

class Progress
{
public:
    explicit Progress(const std::function<bool(const wchar_t*, double)>& callback) : m_callback(callback) {}

    void SetGroup(int group, int groups)
    {
        m_prefix = groups > 1 ? L"공정 " + std::to_wstring(group) + L"/" + std::to_wstring(groups) + L" · " : L"";
    }

    void operator()(const wchar_t* phase, double fraction)
    {
        if (!m_callback)
            return;
        Clock::time_point now = Clock::now();
        if (fraction < 1.0 && now - m_last < std::chrono::milliseconds(40))
            return;
        m_last = now;
        if (!m_callback((m_prefix + phase).c_str(), fraction))
            throw Cancelled();
    }

private:
    const std::function<bool(const wchar_t*, double)>& m_callback;
    std::wstring m_prefix;
    Clock::time_point m_last;
};

class Grid
{
public:
    Grid(const Vec3& lo, const Vec3& hi, size_t items)
    {
        double w = std::max(hi.x - lo.x, 0.0), h = std::max(hi.y - lo.y, 0.0);
        double cell = std::sqrt(std::max(w * h, 1e-18) / std::max<size_t>(items, 1)) * 1.5;
        cell = std::max({ cell, w / kMaxGridCells, h / kMaxGridCells, 1e-9 });
        m_lo = lo;
        m_cell = cell;
        m_nx = std::max(1, static_cast<int>(std::ceil(w / cell)));
        m_ny = std::max(1, static_cast<int>(std::ceil(h / cell)));
        m_cells.resize(static_cast<size_t>(m_nx) * m_ny);
    }

    int CellX(double x) const { return std::clamp(static_cast<int>(std::floor((x - m_lo.x) / m_cell)), 0, m_nx - 1); }
    int CellY(double y) const { return std::clamp(static_cast<int>(std::floor((y - m_lo.y) / m_cell)), 0, m_ny - 1); }

    std::vector<int>* Cell(int cx, int cy)
    {
        if (cx < 0 || cy < 0 || cx >= m_nx || cy >= m_ny)
            return nullptr;
        return &m_cells[static_cast<size_t>(cy) * m_nx + cx];
    }

    void Add(const Vec3& p, int id) { Cell(CellX(p.x), CellY(p.y))->push_back(id); }

    template <class F>
    bool Ring(int cx, int cy, int r, F&& f)
    {
        bool any = false;
        auto visit = [&](int x, int y) { // 람다함수, 바깥 변수 전부 참조로 씀
            if (std::vector<int>* cell = Cell(x, y))
            {
                any = true;
                f(*cell);
            }
        };
        if (r == 0)
        {
            visit(cx, cy);
            return any;
        }
        for (int x = cx - r; x <= cx + r; ++x)
        {
            visit(x, cy - r);
            visit(x, cy + r);
        }
        for (int y = cy - r + 1; y <= cy + r - 1; ++y)
        {
            visit(cx - r, y);
            visit(cx + r, y);
        }
        return any;
    }

    double RingBound(int r) const { return r <= 1 ? 0.0 : (r - 1) * m_cell; }

private:
    Vec3 m_lo;
    double m_cell = 1;
    int m_nx = 1, m_ny = 1;
    std::vector<std::vector<int>> m_cells;
};

class Step
{
public:
    int entity = 0;
    bool closed = false;
    bool reversed = false;
    int startVertex = 0;
    Vec3 entry, exit;
};

void SetEnds(Step& s, const DxfEntity& e)
{
    if (s.closed)
    {
        s.entry = s.exit = e.pts[s.startVertex];
        return;
    }
    s.entry = s.reversed ? e.pts.back() : e.pts.front();
    s.exit = s.reversed ? e.pts.front() : e.pts.back();
}

std::vector<Step> NearestNeighbor(const std::vector<DxfEntity>& entities, const std::vector<int>& ids,
                                  const TravelModel& cost, Vec3 pos, Progress& progress)
{
	const int n = static_cast<int>(ids.size()); // size_t(8 bytes) → int(4 bytes) 형변환, 경고 없애기 위함.
	std::vector<uint8_t> closed(n); 
    std::vector<Vec3> ptPos;
    std::vector<int> ptCand, ptVertex;
    Vec3 lo = entities[ids[0]].pts[0], hi = lo;
    auto addPoint = [&](const Vec3& p, int cand, int vertex) {
        ptPos.push_back(p); //점의 좌표
        ptCand.push_back(cand); //이 점이 속한 도형 번호
		ptVertex.push_back(vertex); // 열린 도형이면 0:시작점, 1:끝점, 닫힌 도형이면 꼭짓점 번호
		Extend(lo, hi, p); // 후보 점들의 최소/최대 좌표를 구함.
    };
    for (int i = 0; i < n; ++i)
    {
        const auto& pts = entities[ids[i]].pts;
        closed[i] = IsClosed(entities[ids[i]]); // bool→uint8_t 자동 형변환(C++기본 기능이라함) true->1, false->0
		if (closed[i]) // 닫힌 도형이면, 후보로 넣는 점의 개수가 달라짐.
        {
            for (size_t k = 0; k + 1 < pts.size(); ++k)
                addPoint(pts[k], i, static_cast<int>(k));
        }
        else // 열린 도형이면, 시작점, 끝점만 넣는다.
        {
            addPoint(pts.front(), i, 0);
            addPoint(pts.back(), i, 1);
        }
    }
    Grid grid(lo, hi, ptPos.size());
    for (int p = 0; p < static_cast<int>(ptPos.size()); ++p)
        grid.Add(ptPos[p], p);

    std::vector<uint8_t> used(n, 0);
    std::vector<Step> steps;
    for (int s = 0; s < n; ++s)
    {
        double best = std::numeric_limits<double>::max();
        int bestPoint = -1;
        int cx = grid.CellX(pos.x), cy = grid.CellY(pos.y);
        for (int r = 0;; ++r)
        {
            if (bestPoint >= 0 && grid.RingBound(r) >= best)
                break;
            bool any = grid.Ring(cx, cy, r, [&](std::vector<int>& cell) {
                cell.erase(std::remove_if(cell.begin(), cell.end(), [&](int id) { return used[ptCand[id]] != 0; }),
                           cell.end());
                for (int id : cell)
                {
                    double d = cost.Cost(pos, ptPos[id]);
                    if (d < best)
                    {
                        best = d;
                        bestPoint = id;
                    }
                }
            });
            if (!any)
                break;
        }

        int c = ptCand[bestPoint];
        Step step{ ids[c], closed[c] != 0, !closed[c] && ptVertex[bestPoint] == 1, closed[c] ? ptVertex[bestPoint] : 0 };
        used[c] = 1;
        SetEnds(step, entities[step.entity]);
        pos = step.exit;
        steps.push_back(step);
        if ((s % 256) == 0)
            progress(L"최근접 이웃", static_cast<double>(s) / n);
    }
    return steps;
}

DxfEntity Oriented(const DxfEntity& e, const Step& s)
{
    DxfEntity out = e;
    if (s.closed && s.startVertex > 0)
    {
        std::vector<Vec3> ring(e.pts.begin(), e.pts.end() - 1);
        std::rotate(ring.begin(), ring.begin() + s.startVertex, ring.end());
        ring.push_back(ring.front());
        out.pts = std::move(ring);
    }
    if (s.reversed)
        std::reverse(out.pts.begin(), out.pts.end());
    return out;
}

}

bool SamePoint(const Vec3& a, const Vec3& b)
{
    return Dist(a, b) <= 1e-6 * std::max(1.0, std::fabs(a.x) + std::fabs(a.y) + std::fabs(a.z));
}

double TravelModel::Cost(const Vec3& a, const Vec3& b) const
{
    if (!lift)
        return Dist(a, b);
    if (SamePoint(a, b))
        return 0;
    double top = std::max({ safeZ, a.z, b.z });
    return (top - a.z) + std::hypot(b.x - a.x, b.y - a.y) + (top - b.z);
}

CutOrderReport OptimizeCutOrder(DxfDrawing& dwg, const Vec3& start, const CutOrderOptions& options)
{
    CutOrderReport report;
    Progress progress(options.progress);

    std::map<int, std::vector<int>> groups;
    std::vector<int> hidden;
    for (int i = 0; i < static_cast<int>(dwg.entities.size()); ++i)
    {
        const DxfEntity& e = dwg.entities[i];
        if (e.pts.empty())
            continue;
        if (!IsVisible(e, dwg))
            hidden.push_back(i);
        else
            groups[options.layerRank ? (*options.layerRank)[dwg.FindLayer(e.layer)] : 0].push_back(i);
    }
    report.groups = static_cast<int>(groups.size());

    std::vector<DxfEntity> ordered;
    Vec3 pos = start;
    try
    {
        int number = 0;
        for (const auto& group : groups)
        {
            progress.SetGroup(++number, report.groups);
            std::vector<Step> steps = NearestNeighbor(dwg.entities, group.second, options.travel, pos, progress);
            for (const Step& s : steps)
                ordered.push_back(Oriented(dwg.entities[s.entity], s));
            if (!steps.empty())
                pos = steps.back().exit;
        }
    }
    catch (const Cancelled&)
    {
        report.cancelled = true;
        return report;
    }

    for (int id : hidden)
        ordered.push_back(dwg.entities[id]);
    dwg.entities = std::move(ordered);
    return report;
}
