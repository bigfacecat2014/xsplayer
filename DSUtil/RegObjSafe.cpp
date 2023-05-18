#include "stdafx.h"
#include "RegObjSafe.h"

// 创建组件分类管理器
HRESULT CreateComponentCategory(CATID catid, WCHAR* catDescription)
{
    HRESULT hr = S_OK;
    ICatRegister* pcr = NULL;

    hr = CoCreateInstance(CLSID_StdComponentCategoriesMgr, NULL, CLSCTX_INPROC_SERVER, IID_ICatRegister, (void**)&pcr);
    if (FAILED(hr))
        return hr;

    // Make sure the HKCR\Component Categories\{..catid...} key is registered.
    CATEGORYINFO catinfo;
    catinfo.catid = catid;
    catinfo.lcid = 0x0409; // english
    size_t len = wcslen(catDescription);
    wcscpy_s(catinfo.szDescription, _countof(catinfo.szDescription), catDescription);
    catinfo.szDescription[len + 1] = '\0';
    hr = pcr->RegisterCategories(1, &catinfo);
    SAFE_RELEASE(pcr);

    return hr;
}

// 向组件分类管理器注册一个分类ID
HRESULT RegisterCLSIDInCategory(REFCLSID clsid, CATID catid)
{
    HRESULT hr = S_OK;
    ICatRegister* pcr = NULL;

    hr = CoCreateInstance(CLSID_StdComponentCategoriesMgr, NULL, CLSCTX_INPROC_SERVER, IID_ICatRegister, (void**)&pcr);
    if (FAILED(hr))
        return hr;

    {
        CATID rgcatid[1];
        rgcatid[0] = catid;
        hr = pcr->RegisterClassImplCategories(clsid, 1, rgcatid);
        SAFE_RELEASE(pcr);
    }

    return hr;
}

// 向组件分类管理器注销一个分类ID
HRESULT UnRegisterCLSIDInCategory(REFCLSID clsid, CATID catid)
{
    HRESULT hr = S_OK;
    ICatRegister* pcr = NULL;

    hr = CoCreateInstance(CLSID_StdComponentCategoriesMgr, NULL, CLSCTX_INPROC_SERVER, IID_ICatRegister, (void**)&pcr);
    if (FAILED(hr))
        return hr;

    {
        CATID rgcatid[1];
        rgcatid[0] = catid;
        hr = pcr->UnRegisterClassImplCategories(clsid, 1, rgcatid);
        SAFE_RELEASE(pcr);
    }

    return hr;
}

// 将CLSID_SafeItem注册为可被安全初始化且被IE中的网页的脚本调用的。
STDAPI RegisterSafeItem(const GUID& CLSID_SafeItem)
{
    HRESULT hr = S_OK;

    hr = CreateComponentCategory(CATID_SafeForInitializing, L"Controls safely initializable from persistent data.");
    if (FAILED(hr))
        return hr;

    hr = RegisterCLSIDInCategory(CLSID_SafeItem, CATID_SafeForInitializing);
    if (FAILED(hr))
        return hr;

    hr = CreateComponentCategory(CATID_SafeForScripting, L"Controls safely scriptable.");
    if (FAILED(hr))
        return hr;

    hr = RegisterCLSIDInCategory(CLSID_SafeItem, CATID_SafeForScripting);
    if (FAILED(hr))
        return hr;

    return hr;
}

// 注销对CLSID_SafeItem的安全申明。
STDAPI UnregisterSafeItem(const GUID& CLSID_SafeItem)
{
    HRESULT hr = S_OK;

    hr = UnRegisterCLSIDInCategory(CLSID_SafeItem, CATID_SafeForInitializing);
    if (FAILED(hr))
        return hr;

    hr = UnRegisterCLSIDInCategory(CLSID_SafeItem, CATID_SafeForScripting);
    if (FAILED(hr))
        return hr;

    return hr;
}