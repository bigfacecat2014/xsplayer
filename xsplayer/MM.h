#pragma once

class CMonitor
{
public:
    CMonitor();
    CMonitor(const CMonitor& monitor);

    void Attach(const HMONITOR hMonitor);
    HMONITOR Detach();

    void ClipRectToMonitor(LPRECT lprc, const BOOL UseWorkAreaRect = FALSE) const;
    void CenterRectToMonitor(LPRECT lprc, const BOOL UseWorkAreaRect = FALSE) const;
    void CenterWindowToMonitor(HWND hwnd, const BOOL UseWorkAreaRect = FALSE) const;

    HDC CreateDC() const;

    void GetMonitorRect(LPRECT lprc) const;
    void GetWorkAreaRect(LPRECT lprc) const;

    PCWSTR GetName() const;

    int GetBitsPerPixel() const;

    BOOL IsOnMonitor(const POINT& pt) const;
    BOOL IsOnMonitor( HWND hwnd) const;
    BOOL IsOnMonitor(const LPRECT lprc) const;

    BOOL IsPrimaryMonitor() const;
    BOOL IsMonitor() const;

    operator HMONITOR() const {
        return this == nullptr ? nullptr : m_hMonitor;
    }

    BOOL operator ==(const CMonitor& monitor) const {
        return m_hMonitor == (HMONITOR)monitor;
    }

    BOOL operator !=(const CMonitor& monitor) const {
        return !(*this == monitor);
    }

    CMonitor& operator =(const CMonitor& monitor) {
        m_hMonitor = (HMONITOR)monitor;
        return *this;
    }

private:
    HMONITOR m_hMonitor = 0;
    MONITORINFOEX m_infoEx = { 0 };
};


class CMonitors
{
public:
    CMonitors();
    virtual ~CMonitors();

    CMonitor GetMonitor(const int index) const;

    int GetCount() const {
        return (int)m_Monitors.size();
    }

    //static members
    static CMonitor GetNearestMonitor(const LPRECT lprc);
    static CMonitor GetNearestMonitor(const POINT& pt);
    static CMonitor GetNearestMonitor(const HWND hwnd);

    static BOOL IsOnScreen(const POINT& pt);
    static BOOL IsOnScreen(const HWND hwnd);
    static BOOL IsOnScreen(const LPRECT lprc);

    static void GetVirtualDesktopRect(LPRECT lprc);

    static BOOL IsMonitor(const HMONITOR hMonitor);

    static CMonitor GetPrimaryMonitor();
    static BOOL AllMonitorsShareDisplayFormat();

    static int GetMonitorCount();

private:
    typedef std::vector<CMonitor*> MonitorArray;
    MonitorArray m_Monitors;

    typedef struct tagMATCHMONITOR {
        HMONITOR target;
        BOOL foundMatch;
    } MATCHMONITOR, * LPMATCHMONITOR;

    static BOOL CALLBACK FindMatchingMonitorHandle(
        HMONITOR hMonitor,  // handle to display monitor
        HDC hdcMonitor,     // handle to monitor DC
        LPRECT lprcMonitor, // monitor intersection rectangle
        LPARAM dwData       // data
    );

    typedef struct tagADDMONITOR {
        MonitorArray* pMonitors;
        int currentIndex;
    } ADDMONITOR, * LPADDMONITOR;

    static BOOL CALLBACK AddMonitorsCallBack(
        HMONITOR hMonitor,  // handle to display monitor
        HDC hdcMonitor,     // handle to monitor DC
        LPRECT lprcMonitor, // monitor intersection rectangle
        LPARAM dwData       // data
    );
};
