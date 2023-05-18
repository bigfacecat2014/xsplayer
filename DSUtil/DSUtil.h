/*7
 * (C) 2003-2006 Gabest
 * (C) 2006-2017 see Authors.txt
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

#pragma once

#include "SharedInclude.h"
#include "MediaTypeEx.h"
#include "StrConv.h"
#include "baseclasses/streams.h"
#include "baseclasses/source.h"
#include "baseclasses/dshowutil.h"
#include "baseclasses/checkbmi.h"

#ifndef LCID_NOSUBTITLES
#define LCID_NOSUBTITLES -1
#endif

 // SafeRelease Template, for type safety
template <class T> void SafeRelease(T** ppT)
{
    if (*ppT) {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

#ifdef _DEBUG
#define DBG_TIMING(x,l,y) { DWORD start = timeGetTime(); y; DWORD end = timeGetTime(); if(end-start>l) DbgLog((LOG_CUSTOM5, 10, L"TIMING: %S took %u ms", x, end-start)); }
extern void DbgSetLogFile(LPCTSTR szLogFile);
extern void DbgSetLogFileDesktop(LPCTSTR szLogFile);
extern void DbgCloseLogFile();
#else
#define DBG_TIMING(x,l,y) y;
#define DbgSetLogFile(sz)
#define DbgSetLogFileDesktop(sz)
#define DbgCloseLogFile()
#endif


// SAFE_ARRAY_DELETE macro.
// Deletes an array allocated with new [].

#ifndef SAFE_ARRAY_DELETE
#define SAFE_ARRAY_DELETE(x) if (x) { delete [] x; x = nullptr; }
#endif

// some common macros
#ifndef SAFE_DELETE
#define SAFE_DELETE(pPtr) { delete pPtr; pPtr = nullptr; }
#endif

#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p) { if (p) { delete [] (p);  (p) = nullptr; } }
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = nullptr; } }
#endif

#ifndef SAFE_CO_FREE
#define SAFE_CO_FREE(pPtr) { CoTaskMemFree(pPtr); pPtr = nullptr; }
#endif

#ifndef SAFE_CLOSE_HANDLE
#define SAFE_CLOSE_HANDLE(p) { if (p) { if ((p) != INVALID_HANDLE_VALUE) CloseHandle(p); (p) = nullptr; } }
#endif

#ifndef StrRes
#define StrRes(id) MAKEINTRESOURCE((id))
#endif

#ifndef ResStr
#define ResStr(id) CString(StrRes((id)))
#endif

#ifndef CHECK_HR
#define CHECK_HR(hr) if (FAILED(hr)) { goto done; }
#endif

#ifndef QI
#define QI(i) (riid == __uuidof(i)) ? GetInterface((i*)this, ppv) :
#endif

#ifndef QI2
#define QI2(i) (riid == IID_##i) ? GetInterface((i*)this, ppv) :
#endif

#ifndef countof
#define countof( array ) ( sizeof( array )/sizeof( array[0] ) )
#endif

inline void ws2s(const std::wstring& ws, OUT std::string& s)
{
    s = (char*)_bstr_t(ws.c_str());
}

inline void s2ws(const std::string& s, OUT std::wstring& ws)
{
    ws = (wchar_t*)_bstr_t(s.c_str());
}

extern int  CountPins(IBaseFilter* pBF, int& nIn, int& nOut, int& nInC, int& nOutC);
extern bool IsSplitter(IBaseFilter* pBF, bool fCountConnectedOnly = false);
extern bool IsMultiplexer(IBaseFilter* pBF, bool fCountConnectedOnly = false);
extern bool IsStreamStart(IBaseFilter* pBF);
extern bool IsStreamEnd(IBaseFilter* pBF);
extern bool IsVideoRenderer(IBaseFilter* pBF);
extern IBaseFilter* GetUpStreamFilter(IBaseFilter* pBF, IPin* pInputPin = nullptr);
extern IPin* GetUpStreamPin(IBaseFilter* pBF, IPin* pInputPin = nullptr);
extern IPin* GetFirstPin(IBaseFilter* pBF, PIN_DIRECTION dir = PINDIR_INPUT);
extern IPin* GetFirstDisconnectedPin(IBaseFilter* pBF, PIN_DIRECTION dir);
extern void  NukeDownstream(IBaseFilter* pBF, IFilterGraph* pFG);
extern void  NukeDownstream(IPin* pPin, IFilterGraph* pFG);
extern IPin* FindPin(IBaseFilter* pBF, PIN_DIRECTION direction, const AM_MEDIA_TYPE* pRequestedMT);
extern CStringW GetFilterName(IBaseFilter* pBF);
extern CStringW GetPinName(IPin* pPin);
extern IFilterGraph* GetGraphFromFilter(IBaseFilter* pBF);
extern IBaseFilter*  GetFilterFromPinOld(IPin* pPin);
extern void  ExtractMediaTypes(IPin* pPin, CAtlArray<GUID>& types);
extern void  ExtractMediaTypes(IPin* pPin, CAtlList<CMediaType>& mts);
extern void  ShowPPage(CString DisplayName, HWND hParentWnd);
extern void  ShowPPage(IUnknown* pUnknown, HWND hParentWnd);
extern CLSID GetCLSID(IBaseFilter* pBF);
extern CLSID GetCLSID(IPin* pPin);
extern bool  IsCLSIDRegistered(LPCTSTR clsid);
extern bool  IsCLSIDRegistered(const CLSID& clsid);
extern CString GetFilterPath(LPCTSTR clsid);
extern CString GetFilterPath(const CLSID& clsid);
extern void  CStringToBin(CString str, CAtlArray<BYTE>& data);
extern CString BinToCString(const BYTE* ptr, size_t len);
extern void FindFiles(CString fn, CAtlList<CString>& files);
enum OpticalDiskType_t {
    OpticalDisk_NotFound,
    OpticalDisk_Audio,
    OpticalDisk_VideoCD,
    OpticalDisk_DVDVideo,
    OpticalDisk_BD,
    OpticalDisk_Unknown
};
extern OpticalDiskType_t GetOpticalDiskType(TCHAR drive, CAtlList<CString>& files);
extern CString GetDriveLabel(TCHAR drive);
extern CString GetDriveLabel(CPath path);
extern DVD_HMSF_TIMECODE RT2HMSF(REFERENCE_TIME rt, double fps = 0.0); // used to remember the current position
extern DVD_HMSF_TIMECODE RT2HMS_r(REFERENCE_TIME rt);                  // used only to display information with rounding to nearest second
extern REFERENCE_TIME HMSF2RT(DVD_HMSF_TIMECODE hmsf, double fps = -1.0);
extern void memsetd(void* dst, unsigned int c, size_t nbytes);
extern void memsetw(void* dst, unsigned short c, size_t nbytes);
extern bool ExtractBIH(const AM_MEDIA_TYPE* pmt, BITMAPINFOHEADER* bih);
extern bool ExtractBIH(IMediaSample* pMS, BITMAPINFOHEADER* bih);
extern bool ExtractAvgTimePerFrame(const AM_MEDIA_TYPE* pmt, REFERENCE_TIME& rtAvgTimePerFrame);
extern bool ExtractDim(const AM_MEDIA_TYPE* pmt, int& w, int& h, int& arx, int& ary);
extern bool CreateFilter(CStringW DisplayName, IBaseFilter** ppBF, CStringW& FriendlyName);
extern CStringW GetFriendlyName(CStringW DisplayName);
extern HRESULT LoadExternalObject(LPCTSTR path, REFCLSID clsid, REFIID iid, void** ppv);
extern HRESULT LoadExternalFilter(LPCTSTR path, REFCLSID clsid, IBaseFilter** ppBF);
extern HRESULT LoadExternalPropertyPage(IPersist* pP, REFCLSID clsid, IPropertyPage** ppPP);
extern bool UnloadUnusedExternalObjects();
extern CString MakeFullPath(LPCTSTR path);
extern CString GetMediaTypeName(const GUID& guid);
extern GUID GUIDFromCString(CString str);
extern HRESULT GUIDFromCString(CString str, GUID& guid);
extern CString CStringFromGUID(const GUID& guid);
extern CStringW UTF8To16(LPCSTR utf8);
extern CStringA UTF16To8(LPCWSTR utf16);
extern CStringW UTF8ToStringW(const char* S);
extern CStringW LocalToStringW(const char* S);
extern bool DeleteRegKey(LPCTSTR pszKey, LPCTSTR pszSubkey);
extern bool SetRegKeyValue(LPCTSTR pszKey, LPCTSTR pszSubkey, LPCTSTR pszValueName, LPCTSTR pszValue);
extern bool SetRegKeyValue(LPCTSTR pszKey, LPCTSTR pszSubkey, LPCTSTR pszValue);
extern void RegisterSourceFilter(const CLSID& clsid, const GUID& subtype2, LPCTSTR chkbytes, LPCTSTR ext = nullptr, ...);
extern void RegisterSourceFilter(const CLSID& clsid, const GUID& subtype2, const CAtlList<CString>& chkbytes, LPCTSTR ext = nullptr, ...);
extern void UnRegisterSourceFilter(const GUID& subtype);
extern CString ReftimeToString(const REFERENCE_TIME& rtVal);
extern CString ReftimeToString2(const REFERENCE_TIME& rtVal);
extern CString DVDtimeToString(const DVD_HMSF_TIMECODE& rtVal, bool bAlwaysShowHours = false);
extern REFERENCE_TIME StringToReftime(LPCTSTR strVal);
extern void SetThreadName(DWORD dwThreadID, LPCSTR szThreadName);
extern CString FindCoverArt(const CString& path, const CString& author);

enum FF_FIELD_TYPE {
    PICT_NONE,
    PICT_TOP_FIELD,
    PICT_BOTTOM_FIELD,
    PICT_FRAME
};

class CPinInfo : public PIN_INFO
{
public:
    CPinInfo() {
        pFilter = nullptr;
    }
    ~CPinInfo() {
        if (pFilter) {
            pFilter->Release();
        }
    }
};

class CFilterInfo : public FILTER_INFO
{
public:
    CFilterInfo() {
        pGraph = nullptr;
    }
    ~CFilterInfo() {
        if (pGraph) {
            pGraph->Release();
        }
    }
};

#define BeginEnumFilters(pFilterGraph, pEnumFilters, pBaseFilter)                                                      \
{                                                                                                                      \
    CComPtr<IEnumFilters> pEnumFilters;                                                                                \
    if (pFilterGraph && SUCCEEDED(pFilterGraph->EnumFilters(&pEnumFilters))) {                                         \
        for (CComPtr<IBaseFilter> pBaseFilter; S_OK == pEnumFilters->Next(1, &pBaseFilter, 0); pBaseFilter = nullptr) {

#define EndEnumFilters }}}

#define BeginEnumCachedFilters(pGraphConfig, pEnumFilters, pBaseFilter)                                                \
{                                                                                                                      \
    CComPtr<IEnumFilters> pEnumFilters;                                                                                \
    if (pGraphConfig && SUCCEEDED(pGraphConfig->EnumCacheFilter(&pEnumFilters))) {                                     \
        for (CComPtr<IBaseFilter> pBaseFilter; S_OK == pEnumFilters->Next(1, &pBaseFilter, 0); pBaseFilter = nullptr) {

#define EndEnumCachedFilters }}}

#define BeginEnumPins(pBaseFilter, pEnumPins, pPin)                                     \
{                                                                                       \
    CComPtr<IEnumPins> pEnumPins;                                                       \
    if (pBaseFilter && SUCCEEDED(pBaseFilter->EnumPins(&pEnumPins))) {                  \
        for (CComPtr<IPin> pPin; S_OK == pEnumPins->Next(1, &pPin, 0); pPin = nullptr) {

#define EndEnumPins }}}

#define BeginEnumMediaTypes(pPin, pEnumMediaTypes, pMediaType)                                                             \
{                                                                                                                          \
    CComPtr<IEnumMediaTypes> pEnumMediaTypes;                                                                              \
    if (pPin && SUCCEEDED(pPin->EnumMediaTypes(&pEnumMediaTypes))) {                                                       \
        AM_MEDIA_TYPE* pMediaType = nullptr;                                                                               \
        for (; S_OK == pEnumMediaTypes->Next(1, &pMediaType, nullptr); DeleteMediaType(pMediaType), pMediaType = nullptr) {

#define EndEnumMediaTypes(pMediaType)                                                                               \
        }                                                                                                           \
        if (pMediaType) {                                                                                           \
            DeleteMediaType(pMediaType);                                                                            \
        }                                                                                                           \
    }                                                                                                               \
}

#define BeginEnumSysDev(clsid, pMoniker)                                                                            \
{                                                                                                                   \
    CComPtr<ICreateDevEnum> pDevEnum4$##clsid;                                                                      \
    pDevEnum4$##clsid.CoCreateInstance(CLSID_SystemDeviceEnum);                                                     \
    CComPtr<IEnumMoniker> pClassEnum4$##clsid;                                                                      \
    if (SUCCEEDED(pDevEnum4$##clsid->CreateClassEnumerator(clsid, &pClassEnum4$##clsid, 0))                         \
        && pClassEnum4$##clsid) {                                                                                   \
        for (CComPtr<IMoniker> pMoniker; pClassEnum4$##clsid->Next(1, &pMoniker, 0) == S_OK; pMoniker = nullptr) {

#define EndEnumSysDev }}}

#define PauseGraph                                                                                              \
    CComQIPtr<IMediaControl> _pMC(m_pGraph);                                                                    \
    OAFilterState _fs = -1;                                                                                     \
    if (_pMC)                                                                                                   \
        _pMC->GetState(1000, &_fs);                                                                             \
    if (_fs == State_Running)                                                                                   \
        _pMC->Pause();                                                                                          \
                                                                                                                \
    HRESULT _hr = E_FAIL;                                                                                       \
    CComQIPtr<IMediaSeeking> _pMS((IUnknown*)(INonDelegatingUnknown*)m_pGraph);                                 \
    REFERENCE_TIME _rtNow = 0;                                                                                  \
    if (_pMS)                                                                                                   \
        _hr = _pMS->GetCurrentPosition(&_rtNow);

#define ResumeGraph                                                                                             \
    if (SUCCEEDED(_hr) && _pMS && _fs != State_Stopped)                                                         \
        _hr = _pMS->SetPositions(&_rtNow, AM_SEEKING_AbsolutePositioning, nullptr, AM_SEEKING_NoPositioning);   \
                                                                                                                \
    if (_fs == State_Running && _pMS)                                                                           \
        _pMC->Run();

#define CallQueue(call)         \
    if (!m_pOutputQueue)        \
            return NOERROR;     \
    m_pOutputQueue->##call;     \
    return NOERROR;

#define UNREACHABLE_CODE()  \
    do {                    \
        ASSERT(false);      \
        __assume(false);    \
    } while (false)

template <class T>
static CUnknown* WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT* phr)
{
    *phr = S_OK;
    CUnknown* punk = new T(lpunk, phr);
    if (punk == nullptr) {
        *phr = E_OUTOFMEMORY;
    }
    return punk;
}

template <class T>
typename std::enable_if<std::is_unsigned<T>::value, T>::type GCD(T a, T b)
{
    static_assert(std::is_integral<T>::value, "GCD supports integral types only");
    if (a == 0 || b == 0) {
        return max(max(a, b), T(1));
    }
    while (a != b) {
        if (a < b) {
            b -= a;
        } else if (a > b) {
            a -= b;
        }
    }
    return a;
}

template <class T>
typename std::enable_if<std::is_signed<T>::value, T>::type GCD(T a, T b)
{
    using uT = typename std::make_unsigned<T>::type;

    return T(GCD(uT(std::abs(a)), uT(std::abs(b))));
}

template<class T>
constexpr typename std::enable_if<std::is_integral<T>::value, bool>::type IsEqual(T a, T b)
{
    return a == b;
}

template<class T>
constexpr typename std::enable_if<std::is_floating_point<T>::value, bool>::type IsEqual(T a, T b)
{
    return std::abs(a - b) < std::numeric_limits<T>::epsilon();
}

template<class T>
constexpr typename std::enable_if<std::is_floating_point<T>::value, bool>::type IsNearlyEqual(T a, T b, T epsilon)
{
    return std::abs(a - b) < epsilon;
}

template <class T>
constexpr typename std::enable_if < std::is_integral<T>::value&& std::is_unsigned<T>::value, int >::type SGN(T n)
{
    return T(0) < n;
}

template <typename T>
constexpr typename std::enable_if < std::is_integral<T>::value&& std::is_signed<T>::value, int >::type SGN(T n)
{
    return (T(0) < n) - (n < T(0));
}

template <typename T>
constexpr typename std::enable_if <std::is_floating_point<T>::value, int>::type SGN(T n)
{
    return IsEqual(n, T(0)) ? 0 : (n > 0 ? 1 : -1);
}

extern void SetThreadName(DWORD dwThreadID, LPCSTR szThreadName);

void split(const std::string& text, const std::string& separators, std::list<std::string>& words);

// Filter Registration
extern void RegisterSourceFilter(const CLSID& clsid, const GUID& subtype2, LPCWSTR chkbytes, ...);
extern void RegisterSourceFilter(const CLSID& clsid, const GUID& subtype2, std::list<LPCWSTR> chkbytes, ...);
extern void UnRegisterSourceFilter(const GUID& subtype);

extern void RegisterProtocolSourceFilter(const CLSID& clsid, LPCWSTR protocol);
extern void UnRegisterProtocolSourceFilter(LPCWSTR protocol);

extern BOOL CheckApplicationBlackList(LPCTSTR subkey);

// Locale
extern std::string ISO6391ToLanguage(LPCSTR code);
extern std::string ISO6392ToLanguage(LPCSTR code);
extern std::string ProbeLangForLanguage(LPCSTR code);
extern LCID ISO6391ToLcid(LPCSTR code);
extern LCID ISO6392ToLcid(LPCSTR code);
extern LCID ProbeLangForLCID(LPCSTR code);
extern std::string ISO6391To6392(LPCSTR code);
extern std::string ISO6392To6391(LPCSTR code);
extern std::string ProbeForISO6392(LPCSTR lang);

// FilterGraphUtils
extern IBaseFilter* FindFilter(const GUID& clsid, IFilterGraph* pFG);
extern BOOL FilterInGraph(const GUID& clsid, IFilterGraph* pFG);
extern BOOL FilterInGraphWithInputSubtype(const GUID& clsid, IFilterGraph* pFG, const GUID& clsidSubtype);
extern IBaseFilter* GetFilterFromPin(IPin* pPin);
extern HRESULT NukeDownstream(IFilterGraph* pGraph, IPin* pPin);
extern HRESULT NukeDownstream(IFilterGraph* pGraph, IBaseFilter* pFilter);
extern HRESULT FindIntefaceInGraph(IPin* pPin, REFIID refiid, void** pUnknown);
extern HRESULT FindPinIntefaceInGraph(IPin* pPin, REFIID refiid, void** pUnknown);
extern HRESULT FindFilterSafe(IPin* pPin, const GUID& guid, IBaseFilter** ppFilter, BOOL bReverse = FALSE);
extern BOOL FilterInGraphSafe(IPin* pPin, const GUID& guid, BOOL bReverse = FALSE);
extern BOOL HasSourceWithType(IPin* pPin, const GUID& mediaType);

std::wstring WStringFromGUID(const GUID& guid);
BSTR ConvertCharToBSTR(const char* sz);

int SafeMultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);
int SafeWideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar);
LPWSTR CoTaskGetWideCharFromMultiByte(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte);
LPSTR CoTaskGetMultiByteFromWideChar(UINT CodePage, DWORD dwFlags, LPCWSTR lpMultiByteStr, int cbMultiByte);

unsigned int lav_xiphlacing(unsigned char* s, unsigned int v);

void videoFormatTypeHandler(const AM_MEDIA_TYPE& mt, BITMAPINFOHEADER** pBMI = nullptr, REFERENCE_TIME* prtAvgTime = nullptr, DWORD* pDwAspectX = nullptr, DWORD* pDwAspectY = nullptr);
void videoFormatTypeHandler(const BYTE* format, const GUID* formattype, BITMAPINFOHEADER** pBMI = nullptr, REFERENCE_TIME* prtAvgTime = nullptr, DWORD* pDwAspectX = nullptr, DWORD* pDwAspectY = nullptr);
void audioFormatTypeHandler(const BYTE* format, const GUID* formattype, DWORD* pnSamples, WORD* pnChannels, WORD* pnBitsPerSample, WORD* pnBlockAlign, DWORD* pnBytesPerSec);
void getExtraData(const AM_MEDIA_TYPE& mt, BYTE* extra, size_t* extralen);
void getExtraData(const BYTE* format, const GUID* formattype, const size_t formatlen, BYTE* extra, size_t* extralen);

struct AVPacket;
struct MediaSideDataFFMpeg;
void CopyMediaSideDataFF(AVPacket* dst, const MediaSideDataFFMpeg** sd);

void __cdecl debugprintf(LPCWSTR format, ...);

HRESULT SetH265VideoType(AM_MEDIA_TYPE& mt, long width, long height, double fps);