#pragma once

#include "DxfParser.h"
#include "PathOptimizer.h"

enum UpdateHint : LPARAM
{
    kHintReload = 0,
    kHintVisibility = 1,
    kHintCutOrder = 2,
    kHintSelection = 3,      // 로그를 더블클릭함: 그 로그의 도형을 강조하고 확대
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
    int m_selectedLog = -1;                 // 로그 창에서 더블클릭한 로그 번호 (-1 = 선택 없음). 왼쪽 뷰가 쓰고 GL 뷰가 읽음

    void ClearSelection()                   // 선택 해제
    {
        m_selectedLog = -1;                 // -1이면 IsFromLog가 항상 false → 강조할 도형 없음
    }

    std::vector<int> m_layerRank;

    static double MmPerUnit(int insUnits);

    void ResetProcessOrder();
    void MoveLayerRank(int layer, int delta);

    static Vec3 PathStart() { return {}; }

    static bool IsFromLog(const DxfEntity& e, int log)   // 도형 e가 로그 log번에서 나온 도형인지
    {
        return log >= 0 && e.sourceLog == log;           // 선택이 있고(-1 아님), 파서가 적어 둔 sourceLog가 그 번호와 같으면 true
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
