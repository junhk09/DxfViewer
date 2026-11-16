#pragma once

#include <functional>

using ProgressCallback = std::function<bool(const wchar_t* text, double fraction)>;

bool RunWithProgress(CWnd* parent, const CString& title, const std::function<void(const ProgressCallback&)>& work);
