#include "pch.h"
#include "LogoView.h"
#include "resource.h"

#include <algorithm>

IMPLEMENT_DYNCREATE(CLogoView, CView)

BEGIN_MESSAGE_MAP(CLogoView, CView)
    ON_WM_CREATE()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_MOUSEACTIVATE()
END_MESSAGE_MAP()

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

BOOL CLogoView::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

void CLogoView::OnSize(UINT nType, int cx, int cy)
{
    CView::OnSize(nType, cx, cy);
    Invalidate(FALSE);
}

int CLogoView::OnMouseActivate(CWnd*, UINT, UINT)
{
    return MA_NOACTIVATE;
}
