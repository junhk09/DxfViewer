#include "pch.h"
#include "MainFrm.h"
#include "GLView_VBO.h"
#include "LeftView.h"
#include "LogoView.h"
#include "resource.h"

// MFC가 실행 중에 이 클래스 객체를 만들 수 있게 등록 (문서 템플릿이 사용)
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

// 툴바 버튼·메뉴 명령 → 처리 함수 연결표
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

// 상태 표시줄 칸 (좌표 표시 하나)
static UINT indicators[] = { ID_INDICATOR_COORD };

// 메인 창 처음 크기 (1280 x 800, 화면 배율 반영)
BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CFrameWnd::PreCreateWindow(cs))
        return FALSE;
    cs.cx = ScaleDpi(1280);
    cs.cy = ScaleDpi(800);
    return TRUE;
}

// 모드 툴바와 상태 표시줄을 만들고, 파일 끌어다 놓기를 허용
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

// 창 나누기: 왼쪽(위 로고 + 아래 왼쪽 패널) | 오른쪽(GL 도면 화면)
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

// 오른쪽 GL 뷰 가져오기
CGLViewVBO* CMainFrame::GetGLView()
{
    return DYNAMIC_DOWNCAST(CGLViewVBO, m_splitter.GetPane(0, 1));
}

// 전체 보기 버튼
void CMainFrame::OnZoomExtents()
{
    if (CGLViewVBO* view = GetGLView())
    {
        view->ZoomExtents();
        view->Invalidate(FALSE);
    }
}

// 표준 뷰 버튼 (위 · 앞 · 오른쪽 · 등각)
void CMainFrame::OnViewPreset(UINT id)
{
    CGLViewVBO* view = GetGLView();
    if (!view)
        return;
    switch (id)
    {
    case ID_VIEW_TOP:   view->SetView(0, 0); break;
    case ID_VIEW_FRONT: view->SetView(0, -90); break;
    case ID_VIEW_RIGHT: view->SetView(-90, -90); break;
    case ID_VIEW_ISO:   view->SetView(CGLViewVBO::kIsoYaw, CGLViewVBO::kIsoPitch); break;
    }
}

// 2D / 3D 모드 버튼
void CMainFrame::OnMode2D()
{
    if (CGLViewVBO* view = GetGLView())
        view->SetMode3D(false);
}

void CMainFrame::OnMode3D()
{
    if (CGLViewVBO* view = GetGLView())
        view->SetMode3D(true);
}

// 2D / 3D 버튼의 눌림 상태 표시
void CMainFrame::OnUpdateMode2D(CCmdUI* pCmdUI)
{
    CGLViewVBO* view = GetGLView();
    pCmdUI->SetCheck(view && !view->Is3D());
}

void CMainFrame::OnUpdateMode3D(CCmdUI* pCmdUI)
{
    CGLViewVBO* view = GetGLView();
    pCmdUI->SetCheck(view && view->Is3D());
}

// 공이동 선 보이기 버튼
void CMainFrame::OnPathTravel()
{
    if (CGLViewVBO* view = GetGLView())
        view->ToggleTravel();
}

// 시뮬레이션 버튼
void CMainFrame::OnPathSimulate()
{
    if (CGLViewVBO* view = GetGLView())
        view->ToggleSimulation();
}

// 공이동 / 시뮬레이션 버튼의 눌림 상태 표시
void CMainFrame::OnUpdatePathTravel(CCmdUI* pCmdUI)
{
    CGLViewVBO* view = GetGLView();
    pCmdUI->SetCheck(view && view->IsTravelShown());
}

void CMainFrame::OnUpdatePathSimulate(CCmdUI* pCmdUI)
{
    CGLViewVBO* view = GetGLView();
    pCmdUI->SetCheck(view && view->IsSimulating());
}

// 3D일 때만 Z 여유 입력 칸들을 보이게 함
void CMainFrame::ShowModeBoxes(bool is3D)
{
    if (m_boxes3D == static_cast<int>(is3D))
        return;
    for (UINT id : { ID_LIFT_LABEL, ID_LIFT_GAP, ID_LIFT_UNIT })
        if (CWnd* control = m_modeBar.GetDlgItem(id))
            control->ShowWindow(is3D ? SW_SHOW : SW_HIDE);
    m_boxes3D = is3D ? 1 : 0;
}

// 툴바 안내문을 2D / 3D에 맞게 바꾸고, Z 여유 칸 표시 여부를 갱신
void CMainFrame::OnUpdateModeHint(CCmdUI* pCmdUI)
{
    CGLViewVBO* view = GetGLView();
    ShowModeBoxes(view && view->Is3D());
    const wchar_t* text = view && view->Is3D()
        ? L"오른쪽 드래그: 회전  왼쪽: 이동  휠: 확대  F5~F8: 표준 뷰  Home: 처음 상태로  로그 더블클릭: 확대"
        : L"왼쪽 드래그: 이동   휠: 확대   Home: 처음 상태로   로그 더블클릭: 확대";
    pCmdUI->SetText(text);
}

// Z 여유 칸은 3D일 때만 입력 가능
void CMainFrame::OnUpdateLiftGap(CCmdUI* pCmdUI)
{
    CGLViewVBO* view = GetGLView();
    pCmdUI->Enable(view && view->Is3D());
}

// 툴바 입력 칸에 숫자 표시
void CMainFrame::ShowNumber(UINT id, double value)
{
    CString text;
    text.Format(L"%g", value);
    m_modeBar.SetDlgItemText(id, text);
}

// 툴바 입력 칸의 숫자 읽기 (비었거나, 숫자가 아니거나, 음수면 false)
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

// Z 여유(mm) 표시 / 읽기
void CMainFrame::ShowLiftClearance(double mm) { ShowNumber(ID_LIFT_GAP, mm); }
bool CMainFrame::ReadLiftClearance(double& mm) { return ReadNumber(ID_LIFT_GAP, mm); }

// Z 여유 칸에서 Enter = 경로 정렬. 다른 키는 단축키로 가로채지 않고 입력 칸이 직접 받게 함
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

// 상태 표시줄에 마우스 좌표 글자 표시
void CMainFrame::SetCoordText(const CString& text)
{
    if (m_statusBar.GetSafeHwnd())
        m_statusBar.SetPaneText(0, text);
}
