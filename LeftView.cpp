#include "pch.h"
#include "LeftView.h"
#include "DxfDoc.h"
#include "resource.h"

#include <algorithm>

// MFC가 실행 중에 이 클래스 객체를 만들 수 있게 등록 (창 나누기에서 사용)
IMPLEMENT_DYNCREATE(CLeftView, CView)

// 탭·레이어 목록·버튼·로그 목록에서 오는 알림 → 처리 함수 연결표
BEGIN_MESSAGE_MAP(CLeftView, CView)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_NOTIFY(TCN_SELCHANGE, IDC_LEFT_TAB, &CLeftView::OnTabChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LAYER_LIST, &CLeftView::OnLayerItemChanged)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_LAYER_LIST, &CLeftView::OnLayerCustomDraw)
    ON_NOTIFY(LVN_COLUMNCLICK, IDC_LAYER_LIST, &CLeftView::OnLayerColumnClick)
    ON_BN_CLICKED(IDC_RANK_UP, &CLeftView::OnRankUp)
    ON_BN_CLICKED(IDC_RANK_DOWN, &CLeftView::OnRankDown)
    ON_BN_CLICKED(IDC_RANK_SAME, &CLeftView::OnRankSame)
    ON_NOTIFY(LVN_GETDISPINFO, IDC_LOG_LIST, &CLeftView::OnLogGetDispInfo)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_LOG_LIST, &CLeftView::OnLogCustomDraw)
    ON_NOTIFY(NM_DBLCLK, IDC_LOG_LIST, &CLeftView::OnLogDoubleClick)   // 로그 목록(IDC_LOG_LIST)에서 더블클릭 알림(NM_DBLCLK)이 오면 OnLogDoubleClick 실행
END_MESSAGE_MAP()

// 이 파일 안에서만 쓰는 도우미 (이름 없는 namespace)
namespace {

// 탭 페이지, 레이어 목록 열, 로그 목록 열 번호
enum Page { kPageLayers, kPageLog };
enum LayerColumn { kLayerName, kLayerColor, kLayerCount, kLayerRank };
enum LogColumn { kLogNo, kLogSection, kLogType, kLogHandle, kLogLayer, kLogColor, kLogDetail, kLogColumns };

// 로그 목록 열 제목
const wchar_t* kLogHeaders[kLogColumns] = { L"번호", L"구역", L"타입", L"핸들", L"레이어", L"색상", L"내용" };

// 색 번호를 글자로 (256 = BYLAYER, 0 = BYBLOCK)
std::wstring ColorText(int aci)
{
    if (aci == 256)
        return L"BYLAYER";
    if (aci == 0)
        return L"BYBLOCK";
    return std::to_wstring(aci);
}

// 로그 row번의 column번 열에 보여 줄 글자
std::wstring LogCell(const std::vector<DxfLogEntry>& log, size_t row, int column)
{
    const DxfLogEntry& e = log[row];
    switch (column)
    {
    case kLogNo:      return std::to_wstring(row + 1);
    case kLogSection: return e.section;
    case kLogType:    return e.drawn ? e.type : e.type + L" (미표시)";
    case kLogHandle:  return e.handle;
    case kLogLayer:   return e.layer;
    case kLogColor:   return ColorText(e.color);
    case kLogDetail:  return e.detail;
    }
    return {};
}

}

// 이 뷰가 보여 주는 문서(CDxfDoc)
CDxfDoc* CLeftView::GetDocument() const
{
    return reinterpret_cast<CDxfDoc*>(m_pDocument);
}

// 탭, 레이어 목록과 버튼, 로그 필터·목록 등 모든 컨트롤을 만듦
int CLeftView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CView::OnCreate(lpCreateStruct) == -1)
        return -1;
    m_font.CreatePointFont(90, L"맑은 고딕");
    CRect none(0, 0, 0, 0);

    m_tab.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS, none, this, IDC_LEFT_TAB);
    m_tab.SetFont(&m_font);
    m_tab.InsertItem(kPageLayers, L"레이어");
    m_tab.InsertItem(kPageLog, L"엔티티 로그");

    m_layersOn.Create(L"전체 선택", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, none, this, ID_LAYER_ALL_ON);
    m_layersOn.SetFont(&m_font);
    m_layersOff.Create(L"전체 해제", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, none, this, ID_LAYER_ALL_OFF);
    m_layersOff.SetFont(&m_font);
    m_layerHint.Create(L"정렬: 파일 순서", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, none, this, IDC_LAYER_HINT);
    m_layerHint.SetFont(&m_font);

    m_rankUp.Create(L"▲", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, none, this, IDC_RANK_UP);
    m_rankDown.Create(L"▼", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, none, this, IDC_RANK_DOWN);
    m_rankSame.Create(L"하나로", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, none, this, IDC_RANK_SAME);
    for (CButton* button : { &m_rankUp, &m_rankDown, &m_rankSame })
        button->SetFont(&m_font);
    m_rankHint.Create(L"← 공정 번호", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, none, this, IDC_RANK_HINT);
    m_rankHint.SetFont(&m_font);

    m_layers.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                    none, this, IDC_LAYER_LIST);
    m_layers.SetFont(&m_font);
    m_layers.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    m_layers.InsertColumn(kLayerName, L"레이어", LVCFMT_LEFT, ScaleDpi(150));
    m_layers.InsertColumn(kLayerColor, L"색상", LVCFMT_CENTER, ScaleDpi(50));
    m_layers.InsertColumn(kLayerCount, L"개수", LVCFMT_RIGHT, ScaleDpi(58));
    m_layers.InsertColumn(kLayerRank, L"공정", LVCFMT_RIGHT, ScaleDpi(42));

    m_logInfo.Create(L"", WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, none, this, IDC_LOG_INFO);
    m_logInfo.SetFont(&m_font);

    m_log.Create(WS_CHILD | WS_BORDER | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                 none, this, IDC_LOG_LIST);
    m_log.SetFont(&m_font);
    m_log.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    const int widths[kLogColumns] = { 45, 110, 110, 50, 100, 65, 520 };
    for (int c = 0; c < kLogColumns; ++c)
        m_log.InsertColumn(c, kLogHeaders[c], c == kLogNo ? LVCFMT_RIGHT : LVCFMT_LEFT, ScaleDpi(widths[c]));

    m_tab.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return 0;
}

// 배경을 버튼 면 색으로 칠함
BOOL CLeftView::OnEraseBkgnd(CDC* pDC)
{
    CRect rc;
    GetClientRect(&rc);
    pDC->FillSolidRect(rc, ::GetSysColor(COLOR_BTNFACE));
    return TRUE;
}

// 크기가 바뀌면 컨트롤 배치를 다시 함
void CLeftView::OnSize(UINT nType, int cx, int cy)
{
    CView::OnSize(nType, cx, cy);
    Layout();
}

// 컨트롤 위치와 크기 배치
void CLeftView::Layout()
{
    if (!m_tab.GetSafeHwnd())
        return;
    CRect rc;
    GetClientRect(&rc);
    m_tab.MoveWindow(rc);
    CRect page = rc;
    m_tab.AdjustRect(FALSE, &page);

    const int bar = ScaleDpi(26), gap = ScaleDpi(3);

    const int layerButton = ScaleDpi(64);
    m_layersOn.MoveWindow(page.left + gap, page.top + gap, layerButton, bar - 2 * gap);
    m_layersOff.MoveWindow(page.left + 2 * gap + layerButton, page.top + gap, layerButton, bar - 2 * gap);
    int hintLeft = page.left + 4 * gap + 2 * layerButton;
    m_layerHint.MoveWindow(hintLeft, page.top, std::max(0, static_cast<int>(page.right) - gap - hintLeft), bar);
    {
        int y = page.top + bar, x = page.left + gap;
        const int arrow = ScaleDpi(28), word = ScaleDpi(56);
        m_rankUp.MoveWindow(x, y + gap, arrow, bar - 2 * gap);
        x += arrow + gap;
        m_rankDown.MoveWindow(x, y + gap, arrow, bar - 2 * gap);
        x += arrow + gap;
        m_rankSame.MoveWindow(x, y + gap, word, bar - 2 * gap);
        x += word + gap;
        m_rankHint.MoveWindow(x, y, std::max(0, static_cast<int>(page.right) - gap - x), bar);
    }
    m_layers.MoveWindow(page.left, page.top + 2 * bar, page.Width(), page.Height() - 2 * bar);

    int top = page.top;
    m_logInfo.MoveWindow(page.left + gap, top, page.Width() - 2 * gap, bar);
    top += bar;
    m_log.MoveWindow(page.left, top, page.Width(), page.bottom - top);
}

// 탭(레이어 / 엔티티 로그)을 바꾸면 그 페이지를 보여 줌
void CLeftView::OnTabChanged(NMHDR*, LRESULT* pResult)
{
    ShowPage(m_tab.GetCurSel());
    *pResult = 0;
}

// 선택한 페이지의 컨트롤만 보이고 나머지는 숨김
void CLeftView::ShowPage(int page)
{
    int layers = page == kPageLayers ? SW_SHOW : SW_HIDE;
    int log = page == kPageLog ? SW_SHOW : SW_HIDE;
    m_layers.ShowWindow(layers);
    m_layersOn.ShowWindow(layers);
    m_layersOff.ShowWindow(layers);
    m_layerHint.ShowWindow(layers);
    for (CButton* button : { &m_rankUp, &m_rankDown, &m_rankSame })
        button->ShowWindow(layers);
    m_rankHint.ShowWindow(layers);
    m_logInfo.ShowWindow(log);
    m_log.ShowWindow(log);
}

// 문서가 바뀌었을 때: 레이어 목록은 항상, 로그는 파일을 새로 열었을 때만 다시 채움 (자기가 보낸 알림은 무시)
void CLeftView::OnUpdate(CView* pSender, LPARAM lHint, CObject*)
{
    if (pSender == this)
        return;
    FillLayers();
    if (lHint == kHintReload)
        FillLog();
}

// 레이어 목록 채우기 (이름·색·개수·공정 번호·체크). 정렬하고 선택은 유지
void CLeftView::FillLayers()
{
    const CDxfDoc* doc = GetDocument();
    const DxfDrawing& dwg = doc->m_dwg;
    int selected = SelectedLayer();
    m_filling = true;
    m_layers.SetRedraw(FALSE);
    m_layers.DeleteAllItems();
    for (int i = 0; i < static_cast<int>(dwg.layers.size()); ++i)
    {
        const DxfLayer& layer = dwg.layers[i];
        int row = m_layers.InsertItem(i, layer.name.c_str());
        m_layers.SetItemText(row, kLayerColor, std::to_wstring(layer.color).c_str());
        m_layers.SetItemText(row, kLayerCount, std::to_wstring(layer.entityCount).c_str());
        if (i < static_cast<int>(doc->m_layerRank.size()))
            m_layers.SetItemText(row, kLayerRank, std::to_wstring(doc->m_layerRank[i] + 1).c_str());
        m_layers.SetItemData(row, static_cast<DWORD_PTR>(i));
        m_layers.SetCheck(row, layer.visible);
    }
    SortLayers();
    if (selected >= 0)
    {
        LVFINDINFO find = {};
        find.flags = LVFI_PARAM;
        find.lParam = selected;
        int row = m_layers.FindItem(&find);
        if (row >= 0)
        {
            m_layers.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            m_layers.EnsureVisible(row, FALSE);
        }
    }
    m_layers.SetRedraw(TRUE);
    m_filling = false;
}

// 레이어 목록에서 선택한 레이어 번호 (없으면 -1)
int CLeftView::SelectedLayer() const
{
    int row = m_layers.GetNextItem(-1, LVNI_SELECTED);
    return row >= 0 ? static_cast<int>(m_layers.GetItemData(row)) : -1;
}

// 공정 번호 순으로 정렬해 다시 표시
void CLeftView::ShowProcessOrder()
{
    m_sortColumn = kLayerRank;
    m_sortAscending = true;
    FillLayers();
}

// ▲ 버튼: 선택한 레이어를 공정 순서 앞으로
void CLeftView::OnRankUp()
{
    GetDocument()->MoveLayerRank(SelectedLayer(), -1);
    ShowProcessOrder();
}

// ▼ 버튼: 선택한 레이어를 공정 순서 뒤로
void CLeftView::OnRankDown()
{
    GetDocument()->MoveLayerRank(SelectedLayer(), +1);
    ShowProcessOrder();
}

// "하나로" 버튼: 모든 레이어를 같은 공정으로
void CLeftView::OnRankSame()
{
    GetDocument()->ResetProcessOrder();
    ShowProcessOrder();
}

// 열 제목 클릭: 그 열로 정렬 (오름차순 → 내림차순 → 파일 순서로 돌아감)
void CLeftView::OnLayerColumnClick(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    int column = reinterpret_cast<NMLISTVIEW*>(pNMHDR)->iSubItem;
    bool firstAscending = column != kLayerCount;
    if (column != m_sortColumn)
    {
        m_sortColumn = column;
        m_sortAscending = firstAscending;
    }
    else if (m_sortAscending == firstAscending)
    {
        m_sortAscending = !m_sortAscending;
    }
    else
    {
        m_sortColumn = -1;
    }
    FillLayers();
}

namespace {

// 레이어 정렬 기준 (CompareLayers에 넘김)
class LayerSort
{
public:
    const std::vector<DxfLayer>* layers;
    const std::vector<int>* ranks;
    int column;
    bool ascending;
};

// 두 레이어 비교 (SortItems 콜백). 같으면 원래 순서대로
int CALLBACK CompareLayers(LPARAM a, LPARAM b, LPARAM context)
{
    const auto* sort = reinterpret_cast<const LayerSort*>(context);
    const DxfLayer& la = (*sort->layers)[a];
    const DxfLayer& lb = (*sort->layers)[b];
    int r = 0;
    switch (sort->column)
    {
    case kLayerName:  r = _wcsicmp(la.name.c_str(), lb.name.c_str()); break;
    case kLayerColor: r = (la.color > lb.color) - (la.color < lb.color); break;
    case kLayerCount: r = (la.entityCount > lb.entityCount) - (la.entityCount < lb.entityCount); break;
    case kLayerRank:
        if (a < static_cast<LPARAM>(sort->ranks->size()) && b < static_cast<LPARAM>(sort->ranks->size()))
            r = ((*sort->ranks)[a] > (*sort->ranks)[b]) - ((*sort->ranks)[a] < (*sort->ranks)[b]);
        break;
    }
    if (!sort->ascending)
        r = -r;
    return r != 0 ? r : static_cast<int>(a - b);
}

}

// 레이어 목록을 정렬하고, 열 제목 화살표와 "정렬: …" 안내를 갱신
void CLeftView::SortLayers()
{
    if (m_sortColumn >= 0)
    {
        LayerSort sort{ &GetDocument()->m_dwg.layers, &GetDocument()->m_layerRank, m_sortColumn, m_sortAscending };
        m_layers.SortItems(CompareLayers, reinterpret_cast<DWORD_PTR>(&sort));
    }

    CHeaderCtrl* header = m_layers.GetHeaderCtrl();
    for (int c = 0; c < header->GetItemCount(); ++c)
    {
        HDITEM item = {};
        item.mask = HDI_FORMAT;
        header->GetItem(c, &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (c == m_sortColumn)
            item.fmt |= m_sortAscending ? HDF_SORTUP : HDF_SORTDOWN;
        header->SetItem(c, &item);
    }

    const wchar_t* state = L"파일 순서";
    switch (m_sortColumn)
    {
    case kLayerName:  state = m_sortAscending ? L"이름순" : L"이름 역순"; break;
    case kLayerColor: state = m_sortAscending ? L"색상 번호 작은 순" : L"색상 번호 큰 순"; break;
    case kLayerCount: state = m_sortAscending ? L"개수 적은 순" : L"개수 많은 순"; break;
    case kLayerRank:  state = m_sortAscending ? L"공정 순서" : L"공정 역순"; break;
    }
    m_layerHint.SetWindowText(CString(L"정렬: ") + state);
}

// 레이어 체크박스를 바꾸면 그 레이어를 켜거나 끄고 다른 뷰에 알림
void CLeftView::OnLayerItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    auto* nm = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
    if (m_filling || nm->iItem < 0 || !(nm->uChanged & LVIF_STATE))
        return;
    if (((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK) == 0)
        return;

    CDxfDoc* doc = GetDocument();
    size_t index = static_cast<size_t>(m_layers.GetItemData(nm->iItem));
    if (index >= doc->m_dwg.layers.size())
        return;
    doc->m_dwg.layers[index].visible = ((nm->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
    doc->UpdateAllViews(this, kHintVisibility);
}

// 레이어 목록의 색 칸을 실제 색으로 칠함
void CLeftView::OnLayerCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;
    switch (cd->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        *pResult = CDRF_NOTIFYITEMDRAW;
        break;
    case CDDS_ITEMPREPAINT:
        *pResult = CDRF_NOTIFYSUBITEMDRAW;
        break;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        if (cd->iSubItem == kLayerColor)
        {
            const auto& layers = GetDocument()->m_dwg.layers;
            size_t index = static_cast<size_t>(cd->nmcd.lItemlParam);
            unsigned int rgb = index < layers.size() ? AciToRgb(layers[index].color) : 0xFFFFFF;
            int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
            cd->clrTextBk = RGB(r, g, b);
            cd->clrText = (r * 299 + g * 587 + b * 114) / 1000 > 128 ? RGB(0, 0, 0) : RGB(255, 255, 255);
        }
        else
        {
            cd->clrTextBk = ::GetSysColor(COLOR_WINDOW);
            cd->clrText = ::GetSysColor(COLOR_WINDOWTEXT);
        }
        break;
    }
}

// 로그 목록을 다시 채움: 줄 수 = 로그 개수 (화면 줄 번호 = 로그 번호, 파일 순서 그대로)
void CLeftView::FillLog()
{
    const auto& log = GetDocument()->m_dwg.log;                     // 파서가 만든 로그 목록
    m_log.SetItemCountEx(static_cast<int>(log.size()), LVSICF_NOSCROLL);   // 가상 목록에 "줄이 N개"라고만 알림 (글자는 OnLogGetDispInfo가 채움)
    m_log.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);        // 이전 선택 표시 지우기
    m_log.Invalidate(FALSE);                                        // 목록 다시 그리기
    ShowLogSummary();                                               // 위쪽 안내문 갱신
}

// 로그 목록 위 안내문 ("N개 (그림 M · 회색 K: 도면에 없음)")
void CLeftView::ShowLogSummary()
{
    const auto& log = GetDocument()->m_dwg.log;
    CString info;
    if (log.empty())
        info = L"엔티티 레코드 없음";
    else
    {
        int total = static_cast<int>(log.size());
        int drawn = static_cast<int>(std::count_if(log.begin(), log.end(), [](const DxfLogEntry& e) { return e.drawn; }));
        info.Format(L"%d개  (그림 %d · 회색 %d: 도면에 없음)", total, drawn, total - drawn);
    }
    m_logInfo.SetWindowText(info);
}

// 가상 목록이 칸 글자를 물어보면 채워 줌 (화면에 보이는 줄만)
void CLeftView::OnLogGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    auto* di = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
    const auto& log = GetDocument()->m_dwg.log;
    int index = di->item.iItem;                                     // 화면 줄 번호 = 로그 번호
    if (!(di->item.mask & LVIF_TEXT) || index < 0 || index >= static_cast<int>(log.size()))
        return;
    std::wstring text = LogCell(log, index, di->item.iSubItem);
    wcsncpy_s(di->item.pszText, di->item.cchTextMax, text.c_str(), _TRUNCATE);
}

// 로그 더블클릭: 그 줄의 로그 번호로 SelectLog
void CLeftView::OnLogDoubleClick(NMHDR* pNMHDR, LRESULT* pResult) // 로그 더블클릭하면 선택
{
    *pResult = 0;                                                   // 알림 처리 결과 (0 = 기본 처리)
    int index = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR)->iItem;   // 알림 헤더를 더블클릭 알림 형식으로 바꿔 줄 번호(iItem)를 꺼냄. 줄 번호 = 로그 번호
    if (index >= 0)                                                 // 실제 로그 줄을 눌렀으면 (빈 곳이면 -1)
        SelectLog(index);                                           //   그 로그를 선택
}

// 로그 선택: 문서에 m_selectedLog 저장 → 다른 뷰에 알림(강조) → 안내문 갱신
void CLeftView::SelectLog(int logIndex)
{
    CDxfDoc* doc = GetDocument();                                   // 이 뷰가 보여 주는 문서
    const DxfDrawing& dwg = doc->m_dwg;                             // 문서의 도면 (로그·도형이 들어 있음). 참조라 복사 안 함
    doc->ClearSelection();                                          // 이전 선택 지우기 (m_selectedLog = -1)
    doc->m_selectedLog = logIndex;                                  // 새로 고른 로그 번호를 문서에 저장 → GL 뷰가 이 값을 읽음
    doc->UpdateAllViews(this, kHintSelection);                      // 문서에 붙은 뷰들 중 나(this)를 뺀 모두의 OnUpdate를 "선택 바뀜"으로 호출
                                                                    //   → CGLViewVBO::OnUpdate가 강조·확대를 하고 돌아옴

    if (logIndex < 0 || logIndex >= static_cast<int>(dwg.log.size()))   // 로그 번호가 범위 밖이면 (선택 해제 등)
    {
        ShowLogSummary();                                           //   안내문을 기본 문구("N개 …")로 되돌리고
        return;                                                     //   끝
    }
    int count = static_cast<int>(std::count_if(dwg.entities.begin(), dwg.entities.end(),   // 모든 도형 중에서
        [logIndex](const DxfEntity& e) { return CDxfDoc::IsFromLog(e, logIndex); }));     //   이 로그에서 나온 도형(sourceLog == logIndex)의 개수
    const DxfLogEntry& e = dwg.log[logIndex];                       // 선택한 로그 한 줄 (타입 이름을 쓰려고)
    CString info;                                                   // 안내문 글자
    if (count > 0)                                                  // 강조된 도형이 있으면
        info.Format(L"#%d %s: 도면 요소 %d개 강조", logIndex + 1, e.type.c_str(), count);   //   예: "#3 CIRCLE: 도면 요소 1개 강조" (번호는 1부터)
    else                                                            // 그려지지 않은 레코드면 (TEXT, INSERT 등)
        info.Format(L"#%d %s: 도면에 그려지지 않는 레코드", logIndex + 1, e.type.c_str());
    m_logInfo.SetWindowText(info);                                  // 로그 목록 위 안내문에 표시
}

// 그려지지 않은 로그는 회색 글자로
void CLeftView::OnLogCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;
    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
    {
        *pResult = CDRF_NOTIFYITEMDRAW;
    }
    else if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
    {
        const auto& log = GetDocument()->m_dwg.log;
        int index = static_cast<int>(cd->nmcd.dwItemSpec);          // 그리려는 줄 번호 = 로그 번호
        if (index >= 0 && index < static_cast<int>(log.size()) && !log[index].drawn)
            cd->clrText = RGB(140, 140, 140);
    }
}
