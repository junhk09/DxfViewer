#include "pch.h"
#include "DxfDoc.h"
#include "GLView_VBO.h"
#include "MainFrm.h"
#include "ProgressDlg.h"
#include "resource.h"

#include <algorithm>

IMPLEMENT_DYNCREATE(CDxfDoc, CDocument)

BEGIN_MESSAGE_MAP(CDxfDoc, CDocument)
    ON_COMMAND(ID_LAYER_ALL_ON, &CDxfDoc::OnLayerAllOn)
    ON_COMMAND(ID_LAYER_ALL_OFF, &CDxfDoc::OnLayerAllOff)
    ON_COMMAND(ID_PATH_OPTIMIZE, &CDxfDoc::OnPathOptimize)
    ON_COMMAND(ID_PATH_ORIGINAL, &CDxfDoc::OnPathOriginal)
    ON_UPDATE_COMMAND_UI(ID_PATH_OPTIMIZE, &CDxfDoc::OnUpdatePathOptimize)
    ON_UPDATE_COMMAND_UI(ID_PATH_ORIGINAL, &CDxfDoc::OnUpdatePathOriginal)
    ON_COMMAND(ID_VIEW_RESET, &CDxfDoc::OnViewReset)
    ON_UPDATE_COMMAND_UI(ID_VIEW_RESET, &CDxfDoc::OnUpdateViewReset)
END_MESSAGE_MAP()

BOOL CDxfDoc::OnNewDocument()
{
    return CDocument::OnNewDocument();
}

BOOL CDxfDoc::OnOpenDocument(LPCTSTR lpszPathName)
{
    DeleteContents();
    SetModifiedFlag(FALSE);

    CWaitCursor wait;
    std::wstring error;
    if (!LoadDxf(lpszPathName, m_dwg, error))
    {
        DeleteContents();
        AfxMessageBox(CString(L"DXF 파일을 읽지 못했습니다.\n\n") + error.c_str(), MB_ICONERROR);
        return FALSE;
    }
    m_fileOrder = m_dwg.entities;
    ResetProcessOrder();
    return TRUE;
}

double CDxfDoc::MmPerUnit(int insUnits)
{
    switch (insUnits)
    {
    case 1:  return 25.4;
    case 2:  return 304.8;
    case 3:  return 1609344.0;
    case 5:  return 10.0;
    case 6:  return 1000.0;
    case 7:  return 1e6;
    case 8:  return 25.4e-6;
    case 9:  return 0.0254;
    case 10: return 914.4;
    case 13: return 0.001;
    case 14: return 100.0;
    default: return 1.0;
    }
}

void CDxfDoc::DeleteContents()
{
    m_dwg.Clear();
    m_fileOrder.clear();
    m_layerRank.clear();
    m_cutOrderOptimized = false;
    m_travelModel = {};
    ClearSelection();
    CDocument::DeleteContents();
}

void CDxfDoc::SetAllLayers(bool visible)
{
    for (DxfLayer& layer : m_dwg.layers)
        layer.visible = visible;
    UpdateAllViews(nullptr, kHintVisibility);
}

void CDxfDoc::OnLayerAllOn() { SetAllLayers(true); }
void CDxfDoc::OnLayerAllOff() { SetAllLayers(false); }

void CDxfDoc::OnPathOptimize()
{
    CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
    if (frame && Is3DMode() && !frame->ReadLiftClearance(m_liftClearanceMm))
    {
        frame->ShowLiftClearance(m_liftClearanceMm);
        AfxMessageBox(L"Z 여유에는 0 이상의 숫자(mm)를 입력해 주세요.", MB_ICONWARNING);
        return;
    }
    if (frame)
        frame->ShowLiftClearance(m_liftClearanceMm);

    CutOrderOptions options;
    options.layerRank = &m_layerRank;
    if (Is3DMode())
    {
        double top = 0;
        bool any = false;
        for (const DxfEntity& e : m_fileOrder)
            if (e.layerIndex >= 0 && e.layerIndex < static_cast<int>(m_dwg.layers.size()) && m_dwg.layers[e.layerIndex].visible)
                for (const Vec3& p : e.pts)
                {
                    top = any ? std::max(top, p.z) : p.z;
                    any = true;
                }
        options.travel.lift = true;
        options.travel.safeZ = top + m_liftClearanceMm / MmPerUnit(m_dwg.insUnits);
    }

    DxfDrawing work;
    work.entities = m_fileOrder;
    work.layers = m_dwg.layers;
    CutOrderReport report;
    bool finished = RunWithProgress(AfxGetMainWnd(), L"경로 정렬", [&](const ProgressCallback& progress) {
        options.progress = progress;
        report = OptimizeCutOrder(work, PathStart(), options);
    });
    if (!finished || report.cancelled)
        return;

    m_dwg.entities = std::move(work.entities);
    m_travelModel = options.travel;
    m_cutOrderOptimized = true;
    UpdateAllViews(nullptr, kHintCutOrder);
}

void CDxfDoc::ResetProcessOrder()
{
    m_layerRank.assign(m_dwg.layers.size(), 0);
}

void CDxfDoc::MoveLayerRank(int layer, int delta)
{
    if (layer < 0 || layer >= static_cast<int>(m_layerRank.size()))
        return;
    int& rank = m_layerRank[layer];
    bool alone = std::count(m_layerRank.begin(), m_layerRank.end(), rank) == 1;
    if (alone)
        rank += delta;
    else
        rank = delta < 0 ? rank * 2 - 1 : rank * 2 + 1;
    if (!alone)
        for (int& other : m_layerRank)
            if (&other != &rank)
                other *= 2;
    NormalizeRanks();
}

void CDxfDoc::NormalizeRanks()
{
    std::vector<int> steps = m_layerRank;
    std::sort(steps.begin(), steps.end());
    steps.erase(std::unique(steps.begin(), steps.end()), steps.end());
    for (int& rank : m_layerRank)
        rank = static_cast<int>(std::lower_bound(steps.begin(), steps.end(), rank) - steps.begin());
}

void CDxfDoc::OnViewReset()
{
    m_dwg.entities = m_fileOrder;
    for (DxfLayer& layer : m_dwg.layers)
        layer.visible = !layer.off && !layer.frozen;
    m_cutOrderOptimized = false;
    m_travelModel = {};
    ClearSelection();
    UpdateAllViews(nullptr, kHintReload);
}

void CDxfDoc::OnUpdateViewReset(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!m_dwg.entities.empty());
}

bool CDxfDoc::Is3DMode() const
{
    POSITION pos = GetFirstViewPosition();
    while (pos)
        if (const CGLViewVBO* view = DYNAMIC_DOWNCAST(CGLViewVBO, GetNextView(pos)))
            return view->Is3D();
    return false;
}

void CDxfDoc::OnPathOriginal()
{
    m_dwg.entities = m_fileOrder;
    m_cutOrderOptimized = false;
    m_travelModel = {};
    UpdateAllViews(nullptr, kHintCutOrder);
}

void CDxfDoc::OnUpdatePathOptimize(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!m_dwg.entities.empty());
}

void CDxfDoc::OnUpdatePathOriginal(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(m_cutOrderOptimized);
}
