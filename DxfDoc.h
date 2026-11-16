#pragma once

#include "DxfParser.h"
#include "PathOptimizer.h"

enum UpdateHint : LPARAM
{
    kHintReload = 0,
    kHintVisibility = 1,
    kHintCutOrder = 2,
    kHintSelection = 3,
    kHintSelectionZoom = 4,
};

class CDxfDoc : public CDocument
{
protected:
    CDxfDoc() = default;
    DECLARE_DYNCREATE(CDxfDoc)

public:
    DxfDrawing m_dwg;
    std::vector<DxfEntity> m_fileOrder;
    bool m_cutOrderOptimized = false;
    TravelModel m_travelModel;
    static constexpr double kDefaultLiftMm = 2.0;
    double m_liftClearanceMm = kDefaultLiftMm;
    int m_selectedLog = -1;

    void ClearSelection()
    {
        m_selectedLog = -1;
    }

    std::vector<int> m_layerRank;

    static double MmPerUnit(int insUnits);

    void ResetProcessOrder();
    void MoveLayerRank(int layer, int delta);

    static Vec3 PathStart() { return {}; }

    static bool IsFromLog(const DxfEntity& e, int log)
    {
        return log >= 0 && e.sourceLog == log;
    }

    BOOL OnNewDocument() override;
    BOOL OnOpenDocument(LPCTSTR lpszPathName) override;
    void DeleteContents() override;

protected:
    afx_msg void OnLayerAllOn();
    afx_msg void OnLayerAllOff();
    afx_msg void OnPathOptimize();
    afx_msg void OnPathOriginal();
    afx_msg void OnUpdatePathOptimize(CCmdUI* pCmdUI);
    afx_msg void OnUpdatePathOriginal(CCmdUI* pCmdUI);
    afx_msg void OnViewReset();
    afx_msg void OnUpdateViewReset(CCmdUI* pCmdUI);
    DECLARE_MESSAGE_MAP()

private:
    void SetAllLayers(bool visible);
    bool Is3DMode() const;
    void NormalizeRanks();
};
