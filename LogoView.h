#pragma once

class CLogoView : public CView
{
protected:
    CLogoView() = default;
    DECLARE_DYNCREATE(CLogoView)

protected:
    void OnDraw(CDC* pDC) override;

    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
    DECLARE_MESSAGE_MAP()

private:
    CBitmap m_logo;
    CSize m_logoSize;
};
