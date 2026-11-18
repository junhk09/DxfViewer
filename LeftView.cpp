#include "pch.h"
#include "LeftView.h"
#include "DxfDoc.h"
#include "resource.h"

#include <algorithm>
#include <fstream>

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
    ON_NOTIFY(NM_DBLCLK, IDC_LOG_LIST, &CLeftView::OnLogDoubleClick)
    ON_NOTIFY(TCN_SELCHANGE, IDC_LOG_FILTER, &CLeftView::OnLogFilterChanged)
    ON_BN_CLICKED(IDC_LOG_SAVE, &CLeftView::OnSaveLog)
END_MESSAGE_MAP()

// 이 파일 안에서만 쓰는 도우미 (이름 없는 namespace)
namespace {

// 탭 페이지, 로그 필터, 레이어 목록 열, 로그 목록 열 번호
enum Page { kPageLayers, kPageLog };
enum LogFilter { kFilterAll, kFilterDrawn, kFilterHidden };
enum LayerColumn { kLayerName, kLayerColor, kLayerCount, kLayerRank };
enum LogColumn { kLogNo, kLogSection, kLogType, kLogHandle, kLogLayer, kLogColor, kLogDetail, kLogColumns };

// 로그 목록 열 제목 (CSV 첫 줄에도 씀)
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

// 유니코드 문자열을 UTF-8 바이트로 (CSV 저장용)
std::string ToUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// CSV 칸 하나: 쉼표·따옴표·줄바꿈이 있으면 따옴표로 감쌈
std::wstring CsvField(const std::wstring& s)
{
    if (s.find_first_of(L",\"\r\n") == std::wstring::npos)
        return s;
    std::wstring out = L"\"";
    for (wchar_t c : s)
    {
        if (c == L'"')
            out += L'"';
        out += c;
    }
    return out + L"\"";
}

}

// 이 뷰가 보여 주는 문서(CDxfDoc)
CDxfDoc* CLeftView::GetDocument() const
{
    return reinterpret_cast<CDxfDoc*>(m_pDocument);
}

// 탭, 레이어 목록과 버튼, 로그 필터·목록·저장 버튼 등 모든 컨트롤을 만듦
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
    m_logSave.Create(L"CSV로 저장...", WS_CHILD | BS_PUSHBUTTON, none, this, IDC_LOG_SAVE);
    m_logSave.SetFont(&m_font);
    m_logFilter.Create(WS_CHILD | TCS_BUTTONS | TCS_FLATBUTTONS | TCS_FOCUSNEVER, none, this, IDC_LOG_FILTER);
    m_logFilter.SetFont(&m_font);
    m_logFilter.InsertItem(kFilterAll, L"전체");
    m_logFilter.InsertItem(kFilterDrawn, L"표시");
    m_logFilter.InsertItem(kFilterHidden, L"미표시");
    m_logFilter.SetCurSel(kFilterAll);

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

    const int bar = ScaleDpi(26), gap = ScaleDpi(3), buttonWidth = ScaleDpi(96);

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

    m_logFilter.MoveWindow(page.left + gap, page.top + gap, page.Width() - 2 * gap, bar - gap);
    int top = page.top + bar;
    m_logSave.MoveWindow(page.right - buttonWidth, top + gap, buttonWidth, bar - 2 * gap);
    m_logInfo.MoveWindow(page.left + gap, top, page.Width() - buttonWidth - 3 * gap, bar);
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
    m_logFilter.ShowWindow(log);
    m_logInfo.ShowWindow(log);
    m_logSave.ShowWindow(log);
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

// 로그 필터 탭의 "전체 / 표시 / 미표시" 개수를 갱신하고 목록을 다시 채움
void CLeftView::FillLog()
{
    const auto& log = GetDocument()->m_dwg.log;
    int drawn = static_cast<int>(std::count_if(log.begin(), log.end(), [](const DxfLogEntry& e) { return e.drawn; }));
    const int counts[] = { static_cast<int>(log.size()), drawn, static_cast<int>(log.size()) - drawn };
    const wchar_t* names[] = { L"전체", L"표시", L"미표시" };
    for (int f = kFilterAll; f <= kFilterHidden; ++f)
    {
        CString label;
        label.Format(L"%s %d", names[f], counts[f]);
        TCITEM item = {};
        item.mask = TCIF_TEXT;
        item.pszText = label.GetBuffer();
        m_logFilter.SetItem(f, &item);
        label.ReleaseBuffer();
    }
    ApplyLogFilter();
}

// 필터에 맞는 로그 번호를 m_logRows에 모으고 목록 줄 수를 갱신 (선택한 로그는 유지)
void CLeftView::ApplyLogFilter()
{
    const auto& log = GetDocument()->m_dwg.log;
    int filter = std::max(0, m_logFilter.GetCurSel());
    m_logRows.clear();
    for (int i = 0; i < static_cast<int>(log.size()); ++i)
    {
        if (filter == kFilterAll || (filter == kFilterDrawn) == log[i].drawn)
            m_logRows.push_back(i);
    }

    m_filling = true;
    m_log.SetItemCountEx(static_cast<int>(m_logRows.size()), LVSICF_NOSCROLL);
    m_log.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    auto shown = std::find(m_logRows.begin(), m_logRows.end(), GetDocument()->m_selectedLog);
    if (shown != m_logRows.end())
    {
        int row = static_cast<int>(shown - m_logRows.begin());
        m_log.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        m_log.EnsureVisible(row, FALSE);
    }
    m_log.Invalidate(FALSE);
    m_filling = false;
    ShowLogSummary();
}

// 필터 탭을 바꾸면 다시 거름
void CLeftView::OnLogFilterChanged(NMHDR*, LRESULT* pResult)
{
    *pResult = 0;
    ApplyLogFilter();
}

// 로그 목록 위 안내문 ("N개 (회색: 도면에 없음)")
void CLeftView::ShowLogSummary()
{
    const auto& log = GetDocument()->m_dwg.log;
    CString info;
    if (log.empty())
        info = L"엔티티 레코드 없음";
    else
        info.Format(L"%d개  (회색: 도면에 없음)", static_cast<int>(m_logRows.size()));
    m_logInfo.SetWindowText(info);
    m_logSave.EnableWindow(!m_logRows.empty());
}

// 화면 줄 번호 → 로그 번호 (m_logRows). 범위 밖이면 -1
int CLeftView::LogIndexOfRow(int row) const
{
    return row >= 0 && row < static_cast<int>(m_logRows.size()) ? m_logRows[row] : -1;
}

// 가상 목록이 칸 글자를 물어보면 채워 줌 (화면에 보이는 줄만)
void CLeftView::OnLogGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    auto* di = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
    int index = LogIndexOfRow(di->item.iItem);
    if (!(di->item.mask & LVIF_TEXT) || index < 0)
        return;
    std::wstring text = LogCell(GetDocument()->m_dwg.log, index, di->item.iSubItem);
    wcsncpy_s(di->item.pszText, di->item.cchTextMax, text.c_str(), _TRUNCATE);
}

// 로그 더블클릭: 그 줄의 로그 번호로 SelectLog
void CLeftView::OnLogDoubleClick(NMHDR* pNMHDR, LRESULT* pResult) // 로그 더블클릭하면 선택
{
    *pResult = 0;
    int index = LogIndexOfRow(reinterpret_cast<NMITEMACTIVATE*>(pNMHDR)->iItem);
    if (index >= 0)
        SelectLog(index);
}

// 로그 선택: 문서에 m_selectedLog 저장 → 다른 뷰에 알림(강조) → 안내문 갱신
void CLeftView::SelectLog(int logIndex)
{
    CDxfDoc* doc = GetDocument();
    const DxfDrawing& dwg = doc->m_dwg;
    doc->ClearSelection();
    doc->m_selectedLog = logIndex;
    doc->UpdateAllViews(this, kHintSelection);

    if (logIndex < 0 || logIndex >= static_cast<int>(dwg.log.size()))
    {
        ShowLogSummary();
        return;
    }
    int count = static_cast<int>(std::count_if(dwg.entities.begin(), dwg.entities.end(),
        [logIndex](const DxfEntity& e) { return CDxfDoc::IsFromLog(e, logIndex); }));
    const DxfLogEntry& e = dwg.log[logIndex];
    CString info;
    if (count > 0)
        info.Format(L"#%d %s: 도면 요소 %d개 강조", logIndex + 1, e.type.c_str(), count);
    else
        info.Format(L"#%d %s: 도면에 그려지지 않는 레코드", logIndex + 1, e.type.c_str());
    m_logInfo.SetWindowText(info);
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
        int index = LogIndexOfRow(static_cast<int>(cd->nmcd.dwItemSpec));
        if (index >= 0 && !GetDocument()->m_dwg.log[index].drawn)
            cd->clrText = RGB(140, 140, 140);
    }
}

// 지금 보이는(필터된) 로그를 CSV 파일로 저장 (UTF-8)
void CLeftView::OnSaveLog()
{
    CDxfDoc* doc = GetDocument();
    const auto& log = doc->m_dwg.log;
    if (m_logRows.empty())
        return;

    CString name = doc->GetTitle();
    int dot = name.ReverseFind(L'.');
    if (dot > 0)
        name = name.Left(dot);
    CFileDialog dlg(FALSE, L"csv", name + L"_entities.csv", OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST,
                    L"CSV 파일 (*.csv)|*.csv|모든 파일 (*.*)|*.*||", this);
    if (dlg.DoModal() != IDOK)
        return;

    std::ofstream file(dlg.GetPathName().GetString(), std::ios::binary);
    if (!file)
    {
        AfxMessageBox(L"파일을 만들 수 없습니다.", MB_ICONERROR);
        return;
    }
    file << "\xEF\xBB\xBF";
    std::wstring line;
    for (int c = 0; c < kLogColumns; ++c)
        line += (c ? L"," : L"") + CsvField(kLogHeaders[c]);
    file << ToUtf8(line) << "\r\n";
    for (int index : m_logRows)
    {
        line.clear();
        for (int c = 0; c < kLogColumns; ++c)
            line += (c ? L"," : L"") + CsvField(LogCell(log, index, c));
        file << ToUtf8(line) << "\r\n";
    }
    CString done;
    done.Format(L"엔티티 로그 %d줄을 저장했습니다.\n%s", static_cast<int>(m_logRows.size()), dlg.GetPathName().GetString());
    AfxMessageBox(done, MB_ICONINFORMATION);
}
