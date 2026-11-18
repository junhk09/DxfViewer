#include "pch.h"
#include "ProgressDlg.h"
#include "resource.h"

#include <atomic>
#include <mutex>
#include <thread>

// 이 파일 안에서만 쓰는 도우미 (이름 없는 namespace)
namespace {

// 작업 스레드 → 진행 창으로 보내는 메시지 (진행률 갱신 / 작업 끝)
const UINT WM_WORK_PROGRESS = WM_APP + 1;
const UINT WM_WORK_DONE = WM_APP + 2;
// 이 시간(ms) 안에 작업이 끝나면 진행 창을 띄우지 않음
const int kQuietMilliseconds = 300;

// 작업 스레드와 진행 창이 함께 보는 상태 (취소·끝·실패 여부, 진행 글자·비율)
class WorkState
{
public:
    std::atomic<bool> cancel{ false };
    std::atomic<bool> done{ false };
    std::atomic<bool> failed{ false };
    std::atomic<HWND> dialog{ nullptr };
    std::mutex lock;
    std::wstring text;
    double fraction = 0;
};

// 진행 창: 진행 막대와 취소 버튼
class CProgressDlg : public CDialog
{
public:
    CProgressDlg(CWnd* parent, const CString& title, WorkState& state)
        : CDialog(IDD_PROGRESS, parent), m_title(title), m_state(state) {}

protected:
    // 창이 뜰 때: 제목·막대 설정. 그새 작업이 끝났으면 바로 닫음
    BOOL OnInitDialog() override
    {
        CDialog::OnInitDialog();
        SetWindowText(m_title);
        m_bar.SubclassDlgItem(IDC_PROGRESS_BAR, this);
        m_bar.SetRange32(0, 1000);
        m_state.dialog = m_hWnd;
        if (m_state.done)
            EndDialog(IDOK);
        else
            OnProgress(0, 0);
        return TRUE;
    }

    // 취소 버튼: 취소 표시만 하고, 작업 스레드가 스스로 멈추길 기다림
    void OnCancel() override
    {
        m_state.cancel = true;
        GetDlgItem(IDCANCEL)->EnableWindow(FALSE);
        SetDlgItemText(IDC_PROGRESS_TEXT, L"취소하는 중...");
    }

    // 진행 메시지를 받으면 글자와 막대를 갱신
    afx_msg LRESULT OnProgress(WPARAM, LPARAM)
    {
        std::wstring text;
        double fraction;
        {
            std::lock_guard<std::mutex> guard(m_state.lock);
            text = m_state.text;
            fraction = m_state.fraction;
        }
        if (!m_state.cancel)
            SetDlgItemText(IDC_PROGRESS_TEXT, text.c_str());
        m_bar.SetPos(static_cast<int>(fraction * 1000));
        return 0;
    }

    // 작업 끝 메시지를 받으면 창을 닫음
    afx_msg LRESULT OnDone(WPARAM, LPARAM)
    {
        EndDialog(IDOK);
        return 0;
    }

    DECLARE_MESSAGE_MAP()

private:
    CString m_title;
    WorkState& m_state;
    CProgressCtrl m_bar;
};

// 진행 창 메시지 → 처리 함수 연결표
BEGIN_MESSAGE_MAP(CProgressDlg, CDialog)
    ON_MESSAGE(WM_WORK_PROGRESS, &CProgressDlg::OnProgress)
    ON_MESSAGE(WM_WORK_DONE, &CProgressDlg::OnDone)
END_MESSAGE_MAP()

}

// work를 작업 스레드에서 실행. 300ms 넘게 걸리면 진행 창을 띄우고 끝날 때까지 기다림.
// 성공하면 true, 취소나 오류면 false
bool RunWithProgress(CWnd* parent, const CString& title, const std::function<void(const ProgressCallback&)>& work)
{
    WorkState state;
    ProgressCallback report = [&state](const wchar_t* text, double fraction) {
        {
            std::lock_guard<std::mutex> guard(state.lock);
            state.text = text;
            state.fraction = fraction;
        }
        if (HWND dialog = state.dialog)
            ::PostMessage(dialog, WM_WORK_PROGRESS, 0, 0);
        return !state.cancel;
    };

    std::thread worker([&] {
        try
        {
            work(report);
        }
        catch (...)
        {
            state.failed = true;
        }
        state.done = true;
        if (HWND dialog = state.dialog)
            ::PostMessage(dialog, WM_WORK_DONE, 0, 0);
    });

    for (int waited = 0; waited < kQuietMilliseconds && !state.done; waited += 10)
        ::Sleep(10);
    if (!state.done)
    {
        CProgressDlg dialog(parent, title, state);
        dialog.DoModal();
    }
    worker.join();

    if (state.failed)
    {
        AfxMessageBox(title + L" 중 오류가 발생했습니다 (메모리 부족 등).", MB_ICONERROR);
        return false;
    }
    return !state.cancel;
}
