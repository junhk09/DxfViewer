#include "pch.h"
#include "MainFrm.h"
#include "GLView.h"
#include "LeftView.h"
#include "LogoView.h"
#include "resource.h"

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_COMMAND(ID_VIEW_ZOOM_EXTENTS, &CMainFrame::OnZoomExtents)
    ON_COMMAND_RANGE(ID_VIEW_TOP, ID_VIEW_ISO, &CMainFrame::OnViewPreset)
    ON_COMMAND(ID_MODE_2D, &CMainFrame::OnMode2D)
    ON_COMMAND(ID_MODE_3D, &CMainFrame::OnMode3D)
    ON_UPDATE_COMMAND_UI(ID_MODE_2D, &CMainFrame::OnUpdateMode2D)
    ON_UPDATE_COMMAND_UI(ID_MODE_3D, &CMainFrame::OnUpdateMode3D)
    ON_UPDATE_COMMAND_UI(ID_MODE_HINT, &CMainFrame::OnUpdateModeHint)
    ON_COMMAND(ID_PATH_TRAVEL, &CMainFrame::OnPathTravel)
    ON_COMMAND(ID_PATH_SIMULATE, &CMainFrame::OnPathSimulate)
    ON_UPDATE_COMMAND_UI(ID_PATH_TRAVEL, &CMainFrame::OnUpdatePathTravel)
    ON_UPDATE_COMMAND_UI(ID_PATH_SIMULATE, &CMainFrame::OnUpdatePathSimulate)
    ON_UPDATE_COMMAND_UI(ID_LIFT_GAP, &CMainFrame::OnUpdateLiftGap)
    ON_UPDATE_COMMAND_UI(ID_LIFT_LABEL, &CMainFrame::OnUpdateLiftGap)
    ON_UPDATE_COMMAND_UI(ID_LIFT_UNIT, &CMainFrame::OnUpdateLiftGap)
END_MESSAGE_MAP()

static UINT indicators[] = { ID_INDICATOR_COORD };

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CFrameWnd::PreCreateWindow(cs))
        return FALSE;
    cs.cx = ScaleDpi(1280);
    cs.cy = ScaleDpi(800);
    return TRUE;
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
        return -1;
    if (!m_modeBar.Create(this, IDD_MODEBAR, CBRS_TOP | CBRS_TOOLTIPS, IDD_MODEBAR))
        return -1;
    if (!m_statusBar.Create(this) || !m_statusBar.SetIndicators(indicators, _countof(indicators)))
        return -1;
    m_statusBar.SetPaneInfo(0, ID_INDICATOR_COORD, SBPS_NORMAL, ScaleDpi(330));
    DragAcceptFiles(TRUE);
    return 0;
}

BOOL CMainFrame::OnCreateClient(LPCREATESTRUCT, CCreateContext* pContext)
{
    if (!m_splitter.CreateStatic(this, 1, 2))
        return FALSE;
    if (!m_leftSplitter.CreateStatic(&m_splitter, 2, 1, WS_CHILD | WS_VISIBLE, m_splitter.IdFromRowCol(0, 0)))
        return FALSE;
    if (!m_leftSplitter.CreateView(0, 0, RUNTIME_CLASS(CLogoView), CSize(0, ScaleDpi(64)), pContext)
        || !m_leftSplitter.CreateView(1, 0, RUNTIME_CLASS(CLeftView), CSize(0, 0), pContext)
        || !m_splitter.CreateView(0, 1, pContext->m_pNewViewClass, CSize(0, 0), pContext))
        return FALSE;
    m_splitter.SetColumnInfo(0, ScaleDpi(320), 0);
    SetActiveView(static_cast<CView*>(m_splitter.GetPane(0, 1)));
    return TRUE;
}

CGLView* CMainFrame::GetGLView()
{
    return DYNAMIC_DOWNCAST(CGLView, m_splitter.GetPane(0, 1));
}

void CMainFrame::OnZoomExtents()
{
    if (CGLView* view = GetGLView())
    {
        view->ZoomExtents();
        view->Invalidate(FALSE);
    }
}

void CMainFrame::OnViewPreset(UINT id)
{
    CGLView* view = GetGLView();
    if (!view)
        return;
    switch (id)
    {
    case ID_VIEW_TOP:   view->SetView(0, 0); break;
    case ID_VIEW_FRONT: view->SetView(0, -90); break;
    case ID_VIEW_RIGHT: view->SetView(-90, -90); break;
    case ID_VIEW_ISO:   view->SetView(CGLView::kIsoYaw, CGLView::kIsoPitch); break;
    }
}

void CMainFrame::OnMode2D()
{
    if (CGLView* view = GetGLView())
        view->SetMode3D(false);
}

void CMainFrame::OnMode3D()
{
    if (CGLView* view = GetGLView())
        view->SetMode3D(true);
}

void CMainFrame::OnUpdateMode2D(CCmdUI* pCmdUI)
{
    CGLView* view = GetGLView();
    pCmdUI->SetCheck(view && !view->Is3D());
}

void CMainFrame::OnUpdateMode3D(CCmdUI* pCmdUI)
{
    CGLView* view = GetGLView();
    pCmdUI->SetCheck(view && view->Is3D());
}

void CMainFrame::OnPathTravel()
{
    if (CGLView* view = GetGLView())
        view->ToggleTravel();
}

void CMainFrame::OnPathSimulate()
{
    if (CGLView* view = GetGLView())
        view->ToggleSimulation();
}

void CMainFrame::OnUpdatePathTravel(CCmdUI* pCmdUI)
{
    CGLView* view = GetGLView();
    pCmdUI->SetCheck(view && view->IsTravelShown());
}

void CMainFrame::OnUpdatePathSimulate(CCmdUI* pCmdUI)
{
    CGLView* view = GetGLView();
    pCmdUI->SetCheck(view && view->IsSimulating());
}

void CMainFrame::ShowModeBoxes(bool is3D)
{
    if (m_boxes3D == static_cast<int>(is3D))
        return;
    for (UINT id : { ID_LIFT_LABEL, ID_LIFT_GAP, ID_LIFT_UNIT })
        if (CWnd* control = m_modeBar.GetDlgItem(id))
            control->ShowWindow(is3D ? SW_SHOW : SW_HIDE);
    m_boxes3D = is3D ? 1 : 0;
}

void CMainFrame::OnUpdateModeHint(CCmdUI* pCmdUI)
{
    CGLView* view = GetGLView();
    ShowModeBoxes(view && view->Is3D());
    const wchar_t* text = view && view->Is3D()
        ? L"오른쪽 드래그: 회전  왼쪽: 이동  휠: 확대  F5~F8: 표준 뷰  Home: 처음 상태로  로그 더블클릭: 확대"
        : L"왼쪽 드래그: 이동   휠: 확대   Home: 처음 상태로   로그 더블클릭: 확대";
    pCmdUI->SetText(text);
}

void CMainFrame::OnUpdateLiftGap(CCmdUI* pCmdUI)
{
    CGLView* view = GetGLView();
    pCmdUI->Enable(view && view->Is3D());
}

void CMainFrame::ShowNumber(UINT id, double value)
{
    CString text;
    text.Format(L"%g", value);
    m_modeBar.SetDlgItemText(id, text);
}

bool CMainFrame::ReadNumber(UINT id, double& value)
{
    CString text;
    m_modeBar.GetDlgItemText(id, text);
    text.Trim();
    wchar_t* end = nullptr;
    double number = wcstod(text, &end);
    if (text.IsEmpty() || *end != L'\0' || !(number >= 0))
        return false;
    value = number;
    return true;
}

void CMainFrame::ShowLiftClearance(double mm) { ShowNumber(ID_LIFT_GAP, mm); }
bool CMainFrame::ReadLiftClearance(double& mm) { return ReadNumber(ID_LIFT_GAP, mm); }

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
    HWND lift = nullptr;
    if (m_modeBar.GetSafeHwnd())
        if (CWnd* box = m_modeBar.GetDlgItem(ID_LIFT_GAP))
            lift = box->GetSafeHwnd();
    if (pMsg->hwnd && pMsg->hwnd == lift
        && (pMsg->message == WM_KEYDOWN || pMsg->message == WM_CHAR))
    {
        if (pMsg->wParam == VK_RETURN)
        {
            if (pMsg->message == WM_KEYDOWN)
                PostMessage(WM_COMMAND, ID_PATH_OPTIMIZE);
            return TRUE;
        }
        if (pMsg->wParam != VK_F9)
        {
            ::TranslateMessage(pMsg);
            ::DispatchMessage(pMsg);
            return TRUE;
        }
    }
    return CFrameWnd::PreTranslateMessage(pMsg);
}

void CMainFrame::SetCoordText(const CString& text)
{
    if (m_statusBar.GetSafeHwnd())
        m_statusBar.SetPaneText(0, text);
}
