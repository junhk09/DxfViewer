#pragma once

#include "resource.h"

class CDxfViewerApp : public CWinApp
{
public:
    BOOL InitInstance() override;

    DECLARE_MESSAGE_MAP()
};

extern CDxfViewerApp theApp;
