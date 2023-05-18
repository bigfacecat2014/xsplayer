#pragma once

struct IDirect3D9;

bool SetPrivilege(LPCTSTR privilege, bool bEnable = true);
UINT GetAdapterByD3D9(IDirect3D9* pD3D, HWND hWnd);
void GetMessageFont(LOGFONT* lf);
void GetStatusFont(LOGFONT* lf);
bool IsFontInstalled(LPCTSTR lpszFont);
bool ExploreToFile(LPCTSTR path);
HRESULT FileDelete(CString file, HWND hWnd, bool recycle = true);

class CoInitializeHelper
{
public:
    CoInitializeHelper();
    ~CoInitializeHelper();
};

template <typename>
class WinapiFunc;

template <typename ReturnType, typename...Args>
struct WinapiFunc<ReturnType WINAPI(Args...)> final {
    typedef ReturnType(WINAPI* WinapiFuncType)(Args...);

    WinapiFunc() = delete;
    WinapiFunc(const WinapiFunc&) = delete;
    WinapiFunc& operator=(const WinapiFunc&) = delete;

    inline WinapiFunc(LPCTSTR dll, LPCSTR func)
        : m_hLib(LoadLibrary(dll))
        , m_pWinapiFunc(reinterpret_cast<WinapiFuncType>(GetProcAddress(m_hLib, func))) {
    }

    inline ~WinapiFunc() {
        FreeLibrary(m_hLib);
    }

    inline explicit operator bool() const {
        return !!m_pWinapiFunc;
    }

    inline ReturnType operator()(Args...args) const {
        return m_pWinapiFunc(args...);
    }

private:
    const HMODULE m_hLib;
    const WinapiFuncType m_pWinapiFunc;
};
