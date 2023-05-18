#include "stdafx.h"
#include "global.h"

//CFactoryTemplate g_Templates[] = { 0 };
//int g_cTemplates = 0;

#ifdef _ADM_ACTIVEX

#include "resource.h"
#include <initguid.h>
#include "xsengine_guid.h"
#include "xsengine_ctl.h"
#include "xse_ctl.h"

OBJECT_ENTRY_AUTO(CLSID_XSEngine, CXSEngine)

HINSTANCE g_this_module = 0;

volatile long ClassFactory::m_serverLocks = 0;
ClassFactoryData g_ClassFactories[] = {
    { &CLSID_XSEngine, nullptr }
};
const DWORD g_numClassFactories = ARRAY_SIZE(g_ClassFactories);

class CXSEngineModule : public CAtlDllModuleT<CXSEngineModule>
{
public:
    CXSEngineModule() {
        DbgInitialise(g_this_module);
    }

    ~CXSEngineModule() {
        DbgTerminate();
    }

    DECLARE_LIBID(LIBID_XSEngineLib)
    STDMETHOD(post)(BSTR reqStr, int* reqId);
};
CXSEngineModule _AtlModule;

BOOL APIENTRY DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_this_module = (HMODULE)hModule;
        break;

    case DLL_THREAD_ATTACH:
        break;

    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        break;
    }
    return _AtlModule.DllMain(reason, lpReserved);
}

STDAPI DllRegisterServer()
{
    HRESULT hr = _AtlModule.DllRegisterServer();
    if (FAILED(hr))
        return hr;
    hr = RegisterSafeItem(CLSID_XSEngine);
    return hr;
}

STDAPI DllUnregisterServer()
{
    HRESULT hr = _AtlModule.DllUnregisterServer();
    if (FAILED(hr))
        return hr;
    hr = UnregisterSafeItem(CLSID_XSEngine);
    return hr;
}

STDAPI DllCanUnloadNow(void)
{
    return _AtlModule.DllCanUnloadNow();
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

STDMETHODIMP CXSEngineModule::post(BSTR reqStr, int* reqId)
{
    // TODO: 在此处添加实现代码

    return S_OK;
}

#endif // end #ifdef _ADM_ACTIVEX



