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

// "경로 정렬" 버튼 / 메뉴 / F9 / Z 여유 칸 Enter → ID_PATH_OPTIMIZE → 메시지 맵을 통해 여기로 옴
void CDxfDoc::OnPathOptimize()
{
    // ── 1. 3D 모드면 Z 여유 값 읽기 ──
    CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());   // 메인 창 (Z 여유 입력 칸이 있는 곳). 형변환 실패 시 nullptr
    if (frame && Is3DMode() && !frame->ReadLiftClearance(m_liftClearanceMm))   // 3D 보기인데 입력 칸이 비었거나, 숫자가 아니거나, 0보다 작으면
                                                                         //   (읽기에 실패하면 m_liftClearanceMm은 이전 값 그대로)
    {
        frame->ShowLiftClearance(m_liftClearanceMm);                     //   입력 칸을 마지막으로 올바르던 값으로 되돌려 놓고
        AfxMessageBox(L"Z 여유에는 0 이상의 숫자(mm)를 입력해 주세요.", MB_ICONWARNING);   //   경고 창
        return;                                                          //   정렬하지 않고 끝
    }
    if (frame)
        frame->ShowLiftClearance(m_liftClearanceMm);                     // 읽은 값을 정리된 형식으로 다시 표시 (예: " 2.50 " → "2.5")

    // ── 2. 정렬 옵션 준비 ──
    CutOrderOptions options;                                             // 기본값: 2D 모드 (lift = false, 직선거리 비용)
    options.layerRank = &m_layerRank;                                    // 레이어별 공정 번호 (같은 번호끼리 한 그룹으로 정렬)
    if (Is3DMode())                                                      // 3D 보기 상태면 "올라감 + 수평 + 내려감" 비용으로
    {
        double top = 0;                                                  // 보이는 도형 중 가장 높은 z
        bool any = false;                                                // 점을 하나라도 봤는지 (첫 점으로 top을 시작하려고)
        for (const DxfEntity& e : m_fileOrder)                           // 원본 순서의 모든 도형 중
            if (e.layerIndex >= 0 && e.layerIndex < static_cast<int>(m_dwg.layers.size()) && m_dwg.layers[e.layerIndex].visible)   // 레이어 번호가 올바르고 보이는 도형만
                for (const Vec3& p : e.pts)                              //   점마다
                {
                    top = any ? std::max(top, p.z) : p.z;                //   첫 점이면 그 z로 시작, 아니면 더 큰 쪽
                    any = true;
                }
        options.travel.lift = true;                                      // 공이동 = 위로 들었다가 → 옆으로 → 내려옴
        options.travel.safeZ = top + m_liftClearanceMm / MmPerUnit(m_dwg.insUnits);   // 들어 올릴 높이 = 가장 높은 z + Z 여유
                                                                         //   (Z 여유는 mm라서 도면 단위로 바꿔서 더함. 도면이 mm면 그대로)
    }

    // ── 3. 작업용 복사본 만들기 ──
    DxfDrawing work;                                                     // 정렬은 이 복사본에서 함 (취소되면 버리면 되도록)
    work.entities = m_fileOrder;                                         // 항상 파일 원래 순서에서 시작 (이전 정렬 결과 위에 또 정렬하지 않음)
    work.layers = m_dwg.layers;                                          // 레이어 정보 (보이는지, 공정 번호 찾기용 번호 순서)

    // ── 4. 진행 창을 띄우고 작업 스레드에서 정렬 ──
    CutOrderReport report;                                               // 정렬 결과 정보 (그룹 수, 취소됐는지)
    bool finished = RunWithProgress(AfxGetMainWnd(), L"경로 정렬", [&](const ProgressCallback& progress) {   // 300ms 넘게 걸리면 진행 창이 뜸
        options.progress = progress;                                     // 진행률을 알릴 함수를 정렬 옵션에 연결
        report = OptimizeCutOrder(work, PathStart(), options);           // ★ 실제 정렬. 원점 (0,0,0)에서 시작 → work.entities가 가공 순서로 바뀜
    });                                                                  // 정렬이 끝날 때까지 여기서 기다림 (그동안 진행 창이 화면 응답을 맡음)
    if (!finished || report.cancelled)                                   // 오류가 났거나 사용자가 취소를 눌렀으면
        return;                                                          //   실제 문서는 손대지 않고 끝 (이전 상태 그대로)

    // ── 5. 결과를 실제 문서에 반영 ──
    m_dwg.entities = std::move(work.entities);                           // 정렬된 도형 목록으로 교체 (복사 없이 통째로 넘김)
    m_travelModel = options.travel;                                      // 어떤 비용 방식으로 정렬했는지 기억 → 공이동 선·시뮬레이션을 같은 모양으로 그림
    m_cutOrderOptimized = true;                                          // "정렬된 상태" 표시 → "원래 순서로" 버튼이 켜짐

    // ── 6. 화면들에게 알림 ──
    UpdateAllViews(nullptr, kHintCutOrder);                              // 도면 화면: 정점·공이동 선 다시 만들고 다시 그림 / 왼쪽 패널: 레이어 목록 갱신
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
