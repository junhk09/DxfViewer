#include "pch.h"
#include "DxfViewer.h"
#include "DxfDoc.h"
#include "GLView_VBO.h"
#include "MainFrm.h"

// 프로그램 전체에 하나뿐인 앱 객체 (MFC가 이 객체로 프로그램을 시작함)
CDxfViewerApp theApp;

// 앱 명령 연결표: "파일 열기"는 MFC 기본 동작을 그대로 씀
BEGIN_MESSAGE_MAP(CDxfViewerApp, CWinApp)
    ON_COMMAND(ID_FILE_OPEN, &CWinApp::OnFileOpen)
END_MESSAGE_MAP()

// 프로그램 시작: 공용 컨트롤 준비 → 문서·프레임·뷰 묶음(템플릿) 등록 → 명령줄 처리 → 창 표시
BOOL CDxfViewerApp::InitInstance()
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    CWinApp::InitInstance();

    AddDocTemplate(new CSingleDocTemplate(IDR_MAINFRAME,
        RUNTIME_CLASS(CDxfDoc), RUNTIME_CLASS(CMainFrame), RUNTIME_CLASS(CGLViewVBO)));

    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo))
        return FALSE;

    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
}
