#include "pch.h"
#include "DxfViewer.h"
#include "DxfDoc.h"
#include "GLView_VBO.h"
#include "MainFrm.h"

CDxfViewerApp theApp;

BEGIN_MESSAGE_MAP(CDxfViewerApp, CWinApp)
    ON_COMMAND(ID_FILE_OPEN, &CWinApp::OnFileOpen)
END_MESSAGE_MAP()

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
