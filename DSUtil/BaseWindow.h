#pragma once

template <class DERIVED_TYPE>
class BaseWindowT
{
public:
    HWND m_hwnd = NULL;

    ~BaseWindowT() {
        if (m_hwnd != NULL) {
            SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)NULL);
            DestroyWindow(m_hwnd);
            m_hwnd = NULL;
        }
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        DERIVED_TYPE* pThis = NULL;

        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (DERIVED_TYPE*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
            pThis->m_hwnd = hwnd;
        }
        else {
            pThis = (DERIVED_TYPE*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }
        if (pThis != NULL) {
            return pThis->HandleMessage(uMsg, wParam, lParam);
        }
        else{
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }

    HRESULT Create(PCWSTR lpWindowName, int nClientAreaWidth, int nClientAreaHeight)
    {
        HRESULT hr = S_OK;

        WNDCLASS wc;
        if (!GetClassInfoW(g_hInstance, ClassName(), &wc))
        {
            WNDCLASSEXW wce;
            wce.cbSize = sizeof(WNDCLASSEXW);
            wce.style = 0;
            wce.lpfnWndProc = DERIVED_TYPE::WindowProc;
            wce.cbClsExtra = 0;
            wce.cbWndExtra = 0;
            wce.hInstance = GetModuleHandle(NULL);
            wce.hIcon = 0;
            wce.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wce.hbrBackground = 0;// GetStockBrush(BLACK_BRUSH);
            wce.lpszMenuName = nullptr;
            wce.lpszClassName = ClassName();
            wce.hIconSm = 0;
            RegisterClassExW(&wce);
        }
        if (m_hwnd == NULL)
        {
            DWORD dwStyle = WS_OVERLAPPEDWINDOW;
            DWORD dwExStyle = 0;
            HWND hWndParent = 0;
            HMENU hMenu = 0;

            RECT rc = { 0, 0, nClientAreaWidth, nClientAreaHeight };
            AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
            int nFrameWidth = rc.right - rc.left;
            int nFrameHeight = rc.bottom - rc.top;

            m_hwnd = CreateWindowExW(dwExStyle, ClassName(), lpWindowName, dwStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                nFrameWidth, nFrameHeight, hWndParent, hMenu, GetModuleHandle(NULL), this);
        }

        return (m_hwnd ? S_OK : E_FAIL);
    }

    UINT_PTR SetTimer(UINT_PTR id, DWORD ms, void* ctx)
    {
        return ::SetTimer(m_hwnd, id, ms, ctx);
    }

    virtual void OnTimer(UINT_PTR id, void* ctx)
    {
        return;
    }


protected:
    virtual PCWSTR  ClassName() const { return L"windows.codemi.net"; }
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_TIMER:
            OnTimer(wParam, (void*)lParam);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return ::DefWindowProc(m_hwnd, uMsg, wParam, lParam);
        }
    }
};
