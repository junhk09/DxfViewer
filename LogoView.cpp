#include "pch.h"
#include "LogoView.h"
#include "resource.h"

#include <algorithm>

// MFC가 실행 중에 이 클래스 객체를 만들 수 있게 등록 (창 나누기에서 사용)
IMPLEMENT_DYNCREATE(CLogoView, CView)

// 윈도우 메시지 → 처리 함수 연결표
BEGIN_MESSAGE_MAP(CLogoView, CView)
    ON_WM_CREATE()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_MOUSEACTIVATE()
END_MESSAGE_MAP()

// 로고 비트맵을 불러오고 크기를 저장
int CLogoView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CView::OnCreate(lpCreateStruct) == -1)
        return -1;
    if (m_logo.LoadBitmap(IDB_LOGO))
    {
        BITMAP bm = {};
        m_logo.GetBitmap(&bm);
        m_logoSize = CSize(bm.bmWidth, bm.bmHeight);
    }
    return 0;
}

// 흰 배경에 로고를 비율을 지키며 가운데에 그림
void CLogoView::OnDraw(CDC* pDC)
{
    CRect rc;
    GetClientRect(&rc);
    pDC->FillSolidRect(rc, RGB(255, 255, 255));
    if (!m_logo.GetSafeHandle() || m_logoSize.cx <= 0)
        return;

    CRect area = rc;
    area.DeflateRect(ScaleDpi(8), ScaleDpi(8));
    if (area.Width() <= 0 || area.Height() <= 0)
        return;
    double scale = std::min(static_cast<double>(area.Width()) / m_logoSize.cx,
                            static_cast<double>(area.Height()) / m_logoSize.cy);
    int w = static_cast<int>(m_logoSize.cx * scale);
    int h = static_cast<int>(m_logoSize.cy * scale);
    int x = area.left + (area.Width() - w) / 2;
    int y = area.top + (area.Height() - h) / 2;

    CDC mem;
    mem.CreateCompatibleDC(pDC);
    CBitmap* old = mem.SelectObject(&m_logo);
    pDC->SetStretchBltMode(HALFTONE);
    ::SetBrushOrgEx(pDC->GetSafeHdc(), 0, 0, nullptr);
    pDC->StretchBlt(x, y, w, h, &mem, 0, 0, m_logoSize.cx, m_logoSize.cy, SRCCOPY);
    mem.SelectObject(old);
}

// 배경 지우기 생략 (OnDraw가 전부 칠함 → 깜빡임 방지)
BOOL CLogoView::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

// 크기가 바뀌면 다시 그리기
void CLogoView::OnSize(UINT nType, int cx, int cy)
{
    CView::OnSize(nType, cx, cy);
    Invalidate(FALSE);
}

// 로고를 클릭해도 활성 뷰가 바뀌지 않게 함
int CLogoView::OnMouseActivate(CWnd*, UINT, UINT)
{
    return MA_NOACTIVATE;
}
