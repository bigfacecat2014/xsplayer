
#include "stdafx.h"
#include "MM.h"

//-------------------------------------------------------------------------------------------------
// CMonitor的实现
//-------------------------------------------------------------------------------------------------
CMonitor::CMonitor() : m_hMonitor(nullptr)
{
}

CMonitor::CMonitor(const CMonitor& monitor)
{
    Attach((HMONITOR)monitor);
}

void CMonitor::Attach(const HMONITOR hMonitor)
{
    m_hMonitor = hMonitor;
    m_infoEx.cbSize = sizeof(m_infoEx);
    ::GetMonitorInfo(m_hMonitor, &m_infoEx);

}

HMONITOR CMonitor::Detach()
{
    HMONITOR hMonitor = m_hMonitor;
    m_hMonitor = nullptr;
    return hMonitor;
}

HDC CMonitor::CreateDC() const
{
    PCWSTR name = GetName();

    // create a dc for this display
    HDC hdc = ::CreateDC(name, name, nullptr, nullptr);

    // set the viewport based on the monitor rect's relation to the primary monitor
    RECT rect;
    GetMonitorRect(&rect);

    ::SetViewportOrgEx(hdc, -rect.left, -rect.top, nullptr);
    ::SetViewportExtEx(hdc, rect.right - rect.left, rect.bottom - rect.top, nullptr);

    return hdc;
}

int CMonitor::GetBitsPerPixel() const
{
    HDC hdc = CreateDC();
    int ret = ::GetDeviceCaps(hdc, BITSPIXEL) * ::GetDeviceCaps(hdc, PLANES);
    ::DeleteDC(hdc);

    return ret;
}

PCWSTR CMonitor::GetName() const
{
    return m_infoEx.szDevice;
}

// these methods return true if any part of the item intersects the monitor rect
BOOL CMonitor::IsOnMonitor(const POINT& pt) const
{
    RECT rect;
    GetMonitorRect(&rect);
    return ::PtInRect(&rect, pt);
}

BOOL CMonitor::IsOnMonitor(const HWND hwnd) const
{
    RECT rect;
    GetMonitorRect(&rect);

    RECT wndRect;
    ::GetWindowRect(hwnd, &wndRect);

    RECT r;
    return ::IntersectRect(&r, &rect, &wndRect);
}

BOOL CMonitor::IsOnMonitor(const LPRECT lprc) const
{
    RECT rect;
    GetMonitorRect(&rect);

    RECT r;
    return ::IntersectRect(&r, &rect, lprc);
}

void CMonitor::GetMonitorRect(LPRECT lprc) const
{
    MONITORINFO mi;
    RECT        rc;

    mi.cbSize = sizeof(mi);
    ::GetMonitorInfo(m_hMonitor, &mi);
    rc = mi.rcMonitor;

    ::SetRect(lprc, rc.left, rc.top, rc.right, rc.bottom);
}

//
// the work area does not include the start bar
void CMonitor::GetWorkAreaRect(LPRECT lprc) const
{
    MONITORINFO mi;
    RECT        rc;

    mi.cbSize = sizeof(mi);
    ::GetMonitorInfo(m_hMonitor, &mi);
    rc = mi.rcWork;

    ::SetRect(lprc, rc.left, rc.top, rc.right, rc.bottom);
}

 void CMonitor::CenterRectToMonitor(LPRECT lprc, const BOOL UseWorkAreaRect) const
{
    int  w = lprc->right - lprc->left;
    int  h = lprc->bottom - lprc->top;

    RECT rect;
    if (UseWorkAreaRect) {
        GetWorkAreaRect(&rect);
    } else {
        GetMonitorRect(&rect);
    }

    // Added rounding to get exactly the same rect as the CWnd::CenterWindow method returns.
    lprc->left = std::lround(rect.left + (rect.right - rect.left - w) / 2.0);
    lprc->top = std::lround(rect.top + (rect.bottom - rect.top - h) / 2.0);

    lprc->right = lprc->left + w;
    lprc->bottom = lprc->top + h;
}

void CMonitor::CenterWindowToMonitor(HWND hwnd, const BOOL UseWorkAreaRect) const
{
    RECT rect;
    ::GetWindowRect(hwnd, &rect);
    CenterRectToMonitor(&rect, UseWorkAreaRect);
    ::SetWindowPos(hwnd, nullptr, rect.left, rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CMonitor::ClipRectToMonitor(LPRECT lprc, const BOOL UseWorkAreaRect) const
{
    int w = lprc->right - lprc->left;
    int h = lprc->bottom - lprc->top;

    RECT rect;
    if (UseWorkAreaRect) {
        GetWorkAreaRect(&rect);
    } else {
        GetMonitorRect(&rect);
    }

    lprc->left = max(rect.left,  min(rect.right - w, lprc->left));
    lprc->top = max(rect.top, min(rect.bottom - h, lprc->top));
    lprc->right = lprc->left + w;
    lprc->bottom = lprc->top  + h;
}

BOOL CMonitor::IsPrimaryMonitor() const
{
    MONITORINFO mi;

    mi.cbSize = sizeof(mi);
    ::GetMonitorInfo(m_hMonitor, &mi);

    return mi.dwFlags == MONITORINFOF_PRIMARY;
}

BOOL CMonitor::IsMonitor() const
{
    return CMonitors::IsMonitor(m_hMonitor);
}


//-------------------------------------------------------------------------------------------------
// CMonitors的实现
//-------------------------------------------------------------------------------------------------
CMonitors::CMonitors()
{
    // WARNING : GetSystemMetrics(SM_CMONITORS) return only visible display monitors,
    // and EnumDisplayMonitors enumerate visible and pseudo invisible monitors !!!
    ADDMONITOR addMonitor;
    addMonitor.pMonitors = &m_Monitors;
    addMonitor.currentIndex = 0;
    ::EnumDisplayMonitors(nullptr, nullptr, AddMonitorsCallBack, (LPARAM)&addMonitor);
}

CMonitors::~CMonitors()
{
    for (size_t i = 0; i < m_Monitors.size(); i++) {
        delete m_Monitors[i];
    }
    m_Monitors.clear();
}

BOOL CALLBACK CMonitors::AddMonitorsCallBack(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    LPADDMONITOR pAddMonitor = (LPADDMONITOR)dwData;

    CMonitor* pMonitor = new CMonitor;
    pMonitor->Attach(hMonitor);

    pAddMonitor->pMonitors->push_back(pMonitor);
    pAddMonitor->currentIndex++;

    return TRUE;
}

CMonitor CMonitors::GetPrimaryMonitor()
{
    // 主显示器的原点坐标总是(0,0)。
    POINT originPt = { 0 };
    HMONITOR hMonitor = ::MonitorFromPoint(originPt, MONITOR_DEFAULTTOPRIMARY);

    CMonitor monitor;
    monitor.Attach(hMonitor);

    return monitor;
}

BOOL CMonitors::IsMonitor(const HMONITOR hMonitor)
{
    if (hMonitor == nullptr) {
        return FALSE;
    }

    MATCHMONITOR match;
    match.target = hMonitor;
    match.foundMatch = FALSE;

    ::EnumDisplayMonitors(nullptr, nullptr, FindMatchingMonitorHandle, (LPARAM)&match);

    return match.foundMatch;
}

BOOL CALLBACK CMonitors::FindMatchingMonitorHandle(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    LPMATCHMONITOR pMatch = (LPMATCHMONITOR)dwData;

    if (hMonitor == pMatch->target) {
        pMatch->foundMatch = TRUE;
        return FALSE;
    }

    pMatch->foundMatch = FALSE;
    return TRUE;
}

BOOL CMonitors::AllMonitorsShareDisplayFormat()
{
    return ::GetSystemMetrics(SM_SAMEDISPLAYFORMAT);
}

int CMonitors::GetMonitorCount()
{
    return ::GetSystemMetrics(SM_CMONITORS);
}

CMonitor CMonitors::GetMonitor(const int index) const
{
    return *m_Monitors[index];
}

void CMonitors::GetVirtualDesktopRect(LPRECT lprc)
{
    ::SetRect(lprc,
        ::GetSystemMetrics(SM_XVIRTUALSCREEN),
        ::GetSystemMetrics(SM_YVIRTUALSCREEN),
        ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
        ::GetSystemMetrics(SM_CYVIRTUALSCREEN));

}

BOOL CMonitors::IsOnScreen(const LPRECT lprc)
{
    return ::MonitorFromRect(lprc, MONITOR_DEFAULTTONULL) != nullptr;
}

BOOL CMonitors::IsOnScreen(const POINT& pt)
{
    return ::MonitorFromPoint(pt, MONITOR_DEFAULTTONULL) != nullptr;
}

BOOL CMonitors::IsOnScreen(const HWND hwnd)
{
    return ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL) != nullptr;
}

CMonitor CMonitors::GetNearestMonitor(const LPRECT lprc)
{
    CMonitor monitor;
    monitor.Attach(::MonitorFromRect(lprc, MONITOR_DEFAULTTONEAREST));
    return monitor;
}

CMonitor CMonitors::GetNearestMonitor(const POINT& pt)
{
    CMonitor monitor;
    monitor.Attach(::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST));
    return monitor;
}

CMonitor CMonitors::GetNearestMonitor(const HWND hwnd)
{
    CMonitor monitor;
    monitor.Attach(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
    return monitor;
}
