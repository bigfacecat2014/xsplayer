#include "stdafx.h"
#include "resource.h"
#include "MM.h"

HINSTANCE g_hInstance = 0;
xse_t g_xse = nullptr;

class BasicMainWindow
{
public:
    enum { DRAW_TIMER_ID = 1000 };
    const DWORD TIMER_INTERVAL = 15; // 定时器间隔
    // 最小呈现时间间隔，根据混合呈现器消费速度抖动统计数据动态加速或者减速。
    // 解码速度慢了(Debug)，网络源那里会有未解码数据积压，UI线程老是从混合呈现器那里拿不到数据。
    // 解码速度快了(Release)，混合呈现器那里会快速生产已解码数据，UI线程若不能及时拿走，
    // 会被上游解码快速流出的数据覆盖样本，UI线程老是抢不到刚好解码的样本，会出现跳帧。

    HWND m_hwnd = NULL;
    UINT_PTR m_drawTimer = 0;
    bool m_isFullScreen = false;
    int m_channelCount = 1; // 已映射并使能的通道格式。
    bool m_channelSwitch[XSE_MAX_CHANNEL_COUNT]; // 通道使能开关（UI复选框的值）
    std::wstring m_channelURL[XSE_MAX_CHANNEL_COUNT]; // 通道URL映射表。
    RECT m_lastWindowRect = { 0 };
    DWORD m_styleWindowed = WS_OVERLAPPEDWINDOW;
    DWORD m_styleFullScreen = WS_POPUP;
    DWORD m_lastPresentTime = 0;
    DWORD m_lastUpdateBeginTime = 0;
    DWORD m_lastRenderBeginTime = 0;
    DWORD m_lastRenderEndTime = 0;

    enum { VIEW_MODE_COUNT = 4 };
    int m_modeList[VIEW_MODE_COUNT] = { 1, 4, 9, 16 };
    int m_curMode = 0;
    bool m_isPaused = false;
    bool m_enableOnTimerRender = false; // 使能定时器驱动的渲染。

    HRESULT Create(PCWSTR lpWindowName, int nClientWidth, int nClientHeight)
    {
        HRESULT hr = S_OK;

        const wchar_t* className = L"windows.codemi.net";
        WNDCLASS wc;
        if (!GetClassInfoW(g_hInstance, className, &wc)) {
            WNDCLASSEXW wce;
            wce.cbSize = sizeof(WNDCLASSEXW);
            wce.style = CS_DBLCLKS | CS_OWNDC;
            wce.lpfnWndProc = BasicMainWindow::WindowProc;
            wce.cbClsExtra = 0;
            wce.cbWndExtra = 0;
            wce.hInstance = GetModuleHandle(NULL);
            wce.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_XSPLAYER));
            wce.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wce.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            wce.lpszMenuName = nullptr;
            wce.lpszClassName = className;
            wce.hIconSm = 0;
            RegisterClassExW(&wce);
        }

        if (m_hwnd == NULL) {
            HWND hWndParent = 0;
            HMENU hMenu = 0;

            RECT rc = { 0, 0, nClientWidth, nClientHeight };
            ::AdjustWindowRectEx(&rc, m_styleWindowed, FALSE, 0);
            int nFrameWidth = rc.right - rc.left;
            int nFrameHeight = rc.bottom - rc.top;

            // 在当前屏幕居中显示。
            {
                CMonitor monitor = CMonitors::GetNearestMonitor(::GetDesktopWindow());
                monitor.CenterRectToMonitor(&rc, TRUE);
            }

            m_hwnd = CreateWindowExW(0, className, lpWindowName, m_styleWindowed, rc.left, rc.top,
                nFrameWidth, nFrameHeight, hWndParent, hMenu, g_hInstance, this);

            ShowWindow(m_hwnd, SW_SHOWNORMAL);
            UpdateWindow(m_hwnd);
        }

        return (m_hwnd ? S_OK : E_FAIL);
    }

    ~BasicMainWindow()
    {
        if (m_hwnd != NULL) {
            SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)NULL);
            DestroyWindow(m_hwnd);
            m_hwnd = NULL;
        }
    }

    void StartDrawTimer()
    {
        UINT_PTR id = ::SetTimer(m_hwnd, DRAW_TIMER_ID, TIMER_INTERVAL, nullptr);
        assert(id == DRAW_TIMER_ID);
    }

    void EnableOnTimerRender(bool bEnable)
    {
        m_enableOnTimerRender = bEnable;
    }

    void StartAllChannels()
    {
        for (int i = 0; i < m_channelCount; ++i) {
            xse_arg_open_t a;
            a.channel = i;
            wcscpy_s(a.url, _countof(a.url), L"rtsp://127.0.0.1:554/ss265.mkv");
            //wcscpy_s(a.url, _countof(a.url), L"rtsp://192.168.0.64:554/");
            wcscpy_s(a.user_name, _countof(a.user_name), L"admin");
            wcscpy_s(a.password, _countof(a.user_name), L"codemi.net");
            a.auto_run = true;
            a.ctx = this;
            a.cb = StaticOnPlayComplete;
            xse_control(g_xse, &a);
        }

        // 自动选择最佳视图模式
        for (int i = 0; i < VIEW_MODE_COUNT; ++i) {
            m_curMode = i;
            if (m_modeList[i] >= m_channelCount) {
                break;
            }
        }
        {
            xse_arg_view_t a;
            a.mode = m_curMode;
            xse_control(g_xse, &a);
        }
    }

    void PlayAllChannels()
    {
        for (int i = 0; i < m_channelCount; ++i) {
            xse_arg_play_t a;
            a.channel = i;
            xse_control(g_xse, &a);
        }
    }

    void PauseAllChannels()
    {
        for (int i = 0; i < m_channelCount; ++i) {
            xse_arg_pause_t a;
            a.channel = i;
            xse_control(g_xse, &a);
        }
    }

    void StopAllChannels()
    {
        for (int i = 0; i < m_channelCount; ++i) {
            xse_arg_stop_t a;
            a.channel = i;
            a.ctx = this;
            a.cb = StaticOnStopComplete;
            xse_control(g_xse, &a);
        }
    }

    bool Render()
    {
        if (g_xse == nullptr)
            return false;

        if (SyncUpdate())
            SyncRender();

        m_lastPresentTime = timeGetTime();

        return true;
    }

    void OnTimer(UINT_PTR id, void* ctx)
    {
        assert(id == DRAW_TIMER_ID);
        DWORD curTime = ::timeGetTime();
        DWORD dt = curTime - m_lastPresentTime;
        if (dt > 50) // 消息循环被拦截，由定时器负责补刷。
            m_enableOnTimerRender = true;
        if (m_enableOnTimerRender)
            Render();
    }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        BasicMainWindow* pThis = NULL;

        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (BasicMainWindow*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
            pThis->m_hwnd = hwnd;
        }
        else {
            pThis = (BasicMainWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }
        if (pThis != NULL) {
            return pThis->HandleMessage(uMsg, wParam, lParam);
        }
        else {
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }


    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        static int count = 0;

        switch (uMsg)
        {
        case WM_TIMER:
            OnTimer(wParam, (void*)lParam);
            return 0;

        case WM_PAINT:
            PAINTSTRUCT ps;
            if (NULL != BeginPaint(m_hwnd, &ps)) {
                SyncRender();
                EndPaint(m_hwnd, &ps);
            }
            return 0;

        case WM_KEYUP:
            {
                if (wParam == 'V') {
                    m_curMode = (m_curMode + 1) % VIEW_MODE_COUNT;
                    xse_arg_view_t a;
                    a.mode = m_curMode;
                    xse_control(g_xse, &a);
                    ::InvalidateRect(m_hwnd, nullptr, TRUE);
                }
                else if (wParam >= '1' && wParam <= '4') {
                    m_curMode = wParam - '1';
                    xse_arg_view_t a;
                    a.mode = m_curMode;
                    xse_control(g_xse, &a);
                    ::InvalidateRect(m_hwnd, nullptr, TRUE);
                }
                else if (wParam == VK_SPACE) {
                    if (m_isPaused) {
                        PlayAllChannels();
                    }
                    else {
                        PauseAllChannels();
                    }
                    m_isPaused = !m_isPaused;
                }
                else if (wParam == VK_F11 || (wParam == VK_ESCAPE && m_isFullScreen)) {
                    ToggleFullscreen();
                }
            }
            return 0;

        case WM_ERASEBKGND:
            //fprintf(stdout, "WM_ERASEBKGND(%d)\n", ++count);
            // 如果正在播放，那么不要擦除背景，一会儿引擎会自动覆盖背景。
            // 如果不在播放，那么要刷新背景，理论上OpenGL引擎会总是会自动处理背景擦除。
            // 我们只需要把这个通知告知它，让它做处理即可，永远不要用GDI画刷去擦除背景。
            // 务必要保证100%做到脏矩形刷新，按需刷新，保证渲染器不做无用功，电能效率100%。
            //return 1;
            break;

        case WM_LBUTTONDBLCLK:
            ToggleFullscreen();
            return 0;

        case WM_SIZE:
            OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;           
        }

        return ::DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }

    void OnSize(LONG cx, LONG cy)
    {
        if (g_xse != nullptr) {
            xse_arg_sync_resize_t a;
            a.pos_rect = { 0, 0, cx, cy };
            a.clip_rect = a.pos_rect;
            xse_control(g_xse, &a);
            ::InvalidateRect(m_hwnd, nullptr, TRUE);
        }
    }

    void SyncRender()
    {
        if (g_xse != nullptr) {
            xse_arg_sync_render_t a;
            DWORD curTime = timeGetTime();
            a.base_time = curTime;
            a.last_render_begin_time = m_lastRenderBeginTime;
            a.last_render_end_time = m_lastRenderEndTime;
            a.cur_render_begin_time = curTime;
            a.hdc = ::GetDC(m_hwnd);
            ::GetClientRect(m_hwnd, &a.bound_rect);
            xse_control(g_xse, &a);
            m_lastRenderBeginTime = curTime;
            m_lastRenderEndTime = timeGetTime();
        }
    }

    bool SyncUpdate()
    {
        xse_arg_sync_update_t a;
        DWORD curTime = timeGetTime();
        a.base_time = curTime;
        a.last_update_time = m_lastUpdateBeginTime;
        m_lastUpdateBeginTime = curTime;
        a.cur_update_time = curTime;
        xse_control(g_xse, &a);
        return a.result == xse_err_ok ? true : false;
    }

    // 全屏是容器窗口的权力和义务。
    // 控件自身不应当染指容器窗口的全屏操作。
    // 控件只需正确响应容器客户区位置和大小的变化通知即可。
    void ToggleFullscreen()
    {
        RECT wr;
        HWND hwndInsertAfter = 0;

        CMonitor currentMonitor = CMonitors::GetNearestMonitor(m_hwnd);

        if (!m_isFullScreen) { // 进入全屏
            ::GetWindowRect(m_hwnd, &m_lastWindowRect);
            currentMonitor.GetMonitorRect(&wr);
            m_styleWindowed = ::GetWindowLong(m_hwnd, GWL_STYLE);
            ::SetWindowLong(m_hwnd, GWL_STYLE, m_styleFullScreen);
            hwndInsertAfter = HWND_TOPMOST;
        }
        else { // 退出全屏
            wr = m_lastWindowRect;
            // 判断是否原来的窗口矩形在当前显示上下文中已经不存在了。
            if (!CMonitors::IsOnScreen(&wr)) {
                currentMonitor.CenterRectToMonitor(&wr, TRUE);
            }

            ::SetWindowLong(m_hwnd, GWL_STYLE, m_styleWindowed);
            hwndInsertAfter = HWND_NOTOPMOST;
            ::InvalidateRect(m_hwnd, nullptr, TRUE);
        }
        ::SetWindowPos(m_hwnd, hwndInsertAfter,  wr.left, wr.top,
            wr.right - wr.left, wr.bottom - wr.top, SWP_SHOWWINDOW);
        m_isFullScreen = !m_isFullScreen;
    }

    void OnPlayComplete(xse_arg_open_t* arg)
    {
    }

    void OnStopComplete(xse_arg_stop_t* arg)
    {
        ::InvalidateRect(m_hwnd, nullptr, TRUE); // 强制刷新
    }

    static void CALLBACK StaticOnPlayComplete(xse_arg_t* arg)
    {
        ((BasicMainWindow*)arg->ctx)->OnPlayComplete((xse_arg_open_t*)arg);
    }

    static void CALLBACK StaticOnStopComplete(xse_arg_t* arg)
    {
        ((BasicMainWindow*)arg->ctx)->OnStopComplete((xse_arg_stop_t*)arg);
    }
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    g_hInstance = hInstance;
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    UINT timerResolution = 1;
    {
        TIMECAPS tc;
        if (0 == ::timeGetDevCaps(&tc, sizeof(tc) == 0))
            timerResolution = tc.wPeriodMin;
        ::timeBeginPeriod(timerResolution);
    }

//#ifdef _DEBUG
    AllocConsole();
    FILE* fo = nullptr;
    _wfreopen_s(&fo, L"CONOUT$", L"w", stdout);
    _wfreopen_s(&fo, L"CONOUT$", L"w", stderr);
    fprintf(stdout, "hello, xsplayer host app.\n");
//#endif

    BasicMainWindow bmw;
    bmw.Create(L"XSPlayer", 1280, 720);
    xse_create(bmw.m_hwnd, &g_xse);
    bmw.StartAllChannels();
    bmw.StartDrawTimer();

    // 主线程的消息循环
    int frameCount = 0;
    int loopCount = 0;
    DWORD lastCountTime = timeGetTime();
    for (bool bQuit = false; !bQuit;)
    {
        DWORD lastPTS = timeGetTime();
        // 精确定时渲染
        loopCount++;
        if (bmw.Render()) {
            frameCount++;
        }
        bmw.EnableOnTimerRender(false); // 标明消息循环没有被系统拦截。
        DWORD fpsInterval = timeGetTime() - lastCountTime;
        if (fpsInterval >= 2000) {
            fprintf(stderr, "fps=%d/%d\n", frameCount/2, loopCount/2);
            lastCountTime = timeGetTime();
            frameCount = 0;
            loopCount = 0;
        }

        do
        {
            bmw.EnableOnTimerRender(false); // 标明消息循环没有被系统拦截，避免WM_TIMER刷新。

            // 处理线程消息队列
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    bQuit = true;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            // 处理引擎的完成通知（回调）。
            {
                xse_arg_t a;
                xse_control(g_xse, &a);
            }
            // 眯一会儿，等待下一个呈现时刻到来。
            {
                DWORD curTime = timeGetTime();
                if (curTime >= lastPTS + 16) // 稍微比60FPS间隔小一点，保证能打满60FPS。
                    break;
                Sleep(1);
            }
        } while (!bQuit);
    }

    // 关闭通道和引擎
    {
        bmw.StopAllChannels();
        xse_control(g_xse, nullptr);
        xse_destroy(g_xse);
        g_xse = nullptr;
    }

    ::timeEndPeriod(timerResolution);

    return 0;
}