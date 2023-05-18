/*
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
#include "stdafx.h"

#include "winddk/devioctl.h"
#include "winddk/ntddcdrm.h"

#include "DSUtil.h"
#include "Mpeg2Def.h"
#include "IMediaSideDataFFmpeg.h"

#include <initguid.h>
#include "moreuuids.h"
#include <dxva.h>
#include <dxva2api.h>

int CountPins(IBaseFilter* pBF, int& nIn, int& nOut, int& nInC, int& nOutC)
{
    nIn = nOut = 0;
    nInC = nOutC = 0;

    BeginEnumPins(pBF, pEP, pPin) {
        PIN_DIRECTION dir;
        if (SUCCEEDED(pPin->QueryDirection(&dir))) {
            CComPtr<IPin> pPinConnectedTo;
            pPin->ConnectedTo(&pPinConnectedTo);

            if (dir == PINDIR_INPUT) {
                nIn++;
                if (pPinConnectedTo) {
                    nInC++;
                }
            } else if (dir == PINDIR_OUTPUT) {
                nOut++;
                if (pPinConnectedTo) {
                    nOutC++;
                }
            }
        }
    }
    EndEnumPins;

    return (nIn + nOut);
}

bool IsSplitter(IBaseFilter* pBF, bool fCountConnectedOnly)
{
    int nIn, nOut, nInC, nOutC;
    CountPins(pBF, nIn, nOut, nInC, nOutC);
    return (fCountConnectedOnly ? nOutC > 1 : nOut > 1);
}

bool IsMultiplexer(IBaseFilter* pBF, bool fCountConnectedOnly)
{
    int nIn, nOut, nInC, nOutC;
    CountPins(pBF, nIn, nOut, nInC, nOutC);
    return (fCountConnectedOnly ? nInC > 1 : nIn > 1);
}

bool IsStreamStart(IBaseFilter* pBF)
{
    CComQIPtr<IAMFilterMiscFlags> pAMMF(pBF);
    if (pAMMF && pAMMF->GetMiscFlags()&AM_FILTER_MISC_FLAGS_IS_SOURCE) {
        return true;
    }

    int nIn, nOut, nInC, nOutC;
    CountPins(pBF, nIn, nOut, nInC, nOutC);
    AM_MEDIA_TYPE mt;
    CComPtr<IPin> pIn = GetFirstPin(pBF);
    return ((nOut > 1)
            || (nOut > 0 && nIn == 1 && pIn && SUCCEEDED(pIn->ConnectionMediaType(&mt)) && mt.majortype == MEDIATYPE_Stream));
}

bool IsStreamEnd(IBaseFilter* pBF)
{
    int nIn, nOut, nInC, nOutC;
    CountPins(pBF, nIn, nOut, nInC, nOutC);
    return (nOut == 0);
}

bool IsVideoRenderer(IBaseFilter* pBF)
{
    int nIn, nOut, nInC, nOutC;
    CountPins(pBF, nIn, nOut, nInC, nOutC);

    if (nInC > 0 && nOut == 0) {
        BeginEnumPins(pBF, pEP, pPin) {
            AM_MEDIA_TYPE mt;
            if (S_OK != pPin->ConnectionMediaType(&mt)) {
                continue;
            }

            FreeMediaType(mt);

            return !!(mt.majortype == MEDIATYPE_Video);
            /*&& (mt.formattype == FORMAT_VideoInfo || mt.formattype == FORMAT_VideoInfo2));*/
        }
        EndEnumPins;
    }

    CLSID clsid;
    memcpy(&clsid, &GUID_NULL, sizeof(clsid));
    pBF->GetClassID(&clsid);

    return (clsid == CLSID_VideoRenderer || clsid == CLSID_VideoRendererDefault);
}

IBaseFilter* GetUpStreamFilter(IBaseFilter* pBF, IPin* pInputPin)
{
    return GetFilterFromPinOld(GetUpStreamPin(pBF, pInputPin));
}

IPin* GetUpStreamPin(IBaseFilter* pBF, IPin* pInputPin)
{
    BeginEnumPins(pBF, pEP, pPin) {
        if (pInputPin && pInputPin != pPin) {
            continue;
        }

        PIN_DIRECTION dir;
        CComPtr<IPin> pPinConnectedTo;
        if (SUCCEEDED(pPin->QueryDirection(&dir)) && dir == PINDIR_INPUT
                && SUCCEEDED(pPin->ConnectedTo(&pPinConnectedTo))) {
            IPin* pRet = pPinConnectedTo.Detach();
            pRet->Release();
            return pRet;
        }
    }
    EndEnumPins;

    return nullptr;
}

IPin* GetFirstPin(IBaseFilter* pBF, PIN_DIRECTION dir)
{
    if (pBF) {
        BeginEnumPins(pBF, pEP, pPin) {
            PIN_DIRECTION dir2;
            if (SUCCEEDED(pPin->QueryDirection(&dir2)) && dir == dir2) {
                IPin* pRet = pPin.Detach();
                pRet->Release();
                return pRet;
            }
        }
        EndEnumPins;
    }

    return nullptr;
}

IPin* GetFirstDisconnectedPin(IBaseFilter* pBF, PIN_DIRECTION dir)
{
    if (pBF) {
        BeginEnumPins(pBF, pEP, pPin) {
            PIN_DIRECTION dir2;
            CComPtr<IPin> pPinTo;
            if (SUCCEEDED(pPin->QueryDirection(&dir2)) && dir == dir2 && (S_OK != pPin->ConnectedTo(&pPinTo))) {
                IPin* pRet = pPin.Detach();
                pRet->Release();
                return pRet;
            }
        }
        EndEnumPins;
    }

    return nullptr;
}

IPin* FindPin(IBaseFilter* pBF, PIN_DIRECTION direction, const AM_MEDIA_TYPE* pRequestedMT)
{
    PIN_DIRECTION pindir;
    BeginEnumPins(pBF, pEP, pPin) {
        CComPtr<IPin> pFellow;

        if (SUCCEEDED(pPin->QueryDirection(&pindir)) &&
                pindir == direction &&
                pPin->ConnectedTo(&pFellow) == VFW_E_NOT_CONNECTED) {
            BeginEnumMediaTypes(pPin, pEM, pmt) {
                if (pmt->majortype == pRequestedMT->majortype && pmt->subtype == pRequestedMT->subtype) {
                    return (pPin);
                }
            }
            EndEnumMediaTypes(pmt);
        }
    }
    EndEnumPins;
    return nullptr;
}

CStringW GetFilterName(IBaseFilter* pBF)
{
    CStringW name = _T("");

    if (pBF) {
        CFilterInfo fi;
        if (SUCCEEDED(pBF->QueryFilterInfo(&fi))) {
            name = fi.achName;
        }
    }

    return name;
}

CStringW GetPinName(IPin* pPin)
{
    CStringW name;
    CPinInfo pi;
    if (pPin && SUCCEEDED(pPin->QueryPinInfo(&pi))) {
        name = pi.achName;
    }

    return name;
}

IFilterGraph* GetGraphFromFilter(IBaseFilter* pBF)
{
    if (!pBF) {
        return nullptr;
    }
    IFilterGraph* pGraph = nullptr;
    CFilterInfo fi;
    if (pBF && SUCCEEDED(pBF->QueryFilterInfo(&fi))) {
        pGraph = fi.pGraph;
    }
    return pGraph;
}

IBaseFilter* GetFilterFromPinOld(IPin* pPin)
{
    if (!pPin) {
        return nullptr;
    }
    IBaseFilter* pBF = nullptr;
    CPinInfo pi;
    if (pPin && SUCCEEDED(pPin->QueryPinInfo(&pi))) {
        pBF = pi.pFilter;
    }
    return pBF;
}

void ExtractMediaTypes(IPin* pPin, CAtlArray<GUID>& types)
{
    types.RemoveAll();

    BeginEnumMediaTypes(pPin, pEM, pmt) {
        bool fFound = false;

        for (ptrdiff_t i = 0; !fFound && i < (int)types.GetCount(); i += 2) {
            if (types[i] == pmt->majortype && types[i + 1] == pmt->subtype) {
                fFound = true;
            }
        }

        if (!fFound) {
            types.Add(pmt->majortype);
            types.Add(pmt->subtype);
        }
    }
    EndEnumMediaTypes(pmt);
}

void ExtractMediaTypes(IPin* pPin, CAtlList<CMediaType>& mts)
{
    mts.RemoveAll();

    BeginEnumMediaTypes(pPin, pEM, pmt) {
        bool fFound = false;

        POSITION pos = mts.GetHeadPosition();
        while (!fFound && pos) {
            CMediaType& mt = mts.GetNext(pos);
            if (mt.majortype == pmt->majortype && mt.subtype == pmt->subtype) {
                fFound = true;
            }
        }

        if (!fFound) {
            mts.AddTail(CMediaType(*pmt));
        }
    }
    EndEnumMediaTypes(pmt);
}

int Eval_Exception(int n_except)
{
    if (n_except == STATUS_ACCESS_VIOLATION) {
        MessageBox(NULL, _T("DSUtil"), _T("The property page of this filter has just caused a\nmemory access violation. The application will gently die now :)"), MB_OK);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

void MyOleCreatePropertyFrame(HWND hwndOwner, UINT x, UINT y, LPCOLESTR lpszCaption, ULONG cObjects, LPUNKNOWN FAR* lplpUnk, ULONG cPages, LPCLSID lpPageClsID, LCID lcid, DWORD dwReserved, LPVOID lpvReserved)
{
    __try {
        OleCreatePropertyFrame(hwndOwner, x, y, lpszCaption, cObjects, lplpUnk, cPages, lpPageClsID, lcid, dwReserved, lpvReserved);
    } __except (Eval_Exception(GetExceptionCode())) {
        // No code; this block never executed.
    }
}

void ShowPPage(CString DisplayName, HWND hParentWnd)
{
    CComPtr<IBindCtx> pBindCtx;
    CreateBindCtx(0, &pBindCtx);

    CComPtr<IMoniker> pMoniker;
    ULONG chEaten;
    if (S_OK != MkParseDisplayName(pBindCtx, CStringW(DisplayName), &chEaten, &pMoniker)) {
        return;
    }

    CComPtr<IBaseFilter> pBF;
    if (FAILED(pMoniker->BindToObject(pBindCtx, 0, IID_PPV_ARGS(&pBF))) || !pBF) {
        return;
    }

    ShowPPage(pBF, hParentWnd);
}

void ShowPPage(IUnknown* pUnk, HWND hParentWnd)
{
    CComQIPtr<ISpecifyPropertyPages> pSPP = pUnk;
    if (!pSPP) {
        return;
    }

    CString str;

    CComQIPtr<IBaseFilter> pBF(pSPP);
    CFilterInfo fi;
    CComQIPtr<IPin> pPin(pSPP);
    CPinInfo pi;
    if (pBF && SUCCEEDED(pBF->QueryFilterInfo(&fi))) {
        str = fi.achName;
    } else if (pPin && SUCCEEDED(pPin->QueryPinInfo(&pi))) {
        str = pi.achName;
    }

    CAUUID caGUID;
    caGUID.pElems = nullptr;
    if (SUCCEEDED(pSPP->GetPages(&caGUID))) {
        IUnknown* lpUnk = nullptr;
        pSPP.QueryInterface(&lpUnk);
        MyOleCreatePropertyFrame(
            hParentWnd, 0, 0, CStringW(str),
            1, (IUnknown**)&lpUnk,
            caGUID.cElems, caGUID.pElems,
            0, 0, nullptr);
        lpUnk->Release();

        if (caGUID.pElems) {
            CoTaskMemFree(caGUID.pElems);
        }
    }
}

CLSID GetCLSID(IBaseFilter* pBF)
{
    CLSID clsid = GUID_NULL;
    if (pBF) {
        pBF->GetClassID(&clsid);
    }
    return clsid;
}

CLSID GetCLSID(IPin* pPin)
{
    return GetCLSID(GetFilterFromPinOld(pPin));
}

bool IsCLSIDRegistered(LPCTSTR clsid)
{
    CString rootkey1(_T("CLSID\\"));
    CString rootkey2(_T("CLSID\\{083863F1-70DE-11d0-BD40-00A0C911CE86}\\Instance\\"));

    return ERROR_SUCCESS == CRegKey().Open(HKEY_CLASSES_ROOT, rootkey1 + clsid, KEY_READ)
           || ERROR_SUCCESS == CRegKey().Open(HKEY_CLASSES_ROOT, rootkey2 + clsid, KEY_READ);
}

bool IsCLSIDRegistered(const CLSID& clsid)
{
    bool fRet = false;

    CComHeapPtr<OLECHAR> pStr;
    if (S_OK == StringFromCLSID(clsid, &pStr) && pStr) {
        fRet = IsCLSIDRegistered(CString(pStr));
    }

    return fRet;
}

CString GetFilterPath(LPCTSTR clsid)
{
    CString rootkey1(_T("CLSID\\"));
    CString rootkey2(_T("CLSID\\{083863F1-70DE-11d0-BD40-00A0C911CE86}\\Instance\\"));

    CRegKey key;
    CString path;

    if (ERROR_SUCCESS == key.Open(HKEY_CLASSES_ROOT, rootkey1 + clsid + _T("\\InprocServer32"), KEY_READ)
            || ERROR_SUCCESS == key.Open(HKEY_CLASSES_ROOT, rootkey2 + clsid + _T("\\InprocServer32"), KEY_READ)) {
        ULONG nCount = MAX_PATH;
        key.QueryStringValue(nullptr, path.GetBuffer(nCount), &nCount);
        path.ReleaseBuffer(nCount);
    }

    return path;
}

CString GetFilterPath(const CLSID& clsid)
{
    CString path;

    CComHeapPtr<OLECHAR> pStr;
    if (S_OK == StringFromCLSID(clsid, &pStr) && pStr) {
        path = GetFilterPath(CString(pStr));
    }

    return path;
}

void CStringToBin(CString str, CAtlArray<BYTE>& data)
{
    str.Trim();
    ASSERT((str.GetLength() & 1) == 0);
    data.SetCount(str.GetLength() / 2);

    BYTE b = 0;

    str.MakeUpper();
    for (int i = 0, j = str.GetLength(); i < j; i++) {
        TCHAR c = str[i];
        if (c >= _T('0') && c <= _T('9')) {
            if (!(i & 1)) {
                b = ((char(c - _T('0')) << 4) & 0xf0) | (b & 0x0f);
            } else {
                b = (char(c - _T('0')) & 0x0f) | (b & 0xf0);
            }
        } else if (c >= _T('A') && c <= _T('F')) {
            if (!(i & 1)) {
                b = ((char(c - _T('A') + 10) << 4) & 0xf0) | (b & 0x0f);
            } else {
                b = (char(c - _T('A') + 10) & 0x0f) | (b & 0xf0);
            }
        } else {
            break;
        }

        if (i & 1) {
            data[i >> 1] = b;
            b = 0;
        }
    }
}

CString BinToCString(const BYTE* ptr, size_t len)
{
    CString ret;

    while (len-- > 0) {
        TCHAR high, low;

        high = (*ptr >> 4) >= 10 ? (*ptr >> 4) - 10 + _T('A') : (*ptr >> 4) + _T('0');
        low = (*ptr & 0xf) >= 10 ? (*ptr & 0xf) - 10 + _T('A') : (*ptr & 0xf) + _T('0');

        ret.AppendFormat(_T("%c%c"), high, low);

        ptr++;
    }

    return ret;
}

void FindFiles(CString fn, CAtlList<CString>& files)
{
    CString path = fn;
    path.Replace('/', '\\');
    path = path.Left(path.ReverseFind('\\') + 1);

    WIN32_FIND_DATA findData;
    HANDLE h = FindFirstFile(fn, &findData);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            files.AddTail(path + findData.cFileName);
        } while (FindNextFile(h, &findData));

        FindClose(h);
    }
}

OpticalDiskType_t GetOpticalDiskType(TCHAR drive, CAtlList<CString>& files)
{
    files.RemoveAll();

    CString path;
    path.Format(_T("%c:"), drive);

    if (GetDriveType(path + _T("\\")) == DRIVE_CDROM) {
        // CDROM_DVDVideo
        FindFiles(path + _T("\\VIDEO_TS\\video_ts.ifo"), files);
        if (!files.IsEmpty()) {
            return OpticalDisk_DVDVideo;
        }

        // CDROM_BD
        FindFiles(path + _T("\\BDMV\\index.bdmv"), files);
        if (!files.IsEmpty()) {
            return OpticalDisk_BD;
        }

        // CDROM_VideoCD
        FindFiles(path + _T("\\mpegav\\avseq??.dat"), files);
        FindFiles(path + _T("\\mpegav\\avseq??.mpg"), files);
        FindFiles(path + _T("\\mpeg2\\avseq??.dat"), files);
        FindFiles(path + _T("\\mpeg2\\avseq??.mpg"), files);
        FindFiles(path + _T("\\mpegav\\music??.dat"), files);
        FindFiles(path + _T("\\mpegav\\music??.mpg"), files);
        FindFiles(path + _T("\\mpeg2\\music??.dat"), files);
        FindFiles(path + _T("\\mpeg2\\music??.mpg"), files);
        if (!files.IsEmpty()) {
            return OpticalDisk_VideoCD;
        }

        // CDROM_Audio
        HANDLE hDrive = CreateFile(CString(_T("\\\\.\\")) + path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, (HANDLE)nullptr);
        if (hDrive != INVALID_HANDLE_VALUE) {
            DWORD BytesReturned;
            CDROM_TOC TOC;
            if (DeviceIoControl(hDrive, IOCTL_CDROM_READ_TOC, nullptr, 0, &TOC, sizeof(TOC), &BytesReturned, 0)) {
                ASSERT(TOC.FirstTrack >= 1u && TOC.LastTrack <= _countof(TOC.TrackData));
                TOC.FirstTrack = max(TOC.FirstTrack, UCHAR(1));
                TOC.LastTrack = min(TOC.LastTrack, UCHAR(_countof(TOC.TrackData)));
                for (ptrdiff_t i = TOC.FirstTrack; i <= TOC.LastTrack; i++) {
                    // MMC-3 Draft Revision 10g: Table 222 - Q Sub-channel control field
                    auto& trackData = TOC.TrackData[i - 1];
                    trackData.Control &= 5;
                    if (trackData.Control == 0 || trackData.Control == 1) {
                        CString fn;
                        fn.Format(_T("%s\\track%02Id.cda"), path.GetString(), i);
                        files.AddTail(fn);
                    }
                }
            }

            CloseHandle(hDrive);
        }
        if (!files.IsEmpty()) {
            return OpticalDisk_Audio;
        }

        // it is a cdrom but nothing special
        return OpticalDisk_Unknown;
    }

    return OpticalDisk_NotFound;
}

CString GetDriveLabel(TCHAR drive)
{
    CString path;
    path.Format(_T("%c:\\"), drive);

    return GetDriveLabel(CPath(path));
}

CString GetDriveLabel(CPath path)
{
    CString label;
    path.StripToRoot();

    TCHAR VolumeNameBuffer[MAX_PATH], FileSystemNameBuffer[MAX_PATH];
    DWORD VolumeSerialNumber, MaximumComponentLength, FileSystemFlags;
    if (GetVolumeInformation(path,
                             VolumeNameBuffer, MAX_PATH, &VolumeSerialNumber, &MaximumComponentLength,
                             &FileSystemFlags, FileSystemNameBuffer, MAX_PATH)) {
        label = VolumeNameBuffer;
    }

    return label;
}

DVD_HMSF_TIMECODE RT2HMSF(REFERENCE_TIME rt, double fps /*= 0.0*/) // use to remember the current position
{
    DVD_HMSF_TIMECODE hmsf = {
        (BYTE)((rt / 10000000 / 60 / 60)),
        (BYTE)((rt / 10000000 / 60) % 60),
        (BYTE)((rt / 10000000) % 60),
        (BYTE)(1.0 * ((rt / 10000) % 1000) * fps / 1000)
    };

    return hmsf;
}

DVD_HMSF_TIMECODE RT2HMS_r(REFERENCE_TIME rt) // used only for information (for display on the screen)
{
    rt = (rt + 5000000) / 10000000;
    DVD_HMSF_TIMECODE hmsf = {
        (BYTE)(rt / 3600),
        (BYTE)(rt / 60 % 60),
        (BYTE)(rt % 60),
        0
    };

    return hmsf;
}

REFERENCE_TIME HMSF2RT(DVD_HMSF_TIMECODE hmsf, double fps /*= -1.0*/)
{
    if (fps <= 0.0) {
        hmsf.bFrames = 0;
        fps = 1.0;
    }
    return (REFERENCE_TIME)((((REFERENCE_TIME)hmsf.bHours * 60 + hmsf.bMinutes) * 60 + hmsf.bSeconds) * 1000 + 1.0 * hmsf.bFrames * 1000 / fps) * 10000;
}

void memsetd(void* dst, unsigned int c, size_t nbytes)
{
    size_t n = nbytes / 4;
    __stosd((unsigned long*)dst, c, n);
}

void memsetw(void* dst, unsigned short c, size_t nbytes)
{
    memsetd(dst, c << 16 | c, nbytes);

    size_t n = nbytes / 2;
    size_t o = (n / 2) * 2;
    if ((n - o) == 1) {
        ((WORD*)dst)[o] = c;
    }
}

bool ExtractBIH(const AM_MEDIA_TYPE* pmt, BITMAPINFOHEADER* bih)
{
    if (pmt && bih) {
        ZeroMemory(bih, sizeof(*bih));
        ASSERT(pmt->formattype == FORMAT_VideoInfo2);
        {
            VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
            memcpy(bih, &vih2->bmiHeader, sizeof(BITMAPINFOHEADER));
            return true;
        }
    }

    return false;
}

bool ExtractAvgTimePerFrame(const AM_MEDIA_TYPE* pmt, REFERENCE_TIME& rtAvgTimePerFrame)
{
    ASSERT(pmt->formattype == FORMAT_VideoInfo2);
    rtAvgTimePerFrame = ((VIDEOINFOHEADER2*)pmt->pbFormat)->AvgTimePerFrame;
    return true;
}

bool ExtractBIH(IMediaSample* pMS, BITMAPINFOHEADER* bih)
{
    AM_MEDIA_TYPE* pmt = nullptr;
    if (SUCCEEDED(pMS->GetMediaType(&pmt)) && pmt) {
        bool fRet = ExtractBIH(pmt, bih);
        DeleteMediaType(pmt);
        return fRet;
    }

    return false;
}

bool ExtractDim(const AM_MEDIA_TYPE* pmt, int& w, int& h, int& arx, int& ary)
{
    w = h = arx = ary = 0;

    ASSERT(pmt->formattype == FORMAT_VideoInfo2);
    if (pmt->formattype == FORMAT_VideoInfo2) {
        VIDEOINFOHEADER2* vih = (VIDEOINFOHEADER2*)pmt->pbFormat;
        w = vih->bmiHeader.biWidth;
        h = abs(vih->bmiHeader.biHeight);
        arx = vih->dwPictAspectRatioX;
        ary = vih->dwPictAspectRatioY;
    } else {
        return false;
    }

    if (!arx || !ary) {
        BYTE* ptr = nullptr;
        DWORD len = 0;

        if (pmt->formattype == FORMAT_MPEGVideo) {
            ptr = ((MPEG1VIDEOINFO*)pmt->pbFormat)->bSequenceHeader;
            len = ((MPEG1VIDEOINFO*)pmt->pbFormat)->cbSequenceHeader;

            if (ptr && len >= 8 && *(DWORD*)ptr == 0xb3010000) {
                w = (ptr[4] << 4) | (ptr[5] >> 4);
                h = ((ptr[5] & 0xf) << 8) | ptr[6];
                float ar[] = {
                    1.0000f, 1.0000f, 0.6735f, 0.7031f,
                    0.7615f, 0.8055f, 0.8437f, 0.8935f,
                    0.9157f, 0.9815f, 1.0255f, 1.0695f,
                    1.0950f, 1.1575f, 1.2015f, 1.0000f,
                };
                arx = (int)((float)w / ar[ptr[7] >> 4] + 0.5);
                ary = h;
            }
        } else if (pmt->formattype == FORMAT_MPEG2_VIDEO) {
            ptr = (BYTE*)((MPEG2VIDEOINFO*)pmt->pbFormat)->dwSequenceHeader;
            len = ((MPEG2VIDEOINFO*)pmt->pbFormat)->cbSequenceHeader;

            if (ptr && len >= 8 && *(DWORD*)ptr == 0xb3010000) {
                w = (ptr[4] << 4) | (ptr[5] >> 4);
                h = ((ptr[5] & 0xf) << 8) | ptr[6];
                struct {
                    int x, y;
                } ar[] = {{w, h}, {4, 3}, {16, 9}, {221, 100}, {w, h}};
                int i = min(max(ptr[7] >> 4, 1), 5) - 1;
                arx = ar[i].x;
                ary = ar[i].y;
            }
        }
    }

    if (!arx || !ary) {
        arx = w;
        ary = h;
    }

    DWORD a = arx, b = ary;
    while (a) {
        int tmp = a;
        a = b % tmp;
        b = tmp;
    }
    if (b) {
        arx /= b, ary /= b;
    }

    return true;
}

bool CreateFilter(CStringW DisplayName, IBaseFilter** ppBF, CStringW& FriendlyName)
{
    if (!ppBF) {
        return false;
    }

    *ppBF = nullptr;
    FriendlyName.Empty();

    CComPtr<IBindCtx> pBindCtx;
    CreateBindCtx(0, &pBindCtx);

    CComPtr<IMoniker> pMoniker;
    ULONG chEaten;
    if (S_OK != MkParseDisplayName(pBindCtx, CComBSTR(DisplayName), &chEaten, &pMoniker)) {
        return false;
    }

    if (FAILED(pMoniker->BindToObject(pBindCtx, 0, IID_PPV_ARGS(ppBF))) || !*ppBF) {
        return false;
    }

    CComPtr<IPropertyBag> pPB;
    CComVariant var;
    if (SUCCEEDED(pMoniker->BindToStorage(pBindCtx, 0, IID_PPV_ARGS(&pPB)))
            && SUCCEEDED(pPB->Read(_T("FriendlyName"), &var, nullptr))) {
        FriendlyName = var.bstrVal;
    }

    return true;
}

CStringW GetFriendlyName(CStringW displayName)
{
    CStringW friendlyName;

    CComPtr<IBindCtx> pBindCtx;
    CreateBindCtx(0, &pBindCtx);

    CComPtr<IMoniker> pMoniker;
    ULONG chEaten;
    if (S_OK == MkParseDisplayName(pBindCtx, CComBSTR(displayName), &chEaten, &pMoniker)) {
        CComPtr<IPropertyBag> pPB;
        CComVariant var;
        if (SUCCEEDED(pMoniker->BindToStorage(pBindCtx, 0, IID_PPV_ARGS(&pPB)))
                && SUCCEEDED(pPB->Read(_T("FriendlyName"), &var, nullptr))) {
            friendlyName = var.bstrVal;
        }
    }

    return friendlyName;
}

typedef HRESULT(__stdcall* fDllCanUnloadNow)(void);

struct ExternalObject {
    CString path;
    HINSTANCE hInst;
    CLSID clsid;
    fDllCanUnloadNow fpDllCanUnloadNow;
    bool bUnloadOnNextCheck;
};

static CAtlList<ExternalObject> s_extObjs;
static CCritSec s_csExtObjs;

HRESULT LoadExternalObject(LPCTSTR path, REFCLSID clsid, REFIID iid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);

    CAutoLock lock(&s_csExtObjs);

    CString fullpath = MakeFullPath(path);

    HINSTANCE hInst = nullptr;
    bool fFound = false;

    POSITION pos = s_extObjs.GetHeadPosition();
    while (pos) {
        ExternalObject& eo = s_extObjs.GetNext(pos);
        if (!eo.path.CompareNoCase(fullpath)) {
            hInst = eo.hInst;
            fFound = true;
            eo.bUnloadOnNextCheck = false;
            break;
        }
    }

    HRESULT hr = E_FAIL;

    if (!hInst) {
        hInst = LoadLibraryEx(fullpath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    if (hInst) {
        typedef HRESULT(__stdcall * PDllGetClassObject)(REFCLSID rclsid, REFIID riid, LPVOID * ppv);
        PDllGetClassObject p = (PDllGetClassObject)GetProcAddress(hInst, "DllGetClassObject");

        if (p && FAILED(hr = p(clsid, iid, ppv))) {
            CComPtr<IClassFactory> pCF;
            if (SUCCEEDED(hr = p(clsid, IID_PPV_ARGS(&pCF)))) {
                hr = pCF->CreateInstance(nullptr, iid, ppv);
            }
        }
    }

    if (FAILED(hr) && hInst && !fFound) {
        FreeLibrary(hInst);
        return hr;
    }

    if (hInst && !fFound) {
        ExternalObject eo;
        eo.path = fullpath;
        eo.hInst = hInst;
        eo.clsid = clsid;
        eo.fpDllCanUnloadNow = (fDllCanUnloadNow)GetProcAddress(hInst, "DllCanUnloadNow");
        eo.bUnloadOnNextCheck = false;
        s_extObjs.AddTail(eo);
    }

    return hr;
}

HRESULT LoadExternalFilter(LPCTSTR path, REFCLSID clsid, IBaseFilter** ppBF)
{
    return LoadExternalObject(path, clsid, IID_PPV_ARGS(ppBF));
}

HRESULT LoadExternalPropertyPage(IPersist* pP, REFCLSID clsid, IPropertyPage** ppPP)
{
    CAutoLock lock(&s_csExtObjs);

    CLSID clsid2 = GUID_NULL;
    if (FAILED(pP->GetClassID(&clsid2))) {
        return E_FAIL;
    }

    POSITION pos = s_extObjs.GetHeadPosition();
    while (pos) {
        ExternalObject& eo = s_extObjs.GetNext(pos);
        if (eo.clsid == clsid2) {
            return LoadExternalObject(eo.path, clsid, IID_PPV_ARGS(ppPP));
        }
    }

    return E_FAIL;
}

bool UnloadUnusedExternalObjects()
{
    CAutoLock lock(&s_csExtObjs);

    POSITION pos = s_extObjs.GetHeadPosition(), currentPos;
    while (pos) {
        currentPos = pos;
        ExternalObject& eo = s_extObjs.GetNext(pos);

        if (eo.fpDllCanUnloadNow && eo.fpDllCanUnloadNow() == S_OK) {
            // Before actually unloading it, we require that the library reports
            // that it can be unloaded safely twice in a row with a 60s delay
            // between the two checks.
            if (eo.bUnloadOnNextCheck) {
                FreeLibrary(eo.hInst);
                s_extObjs.RemoveAt(currentPos);
            } else {
                eo.bUnloadOnNextCheck = true;
            }
        } else {
            eo.bUnloadOnNextCheck = false;
        }
    }

    return s_extObjs.IsEmpty();
}

CString MakeFullPath(LPCTSTR path)
{
    CString full(path);
    full.Replace('/', '\\');

    CString fn;
    fn.ReleaseBuffer(GetModuleFileName(NULL, fn.GetBuffer(MAX_PATH), MAX_PATH));
    CPath p(fn);

    if (full.GetLength() >= 2 && full[0] == '\\') {
        if (full[1] != '\\') {
            p.StripToRoot();
            full = CString(p) + full.Mid(1);
        }
    } else if (full.Find(_T(":\\")) < 0) {
        p.RemoveFileSpec();
        p.AddBackslash();
        full = CString(p) + full;
    }

    CPath c(full);
    c.Canonicalize();
    return CString(c);
}

//

CString GetMediaTypeName(const GUID& guid)
{
    CString ret = guid == GUID_NULL
                  ? CString(_T("Any type"))
                  : CString(GuidNames[guid]);

    if (ret == _T("FOURCC GUID")) {
        CString str;
        if (guid.Data1 >= 0x10000) {
            str.Format(_T("Video: %c%c%c%c"), (guid.Data1 >> 0) & 0xff, (guid.Data1 >> 8) & 0xff, (guid.Data1 >> 16) & 0xff, (guid.Data1 >> 24) & 0xff);
        } else {
            str.Format(_T("Audio: 0x%08x"), guid.Data1);
        }
        ret = str;
    } else if (ret == _T("Unknown GUID Name")) {
        WCHAR null[128] = {0}, buff[128];
        StringFromGUID2(GUID_NULL, null, _countof(null) - 1);
        ret = CString(CStringW(StringFromGUID2(guid, buff, _countof(buff) - 1) ? buff : null));
    }

    return ret;
}

GUID GUIDFromCString(CString str)
{
    GUID guid = GUID_NULL;
    HRESULT hr = CLSIDFromString(CComBSTR(str), &guid);
    ASSERT(SUCCEEDED(hr));
    UNREFERENCED_PARAMETER(hr);
    return guid;
}

HRESULT GUIDFromCString(CString str, GUID& guid)
{
    guid = GUID_NULL;
    return CLSIDFromString(CComBSTR(str), &guid);
}

CString CStringFromGUID(const GUID& guid)
{
    WCHAR null[128] = {0}, buff[128];
    StringFromGUID2(GUID_NULL, null, _countof(null) - 1);
    return CString(StringFromGUID2(guid, buff, _countof(buff) - 1) > 0 ? buff : null);
}

CStringW UTF8To16(LPCSTR utf8)
{
    CStringW str;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0) - 1;
    if (n < 0) {
        return str;
    }
    str.ReleaseBuffer(MultiByteToWideChar(CP_UTF8, 0, utf8, -1, str.GetBuffer(n), n + 1) - 1);
    return str;
}

CStringA UTF16To8(LPCWSTR utf16)
{
    CStringA str;
    int n = WideCharToMultiByte(CP_UTF8, 0, utf16, -1, nullptr, 0, nullptr, nullptr) - 1;
    if (n < 0) {
        return str;
    }
    str.ReleaseBuffer(WideCharToMultiByte(CP_UTF8, 0, utf16, -1, str.GetBuffer(n), n + 1, nullptr, nullptr) - 1);
    return str;
}

CStringW UTF8ToStringW(const char* S)
{
    CStringW str;
    if (S == nullptr) {
        return str;
    }

    // Don't use MultiByteToWideChar(), some characters are not well decoded
    const unsigned char* Z = (const unsigned char*)S;
    while (*Z) { //0 is end
        //1 byte
        if (*Z < 0x80) {
            str += (wchar_t)(*Z);
            Z++;
        }
        //2 bytes
        else if ((*Z & 0xE0) == 0xC0) {
            if ((*(Z + 1) & 0xC0) == 0x80) {
                str += (wchar_t)((((wchar_t)(*Z & 0x1F)) << 6) | (*(Z + 1) & 0x3F));
                Z += 2;
            } else {
                str.Empty();
                return str; //Bad character
            }
        }
        //3 bytes
        else if ((*Z & 0xF0) == 0xE0) {
            if ((*(Z + 1) & 0xC0) == 0x80 && (*(Z + 2) & 0xC0) == 0x80) {
                str += (wchar_t)((((wchar_t)(*Z & 0x0F)) << 12) | ((*(Z + 1) & 0x3F) << 6) | (*(Z + 2) & 0x3F));
                Z += 3;
            } else {
                str.Empty();
                return str; //Bad character
            }
        }
        //4 bytes
        else if ((*Z & 0xF8) == 0xF0) {
            if ((*(Z + 1) & 0xC0) == 0x80 && (*(Z + 2) & 0xC0) == 0x80 && (*(Z + 3) & 0xC0) == 0x80) {
                str += (wchar_t)((((wchar_t)(*Z & 0x0F)) << 18) | ((*(Z + 1) & 0x3F) << 12) || ((*(Z + 2) & 0x3F) << 6) | (*(Z + 3) & 0x3F));
                Z += 4;
            } else {
                str.Empty();
                return str; //Bad character
            }
        } else {
            str.Empty();
            return str; //Bad character
        }
    }
    return str;
}

CStringW LocalToStringW(const char* S)
{
    CStringW str;
    if (S == nullptr) {
        return str;
    }

    int Size = MultiByteToWideChar(CP_ACP, 0, S, -1, nullptr, 0);
    if (Size != 0) {
        str.ReleaseBuffer(MultiByteToWideChar(CP_ACP, 0, S, -1, str.GetBuffer(Size), Size + 1) - 1);
    }
    return str;
}

// filter registration helpers

bool DeleteRegKey(LPCTSTR pszKey, LPCTSTR pszSubkey)
{
    bool bOK = false;

    HKEY hKey;
    LONG ec = ::RegOpenKeyEx(HKEY_CLASSES_ROOT, pszKey, 0, KEY_ALL_ACCESS, &hKey);
    if (ec == ERROR_SUCCESS) {
        if (pszSubkey != 0) {
            ec = ::RegDeleteKey(hKey, pszSubkey);
        }

        bOK = (ec == ERROR_SUCCESS);

        ::RegCloseKey(hKey);
    }

    return bOK;
}

bool SetRegKeyValue(LPCTSTR pszKey, LPCTSTR pszSubkey, LPCTSTR pszValueName, LPCTSTR pszValue)
{
    bool bOK = false;

    CString szKey(pszKey);
    if (pszSubkey != 0) {
        szKey += CString(_T("\\")) + pszSubkey;
    }

    HKEY hKey;
    LONG ec = ::RegCreateKeyEx(HKEY_CLASSES_ROOT, szKey, 0, 0, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, 0, &hKey, 0);
    if (ec == ERROR_SUCCESS) {
        if (pszValue != 0) {
            ec = ::RegSetValueEx(hKey, pszValueName, 0, REG_SZ,
                                 reinterpret_cast<BYTE*>(const_cast<LPTSTR>(pszValue)),
                                 (DWORD)(_tcslen(pszValue) + 1) * sizeof(TCHAR));
        }

        bOK = (ec == ERROR_SUCCESS);

        ::RegCloseKey(hKey);
    }

    return bOK;
}

bool SetRegKeyValue(LPCTSTR pszKey, LPCTSTR pszSubkey, LPCTSTR pszValue)
{
    return SetRegKeyValue(pszKey, pszSubkey, 0, pszValue);
}

void RegisterSourceFilter(const CLSID& clsid, const GUID& subtype2, LPCTSTR chkbytes, LPCTSTR ext, ...)
{
    CString null = CStringFromGUID(GUID_NULL);
    CString majortype = CStringFromGUID(MEDIATYPE_Stream);
    CString subtype = CStringFromGUID(subtype2);

    SetRegKeyValue(_T("Media Type\\") + majortype, subtype, _T("0"), chkbytes);
    SetRegKeyValue(_T("Media Type\\") + majortype, subtype, _T("Source Filter"), CStringFromGUID(clsid));

    DeleteRegKey(_T("Media Type\\") + null, subtype);

    va_list marker;
    va_start(marker, ext);
    for (; ext; ext = va_arg(marker, LPCTSTR)) {
        DeleteRegKey(_T("Media Type\\Extensions"), ext);
    }
    va_end(marker);
}

void RegisterSourceFilter(const CLSID& clsid, const GUID& subtype2, const CAtlList<CString>& chkbytes, LPCTSTR ext, ...)
{
    CString null = CStringFromGUID(GUID_NULL);
    CString majortype = CStringFromGUID(MEDIATYPE_Stream);
    CString subtype = CStringFromGUID(subtype2);

    POSITION pos = chkbytes.GetHeadPosition();
    for (ptrdiff_t i = 0; pos; i++) {
        CString idx;
        idx.Format(_T("%Id"), i);
        SetRegKeyValue(_T("Media Type\\") + majortype, subtype, idx, chkbytes.GetNext(pos));
    }

    SetRegKeyValue(_T("Media Type\\") + majortype, subtype, _T("Source Filter"), CStringFromGUID(clsid));

    DeleteRegKey(_T("Media Type\\") + null, subtype);

    va_list marker;
    va_start(marker, ext);
    for (; ext; ext = va_arg(marker, LPCTSTR)) {
        DeleteRegKey(_T("Media Type\\Extensions"), ext);
    }
    va_end(marker);
}

void UnRegisterSourceFilter(const GUID& subtype)
{
    DeleteRegKey(_T("Media Type\\") + CStringFromGUID(MEDIATYPE_Stream), CStringFromGUID(subtype));
}

// hour, minute, second, millisec
CString ReftimeToString(const REFERENCE_TIME& rtVal)
{
    if (rtVal == _I64_MIN) {
        return _T("INVALID TIME");
    }

    CString strTemp;
    LONGLONG llTotalMs = ConvertToMilliseconds(rtVal);
    int lHour     = (int)(llTotalMs / (1000 * 60 * 60));
    int lMinute   = (llTotalMs / (1000 * 60)) % 60;
    int lSecond   = (llTotalMs /  1000) % 60;
    int lMillisec = llTotalMs  %  1000;

    strTemp.Format(_T("%02d:%02d:%02d,%03d"), lHour, lMinute, lSecond, lMillisec);
    return strTemp;
}

// hour, minute, second (round)
CString ReftimeToString2(const REFERENCE_TIME& rtVal)
{
    CString strTemp;
    LONGLONG seconds = (rtVal + 5000000) / 10000000;
    int lHour   = (int)(seconds / 3600);
    int lMinute = (int)(seconds / 60 % 60);
    int lSecond = (int)(seconds % 60);

    strTemp.Format(_T("%02d:%02d:%02d"), lHour, lMinute, lSecond);
    return strTemp;
}

CString DVDtimeToString(const DVD_HMSF_TIMECODE& rtVal, bool bAlwaysShowHours)
{
    CString strTemp;
    if (rtVal.bHours > 0 || bAlwaysShowHours) {
        strTemp.Format(_T("%02u:%02u:%02u"), rtVal.bHours, rtVal.bMinutes, rtVal.bSeconds);
    } else {
        strTemp.Format(_T("%02u:%02u"), rtVal.bMinutes, rtVal.bSeconds);
    }
    return strTemp;
}

REFERENCE_TIME StringToReftime(LPCTSTR strVal)
{
    REFERENCE_TIME rt = 0;
    int lHour = 0;
    int lMinute = 0;
    int lSecond = 0;
    int lMillisec = 0;
    LONGLONG llTotalMillisec = 0;

    if (_stscanf_s(strVal, _T("%02d:%02d:%02d,%03d"), &lHour, &lMinute, &lSecond, &lMillisec) == 4) {
        llTotalMillisec = (((lHour * 60) + lMinute) * 60 + lSecond)* REF_TIME_MILLISECONDS + lMillisec;
        rt = llTotalMillisec * (UNITS_IN_100NS / REF_TIME_MILLISECONDS); // �ȼ���ÿ�����ж��ٸ������뵥λ
    }

    return rt;
}

const wchar_t* StreamTypeToName(PES_STREAM_TYPE _Type)
{
    switch (_Type) {
        case VIDEO_STREAM_MPEG1:
            return L"MPEG-1";
        case VIDEO_STREAM_MPEG2:
            return L"MPEG-2";
        case AUDIO_STREAM_MPEG1:
            return L"MPEG-1";
        case AUDIO_STREAM_MPEG2:
            return L"MPEG-2";
        case VIDEO_STREAM_H264:
            return L"H264";
        case VIDEO_STREAM_HEVC:
            return L"HEVC";
        case AUDIO_STREAM_LPCM:
            return L"LPCM";
        case AUDIO_STREAM_AC3:
            return L"Dolby Digital";
        case AUDIO_STREAM_DTS:
            return L"DTS";
        case AUDIO_STREAM_AC3_TRUE_HD:
            return L"Dolby TrueHD";
        case AUDIO_STREAM_AC3_PLUS:
            return L"Dolby Digital Plus";
        case AUDIO_STREAM_DTS_HD:
            return L"DTS-HD High Resolution Audio";
        case AUDIO_STREAM_DTS_HD_MASTER_AUDIO:
            return L"DTS-HD Master Audio";
        case PRESENTATION_GRAPHICS_STREAM:
            return L"Presentation Graphics Stream";
        case INTERACTIVE_GRAPHICS_STREAM:
            return L"Interactive Graphics Stream";
        case SUBTITLE_STREAM:
            return L"Subtitle";
        case SECONDARY_AUDIO_AC3_PLUS:
            return L"Secondary Dolby Digital Plus";
        case SECONDARY_AUDIO_DTS_HD:
            return L"Secondary DTS-HD High Resolution Audio";
        case VIDEO_STREAM_VC1:
            return L"VC-1";
    }
    return nullptr;
}

//
// Usage: SetThreadName (-1, "MainThread");
// Code from http://msdn.microsoft.com/en-us/library/xcb2z8hs%28v=vs.110%29.aspx
//

#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO {
    DWORD  dwType;      // must be 0x1000
    LPCSTR szName;      // pointer to name (in user addr space)
    DWORD  dwThreadID;  // thread ID (-1 caller thread)
    DWORD  dwFlags;     // reserved for future use, must be zero
} THREADNAME_INFO;
#pragma pack(pop)

void SetThreadName(DWORD dwThreadID, LPCSTR szThreadName)
{
    THREADNAME_INFO info;
    info.dwType     = 0x1000;
    info.szName     = szThreadName;
    info.dwThreadID = dwThreadID;
    info.dwFlags    = 0;

    __try {
        RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}


#ifdef _DEBUG

extern HANDLE m_hOutput;
volatile LONG hOutputCounter = 0;
extern HRESULT  DbgUniqueProcessName(LPCTSTR inName, LPTSTR outName);
void DbgSetLogFile(LPCTSTR szFile)
{
    HANDLE hOutput = CreateFile(szFile, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (INVALID_HANDLE_VALUE == hOutput &&
        GetLastError() == ERROR_SHARING_VIOLATION)
    {
        TCHAR uniqueName[MAX_PATH] = { 0 };
        if (SUCCEEDED(DbgUniqueProcessName(szFile, uniqueName)))
        {
            hOutput = CreateFile(uniqueName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    }

    if (hOutput != INVALID_HANDLE_VALUE) {
        if (InterlockedCompareExchangePointer(&m_hOutput, hOutput, INVALID_HANDLE_VALUE) != INVALID_HANDLE_VALUE)
            CloseHandle(hOutput);
    }

    InterlockedIncrement(&hOutputCounter);
}

void DbgSetLogFileDesktop(LPCTSTR szFile)
{
    TCHAR szLogPath[512];
    SHGetFolderPath(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, szLogPath);
    PathAppend(szLogPath, szFile);
    DbgSetLogFile(szLogPath);
}

void DbgCloseLogFile()
{
    LONG count = InterlockedDecrement(&hOutputCounter);
    if (count == 0 && m_hOutput != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(m_hOutput);
        CloseHandle(m_hOutput);
        m_hOutput = INVALID_HANDLE_VALUE;
    }
}
#endif

void split(const std::string& text, const std::string& separators, std::list<std::string>& words)
{
    size_t n = text.length();
    size_t start, stop;

    start = text.find_first_not_of(separators);
    while ((start >= 0) && (start < n))
    {
        stop = text.find_first_of(separators, start);
        if ((stop < 0) || (stop > n)) stop = n;
        words.push_back(text.substr(start, stop - start));
        start = text.find_first_not_of(separators, stop + 1);
    }
}

IBaseFilter* FindFilter(const GUID& clsid, IFilterGraph* pFG)
{
    IBaseFilter* pFilter = nullptr;
    IEnumFilters* pEnumFilters = nullptr;
    if (pFG && SUCCEEDED(pFG->EnumFilters(&pEnumFilters))) {
        for (IBaseFilter* pBF = nullptr; S_OK == pEnumFilters->Next(1, &pBF, 0); ) {
            GUID clsid2;
            if (SUCCEEDED(pBF->GetClassID(&clsid2)) && clsid == clsid2) {
                pFilter = pBF;
                break;
            }
            SafeRelease(&pBF);
        }
        SafeRelease(&pEnumFilters);
    }

    return pFilter;
}

BOOL FilterInGraph(const GUID& clsid, IFilterGraph* pFG)
{
    BOOL bFound = FALSE;
    IBaseFilter* pFilter = nullptr;

    pFilter = FindFilter(clsid, pFG);
    bFound = (pFilter != nullptr);
    SafeRelease(&pFilter);

    return bFound;
}

BOOL FilterInGraphWithInputSubtype(const GUID& clsid, IFilterGraph* pFG, const GUID& clsidSubtype)
{
    BOOL bFound = FALSE;
    IBaseFilter* pFilter = nullptr;

    pFilter = FindFilter(clsid, pFG);

    if (pFilter) {
        IEnumPins* pPinEnum = nullptr;
        pFilter->EnumPins(&pPinEnum);
        IPin* pPin = nullptr;
        while ((S_OK == pPinEnum->Next(1, &pPin, nullptr)) && pPin) {
            PIN_DIRECTION dir;
            pPin->QueryDirection(&dir);
            if (dir == PINDIR_INPUT) {
                AM_MEDIA_TYPE mt;
                pPin->ConnectionMediaType(&mt);

                if (mt.subtype == clsidSubtype) {
                    bFound = TRUE;
                }
                FreeMediaType(mt);
            }
            SafeRelease(&pPin);

            if (bFound)
                break;
        }

        SafeRelease(&pPinEnum);
        SafeRelease(&pFilter);
    }

    return bFound;
}

std::wstring WStringFromGUID(const GUID& guid)
{
    WCHAR null[128] = { 0 }, buff[128];
    StringFromGUID2(GUID_NULL, null, 127);
    return std::wstring(StringFromGUID2(guid, buff, 127) > 0 ? buff : null);
}

int SafeMultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
{
    int len = MultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
    if (cchWideChar) {
        if (len == cchWideChar || (len == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
            lpWideCharStr[cchWideChar - 1] = 0;
        }
        else if (len == 0) {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_NO_UNICODE_TRANSLATION && CodePage == CP_UTF8) {
                return SafeMultiByteToWideChar(CP_ACP, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
            }
            else if (dwErr == ERROR_NO_UNICODE_TRANSLATION && (dwFlags & MB_ERR_INVALID_CHARS)) {
                return SafeMultiByteToWideChar(CP_UTF8, (dwFlags & ~MB_ERR_INVALID_CHARS), lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
            }
            lpWideCharStr[0] = 0;
        }
    }
    return len;
}

int SafeWideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
    int len = WideCharToMultiByte(CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
    if (cbMultiByte) {
        if (len == cbMultiByte || (len == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
            lpMultiByteStr[cbMultiByte - 1] = 0;
        }
        else if (len == 0) {
            lpMultiByteStr[0] = 0;
        }
    }
    return len;
}

LPWSTR CoTaskGetWideCharFromMultiByte(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte)
{
    int len = MultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, nullptr, 0);
    if (len) {
        LPWSTR pszWideString = (LPWSTR)CoTaskMemAlloc(len * sizeof(WCHAR));
        MultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, pszWideString, len);

        return pszWideString;
    }
    else {
        DWORD dwErr = GetLastError();
        if (dwErr == ERROR_NO_UNICODE_TRANSLATION && CodePage == CP_UTF8) {
            return CoTaskGetWideCharFromMultiByte(CP_ACP, dwFlags, lpMultiByteStr, cbMultiByte);
        }
        else if (dwErr == ERROR_NO_UNICODE_TRANSLATION && (dwFlags & MB_ERR_INVALID_CHARS)) {
            return CoTaskGetWideCharFromMultiByte(CP_UTF8, (dwFlags & ~MB_ERR_INVALID_CHARS), lpMultiByteStr, cbMultiByte);
        }
    }
    return NULL;
}

LPSTR CoTaskGetMultiByteFromWideChar(UINT CodePage, DWORD dwFlags, LPCWSTR lpMultiByteStr, int cbMultiByte)
{
    int len = WideCharToMultiByte(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, nullptr, 0, nullptr, nullptr);
    if (len) {
        LPSTR pszMBString = (LPSTR)CoTaskMemAlloc(len * sizeof(char));
        WideCharToMultiByte(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, pszMBString, len, nullptr, nullptr);

        return pszMBString;
    }
    return NULL;
}

BSTR ConvertCharToBSTR(const char* sz)
{
    bool acp = false;
    if (!sz || strlen(sz) == 0)
        return nullptr;

    WCHAR* wide = CoTaskGetWideCharFromMultiByte(CP_UTF8, MB_ERR_INVALID_CHARS, sz, -1);
    if (!wide)
        return nullptr;

    BSTR bstr = SysAllocString(wide);
    CoTaskMemFree(wide);

    return bstr;
}

IBaseFilter* GetFilterFromPin(IPin* pPin)
{
    CheckPointer(pPin, nullptr);

    PIN_INFO pi;
    if (pPin && SUCCEEDED(pPin->QueryPinInfo(&pi))) {
        return pi.pFilter;
    }

    return nullptr;
}

HRESULT NukeDownstream(IFilterGraph* pGraph, IPin* pPin)
{
    PIN_DIRECTION dir;
    if (pPin) {
        IPin* pPinTo = nullptr;
        if (FAILED(pPin->QueryDirection(&dir)))
            return E_FAIL;
        if (dir == PINDIR_OUTPUT) {
            if (SUCCEEDED(pPin->ConnectedTo(&pPinTo)) && pPinTo) {
                if (IBaseFilter* pFilter = GetFilterFromPin(pPinTo)) {
                    NukeDownstream(pGraph, pFilter);
                    pGraph->Disconnect(pPinTo);
                    pGraph->Disconnect(pPin);
                    pGraph->RemoveFilter(pFilter);
                    SafeRelease(&pFilter);
                }
                SafeRelease(&pPinTo);
            }
        }
    }

    return S_OK;
}

HRESULT NukeDownstream(IFilterGraph* pGraph, IBaseFilter* pFilter)
{
    IEnumPins* pEnumPins = nullptr;
    if (pFilter && SUCCEEDED(pFilter->EnumPins(&pEnumPins))) {
        for (IPin* pPin = nullptr; S_OK == pEnumPins->Next(1, &pPin, 0); pPin = nullptr) {
            NukeDownstream(pGraph, pPin);
            SafeRelease(&pPin);
        }
        SafeRelease(&pEnumPins);
    }

    return S_OK;
}

// pPin - pin of our filter to start searching
// refiid - guid of the interface to find
// pUnknown - variable that'll receive the interface
HRESULT FindIntefaceInGraph(IPin* pPin, REFIID refiid, void** pUnknown)
{
    PIN_DIRECTION dir;
    pPin->QueryDirection(&dir);

    IPin* pOtherPin = nullptr;
    if (SUCCEEDED(pPin->ConnectedTo(&pOtherPin)) && pOtherPin) {
        IBaseFilter* pFilter = GetFilterFromPin(pOtherPin);
        SafeRelease(&pOtherPin);

        HRESULT hrFilter = pFilter->QueryInterface(refiid, pUnknown);
        if (FAILED(hrFilter)) {
            IEnumPins* pPinEnum = nullptr;
            pFilter->EnumPins(&pPinEnum);

            HRESULT hrPin = E_FAIL;
            for (IPin* pOtherPin2 = nullptr; pPinEnum->Next(1, &pOtherPin2, 0) == S_OK; pOtherPin2 = nullptr) {
                PIN_DIRECTION pinDir;
                pOtherPin2->QueryDirection(&pinDir);
                if (dir == pinDir) {
                    hrPin = FindIntefaceInGraph(pOtherPin2, refiid, pUnknown);
                }
                SafeRelease(&pOtherPin2);
                if (SUCCEEDED(hrPin))
                    break;
            }
            hrFilter = hrPin;
            SafeRelease(&pPinEnum);
        }
        SafeRelease(&pFilter);

        if (SUCCEEDED(hrFilter)) {
            return S_OK;
        }
    }
    return E_NOINTERFACE;
}

// pPin - pin of our filter to start searching
// refiid - guid of the interface to find
// pUnknown - variable that'll receive the interface
HRESULT FindPinIntefaceInGraph(IPin* pPin, REFIID refiid, void** pUnknown)
{
    PIN_DIRECTION dir;
    pPin->QueryDirection(&dir);

    IPin* pOtherPin = nullptr;
    if (SUCCEEDED(pPin->ConnectedTo(&pOtherPin)) && pOtherPin) {
        IBaseFilter* pFilter = nullptr;
        HRESULT hrFilter = pOtherPin->QueryInterface(refiid, pUnknown);

        if (FAILED(hrFilter)) {
            pFilter = GetFilterFromPin(pOtherPin);

            IEnumPins* pPinEnum = nullptr;
            pFilter->EnumPins(&pPinEnum);

            HRESULT hrPin = E_FAIL;
            for (IPin* pOtherPin2 = nullptr; pPinEnum->Next(1, &pOtherPin2, 0) == S_OK; pOtherPin2 = nullptr) {
                PIN_DIRECTION pinDir;
                pOtherPin2->QueryDirection(&pinDir);
                if (dir == pinDir) {
                    hrPin = FindPinIntefaceInGraph(pOtherPin2, refiid, pUnknown);
                }
                SafeRelease(&pOtherPin2);
                if (SUCCEEDED(hrPin))
                    break;
            }
            hrFilter = hrPin;
            SafeRelease(&pPinEnum);
        }
        SafeRelease(&pFilter);
        SafeRelease(&pOtherPin);

        if (SUCCEEDED(hrFilter)) {
            return S_OK;
        }
    }
    return E_NOINTERFACE;
}

// pPin - pin of our filter to start searching
// guid - guid of the filter to find
// ppFilter - variable that'll receive a AddRef'd reference to the filter
HRESULT FindFilterSafe(IPin* pPin, const GUID& guid, IBaseFilter** ppFilter, BOOL bReverse)
{
    CheckPointer(ppFilter, E_POINTER);
    CheckPointer(pPin, E_POINTER);
    HRESULT hr = S_OK;

    PIN_DIRECTION dir;
    pPin->QueryDirection(&dir);

    IPin* pOtherPin = nullptr;
    if (bReverse) {
        dir = (dir == PINDIR_INPUT) ? PINDIR_OUTPUT : PINDIR_INPUT;
        pOtherPin = pPin;
        pPin->AddRef();
        hr = S_OK;
    }
    else {
        hr = pPin->ConnectedTo(&pOtherPin);
    }
    if (SUCCEEDED(hr) && pOtherPin) {
        IBaseFilter* pFilter = GetFilterFromPin(pOtherPin);
        SafeRelease(&pOtherPin);

        HRESULT hrFilter = E_NOINTERFACE;
        CLSID filterGUID;
        if (SUCCEEDED(pFilter->GetClassID(&filterGUID))) {
            if (filterGUID == guid) {
                *ppFilter = pFilter;
                hrFilter = S_OK;
            }
            else {
                IEnumPins* pPinEnum = nullptr;
                pFilter->EnumPins(&pPinEnum);

                HRESULT hrPin = E_FAIL;
                for (IPin* pOtherPin2 = nullptr; pPinEnum->Next(1, &pOtherPin2, 0) == S_OK; pOtherPin2 = nullptr) {
                    PIN_DIRECTION pinDir;
                    pOtherPin2->QueryDirection(&pinDir);
                    if (dir == pinDir) {
                        hrPin = FindFilterSafe(pOtherPin2, guid, ppFilter);
                    }
                    SafeRelease(&pOtherPin2);
                    if (SUCCEEDED(hrPin))
                        break;
                }
                hrFilter = hrPin;
                SafeRelease(&pPinEnum);
                SafeRelease(&pFilter);
            }
        }

        if (SUCCEEDED(hrFilter)) {
            return S_OK;
        }
    }
    return E_NOINTERFACE;
}

// pPin - pin of our filter to start searching
// guid - guid of the filter to find
// ppFilter - variable that'll receive a AddRef'd reference to the filter
BOOL HasSourceWithType(IPin* pPin, const GUID& mediaType)
{
    CheckPointer(pPin, E_POINTER);
    BOOL bFound = FALSE;

    PIN_DIRECTION dir;
    pPin->QueryDirection(&dir);

    IPin* pOtherPin = nullptr;
    if (SUCCEEDED(pPin->ConnectedTo(&pOtherPin)) && pOtherPin) {
        IBaseFilter* pFilter = GetFilterFromPin(pOtherPin);

        HRESULT hrFilter = E_NOINTERFACE;
        IEnumPins* pPinEnum = nullptr;
        pFilter->EnumPins(&pPinEnum);

        HRESULT hrPin = E_FAIL;
        for (IPin* pOtherPin2 = nullptr; !bFound && pPinEnum->Next(1, &pOtherPin2, 0) == S_OK; pOtherPin2 = nullptr) {
            if (pOtherPin2 != pOtherPin) {
                PIN_DIRECTION pinDir;
                pOtherPin2->QueryDirection(&pinDir);
                if (dir != pinDir) {
                    IEnumMediaTypes* pMediaTypeEnum = nullptr;
                    if (SUCCEEDED(pOtherPin2->EnumMediaTypes(&pMediaTypeEnum))) {
                        for (AM_MEDIA_TYPE* mt = nullptr; pMediaTypeEnum->Next(1, &mt, 0) == S_OK; mt = nullptr) {
                            if (mt->majortype == mediaType) {
                                bFound = TRUE;
                            }
                            DeleteMediaType(mt);
                        }
                        SafeRelease(&pMediaTypeEnum);
                    }
                }
                else {
                    bFound = HasSourceWithType(pOtherPin2, mediaType);
                }
            }
            SafeRelease(&pOtherPin2);
        }
        SafeRelease(&pPinEnum);
        SafeRelease(&pFilter);
        SafeRelease(&pOtherPin);
    }
    return bFound;
}

BOOL FilterInGraphSafe(IPin* pPin, const GUID& guid, BOOL bReverse)
{
    IBaseFilter* pFilter = nullptr;
    HRESULT hr = FindFilterSafe(pPin, guid, &pFilter, bReverse);
    if (SUCCEEDED(hr) && pFilter) {
        SafeRelease(&pFilter);
        return TRUE;
    }
    return FALSE;
}

unsigned int lav_xiphlacing(unsigned char* s, unsigned int v)
{
    unsigned int n = 0;

    while (v >= 0xff) {
        *s++ = 0xff;
        v -= 0xff;
        n++;
    }
    *s = v;
    n++;
    return n;
}

void videoFormatTypeHandler(const AM_MEDIA_TYPE& mt, BITMAPINFOHEADER** pBMI, REFERENCE_TIME* prtAvgTime, DWORD* pDwAspectX, DWORD* pDwAspectY)
{
    videoFormatTypeHandler(mt.pbFormat, &mt.formattype, pBMI, prtAvgTime, pDwAspectX, pDwAspectY);
}

void videoFormatTypeHandler(const BYTE* format, const GUID* formattype, BITMAPINFOHEADER** pBMI, REFERENCE_TIME* prtAvgTime, DWORD* pDwAspectX, DWORD* pDwAspectY)
{
    REFERENCE_TIME rtAvg = 0;
    BITMAPINFOHEADER* bmi = nullptr;
    DWORD dwAspectX = 0, dwAspectY = 0;

    if (!format)
        goto done;

    ASSERT(*formattype == FORMAT_VideoInfo2);
    {
        VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)format;
        rtAvg = vih2->AvgTimePerFrame;
        bmi = &vih2->bmiHeader;
        dwAspectX = vih2->dwPictAspectRatioX;
        dwAspectY = vih2->dwPictAspectRatioY;
    }

done:
    if (pBMI) {
        *pBMI = bmi;
    }
    if (prtAvgTime) {
        *prtAvgTime = rtAvg;
    }
    if (pDwAspectX && pDwAspectY) {
        *pDwAspectX = dwAspectX;
        *pDwAspectY = dwAspectY;
    }
}

void audioFormatTypeHandler(const BYTE* format, const GUID* formattype, DWORD* pnSamples, WORD* pnChannels, WORD* pnBitsPerSample, WORD* pnBlockAlign, DWORD* pnBytesPerSec)
{
    DWORD nSamples = 0;
    WORD  nChannels = 0;
    WORD  nBitsPerSample = 0;
    WORD  nBlockAlign = 0;
    DWORD nBytesPerSec = 0;

    if (!format)
        goto done;

    if (*formattype == FORMAT_WaveFormatEx) {
        WAVEFORMATEX* wfex = (WAVEFORMATEX*)format;
        nSamples = wfex->nSamplesPerSec;
        nChannels = wfex->nChannels;
        nBitsPerSample = wfex->wBitsPerSample;
        nBlockAlign = wfex->nBlockAlign;
        nBytesPerSec = wfex->nAvgBytesPerSec;
    }
    else if (*formattype == FORMAT_VorbisFormat2) {
        VORBISFORMAT2* vf2 = (VORBISFORMAT2*)format;
        nSamples = vf2->SamplesPerSec;
        nChannels = (WORD)vf2->Channels;
        nBitsPerSample = (WORD)vf2->BitsPerSample;
    }

done:
    if (pnSamples)
        *pnSamples = nSamples;
    if (pnChannels)
        *pnChannels = nChannels;
    if (pnBitsPerSample)
        *pnBitsPerSample = nBitsPerSample;
    if (pnBlockAlign)
        *pnBlockAlign = nBlockAlign;
    if (pnBytesPerSec)
        *pnBytesPerSec = nBytesPerSec;
}

void getExtraData(const AM_MEDIA_TYPE& mt, BYTE* extra, size_t* extralen)
{
    return getExtraData(mt.pbFormat, &mt.formattype, mt.cbFormat, extra, extralen);
}

void getExtraData(const BYTE* format, const GUID* formattype, const size_t formatlen, BYTE* extra, size_t* extralen)
{
    const BYTE* extraposition = nullptr;
    size_t extralength = 0;
    if (*formattype == FORMAT_WaveFormatEx) {
        WAVEFORMATEX* wfex = (WAVEFORMATEX*)format;
        extraposition = format + sizeof(WAVEFORMATEX);
        // Protected against over-reads
        extralength = formatlen - sizeof(WAVEFORMATEX);
    }
     else if (*formattype == FORMAT_VideoInfo2) {
        extraposition = format + sizeof(VIDEOINFOHEADER2);
        extralength = formatlen - sizeof(VIDEOINFOHEADER2);
    }

    if (extra && extralength)
        memcpy(extra, extraposition, extralength);
    if (extralen)
        *extralen = extralength;
}

void CopyMediaSideDataFF(AVPacket* dst, const MediaSideDataFFMpeg** sd)
{
    if (!dst)
        return;

    if (!sd || !*sd) {
        dst->side_data = nullptr;
        dst->side_data_elems = 0;
        return;
    }

    // add sidedata to the packet
    for (int i = 0; i < (*sd)->side_data_elems; i++) {
        uint8_t* ptr = av_packet_new_side_data(dst, (*sd)->side_data[i].type, (*sd)->side_data[i].size);
        memcpy(ptr, (*sd)->side_data[i].data, (*sd)->side_data[i].size);
    }

    *sd = nullptr;
}

void __cdecl debugprintf(LPCWSTR format, ...)
{
    WCHAR buf[4096], *p = buf;
    va_list args;
    int n;

    va_start(args, format);
    n = _vsnwprintf_s(p, 4096, 4096 - 3, format, args); // buf-3 is room for CR/LF/NUL
    va_end(args);

    p += (n < 0) ? (4096 - 3) : n;

    while (p > buf && isspace(p[-1]))
        *--p = L'\0';

    *p++ = L'\r';
    *p++ = L'\n';
    *p = L'\0';

    OutputDebugString(buf);
}

HRESULT SetH265VideoType(AM_MEDIA_TYPE& mt, long width, long height, double fps)
{
    assert(width >= 0);
    _FreeMediaType(mt);
    mt.pbFormat = (BYTE*)CoTaskMemAlloc(sizeof(VIDEOINFOHEADER2));
    if (mt.pbFormat == nullptr) {
        return E_OUTOFMEMORY;
    }
    mt.cbFormat = sizeof(VIDEOINFOHEADER2);
    mt.majortype = MEDIATYPE_Video;
    mt.subtype = MEDIASUBTYPE_HEVC;
    mt.formattype = FORMAT_VideoInfo2;
    mt.bTemporalCompression = TRUE;
    mt.lSampleSize = 0;
    mt.bFixedSizeSamples = FALSE;

    VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)mt.pbFormat;
    ZeroMemory(vih2, sizeof(VIDEOINFOHEADER2));
    vih2->AvgTimePerFrame = FramesPerSecToFrameLength(fps);

    BITMAPINFOHEADER& bmi = vih2->bmiHeader;
    bmi.biSize = sizeof(BITMAPINFOHEADER);
    bmi.biWidth = width;
    bmi.biHeight = height;
    bmi.biPlanes = 1;
    bmi.biBitCount = 32;
    bmi.biCompression = MAKEFOURCC('H', '2', '6', '5');
    bmi.biSizeImage = 0;

    return S_OK;
}
