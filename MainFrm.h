#pragma once

class CGLViewVBO;

class CMainFrame : public CFrameWnd
{
protected:
    CMainFrame() = default;
    DECLARE_DYNCREATE(CMainFrame)

public:
    void SetCoordText(const CString& text);

    void ShowLiftClearance(double mm);
    bool ReadLiftClearance(double& mm);

protected:
    BOOL PreCreateWindow(CREATESTRUCT& cs) override;
    BOOL PreTranslateMessage(MSG* pMsg) override;
    BOOL OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext) override;

    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnZoomExtents();
    afx_msg void OnViewPreset(UINT id);
    afx_msg void OnMode2D();
    afx_msg void OnMode3D();
    afx_msg void OnUpdateMode2D(CCmdUI* pCmdUI);
    afx_msg void OnUpdateMode3D(CCmdUI* pCmdUI);
    afx_msg void OnUpdateModeHint(CCmdUI* pCmdUI);
    afx_msg void OnPathTravel();
    afx_msg void OnPathSimulate();
    afx_msg void OnUpdatePathTravel(CCmdUI* pCmdUI);
    afx_msg void OnUpdatePathSimulate(CCmdUI* pCmdUI);
    afx_msg void OnUpdateLiftGap(CCmdUI* pCmdUI);
    DECLARE_MESSAGE_MAP()

private:
    CGLViewVBO* GetGLView();
    void ShowModeBoxes(bool is3D);
    void ShowNumber(UINT id, double value);
    bool ReadNumber(UINT id, double& value);

    int m_boxes3D = -1;

    CSplitterWnd m_splitter;
    CSplitterWnd m_leftSplitter;
    CDialogBar m_modeBar;
    CStatusBar m_statusBar;
};
