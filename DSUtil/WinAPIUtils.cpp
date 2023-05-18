/*
 * (C) 2011-2015, 2017 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include <d3d9.h>
#include <Shlobj.h>
#include "WinAPIUtils.h"
#include "PathUtils.h"


bool SetPrivilege(LPCTSTR privilege, bool bEnable)
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;

    SetThreadExecutionState(ES_CONTINUOUS);

    // Get a token for this process.
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    // Get the LUID for the privilege.
    LookupPrivilegeValue(nullptr, privilege, &tkp.Privileges[0].Luid);

    tkp.PrivilegeCount = 1;  // one privilege to set
    tkp.Privileges[0].Attributes = bEnable ? SE_PRIVILEGE_ENABLED : 0;

    // Set the privilege for this process.
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)nullptr, 0);

    return (GetLastError() == ERROR_SUCCESS);
}

CString GetHiveName(const HKEY hive)
{
    switch ((ULONG_PTR)hive) {
        case (ULONG_PTR)HKEY_CLASSES_ROOT:
            return _T("HKEY_CLASSES_ROOT");
        case (ULONG_PTR)HKEY_CURRENT_USER:
            return _T("HKEY_CURRENT_USER");
        case (ULONG_PTR)HKEY_LOCAL_MACHINE:
            return _T("HKEY_LOCAL_MACHINE");
        case (ULONG_PTR)HKEY_USERS:
            return _T("HKEY_USERS");
        case (ULONG_PTR)HKEY_PERFORMANCE_DATA:
            return _T("HKEY_PERFORMANCE_DATA");
        case (ULONG_PTR)HKEY_CURRENT_CONFIG:
            return _T("HKEY_CURRENT_CONFIG");
        case (ULONG_PTR)HKEY_DYN_DATA:
            return _T("HKEY_DYN_DATA");
        case (ULONG_PTR)HKEY_PERFORMANCE_TEXT:
            return _T("HKEY_PERFORMANCE_TEXT");
        case (ULONG_PTR)HKEY_PERFORMANCE_NLSTEXT:
            return _T("HKEY_PERFORMANCE_NLSTEXT");
        default:
            return _T("");
    }
}

UINT GetAdapterByD3D9(IDirect3D9* pD3D, HWND hWnd)
{
    if (hWnd == nullptr || pD3D == nullptr) {
        return D3DADAPTER_DEFAULT;
    }

    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor == nullptr) {
        return D3DADAPTER_DEFAULT;
    }

    UINT num_adp = pD3D->GetAdapterCount();
    for (UINT adp = 0; adp < num_adp; ++adp) {
        HMONITOR hAdpMon = pD3D->GetAdapterMonitor(adp);
        if (hAdpMon == hMonitor) {
            return adp;
        }
    }

    return D3DADAPTER_DEFAULT;
}

int CALLBACK EnumFontFamExProc(ENUMLOGFONTEX* /*lpelfe*/, NEWTEXTMETRICEX* /*lpntme*/, int /*FontType*/, LPARAM lParam)
{
    LPARAM* l = (LPARAM*)lParam;
    *l = TRUE;
    return TRUE;
}

namespace
{
    void GetNonClientMetrics(NONCLIENTMETRICS* ncm)
    {
        ZeroMemory(ncm, sizeof(NONCLIENTMETRICS));
        ncm->cbSize = sizeof(NONCLIENTMETRICS);
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm->cbSize, ncm, 0);
    }
}

void GetMessageFont(LOGFONT* lf)
{
    NONCLIENTMETRICS ncm;
    GetNonClientMetrics(&ncm);
    *lf = ncm.lfMessageFont;
    ASSERT(lf->lfHeight);
}

void GetStatusFont(LOGFONT* lf)
{
    NONCLIENTMETRICS ncm;
    GetNonClientMetrics(&ncm);
    *lf = ncm.lfStatusFont;
    ASSERT(lf->lfHeight);
}

bool IsFontInstalled(LPCTSTR lpszFont)
{
    // Get the screen DC
    // commented by yxs
    //CDC dc;
    //if (!dc.CreateCompatibleDC(nullptr)) {
    //    return false;
    //}

    LOGFONT lf;
    ZeroMemory(&lf, sizeof(LOGFONT));
    // Any character set will do
    lf.lfCharSet = DEFAULT_CHARSET;
    // Set the facename to check for
    _tcscpy_s(lf.lfFaceName, lpszFont);
    LPARAM lParam = 0;
    // Enumerate fonts
    // commented by yxs
    //EnumFontFamiliesEx(dc.GetSafeHdc(), &lf, (FONTENUMPROC)EnumFontFamExProc, (LPARAM)&lParam, 0);
    DebugBreak();

    return lParam ? true : false;
}

bool ExploreToFile(LPCTSTR path)
{
    CoInitializeHelper co;

    bool success = false;
    PIDLIST_ABSOLUTE pidl;

    if (PathUtils::Exists(path) && SHParseDisplayName(path, nullptr, &pidl, 0, nullptr) == S_OK) {
        success = SUCCEEDED(SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0));
        CoTaskMemFree(pidl);
    }

    return success;
}

CoInitializeHelper::CoInitializeHelper()
{
    HRESULT res = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (res == RPC_E_CHANGED_MODE) { // Try another threading model
        res = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }
    if (res != S_OK && res != S_FALSE) {
        throw res;
    }
}

CoInitializeHelper::~CoInitializeHelper()
{
    CoUninitialize();
}

HRESULT FileDelete(CString file, HWND hWnd, bool recycle /*= true*/)
{
    // Strings in SHFILEOPSTRUCT must be double-null terminated
    file.AppendChar(_T('\0'));

    SHFILEOPSTRUCT fileOpStruct;
    ZeroMemory(&fileOpStruct, sizeof(SHFILEOPSTRUCT));
    fileOpStruct.hwnd = hWnd;
    fileOpStruct.wFunc = FO_DELETE;
    fileOpStruct.pFrom = file;
    if (recycle) {
        fileOpStruct.fFlags = FOF_ALLOWUNDO | FOF_WANTNUKEWARNING;
    }
    int hRes = SHFileOperation(&fileOpStruct);
    if (fileOpStruct.fAnyOperationsAborted) {
        hRes = E_ABORT;
    }
    //TRACE(_T("Delete recycle=%d hRes=0x%08x, file=%s\n"), recycle, hRes, file.GetString());
    return hRes;
}
