#pragma once

class CDxfDoc;

class CLeftView : public CView
{
protected:
    CLeftView() = default;
    DECLARE_DYNCREATE(CLeftView)

public:
    CDxfDoc* GetDocument() const;

protected:
    void OnDraw(CDC*) override {}
    void OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint) override;

    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnTabChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLayerItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLayerCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLayerColumnClick(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnRankUp();
    afx_msg void OnRankDown();
    afx_msg void OnRankSame();
    afx_msg void OnLogGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLogCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLogDoubleClick(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    void FillLayers();
    void SortLayers();
    int SelectedLayer() const;
    void ShowProcessOrder();
    void FillLog();
    void ShowLogSummary();
    void SelectLog(int logIndex);
    void ShowPage(int page);
    void Layout();

    CFont m_font;
    CTabCtrl m_tab;
    CListCtrl m_layers;
    CButton m_layersOn, m_layersOff;
    CStatic m_layerHint;
    CButton m_rankUp, m_rankDown, m_rankSame;
    CStatic m_rankHint;
    int m_sortColumn = -1;
    bool m_sortAscending = true;
    CListCtrl m_log;
    CStatic m_logInfo;
    bool m_filling = false;
};
