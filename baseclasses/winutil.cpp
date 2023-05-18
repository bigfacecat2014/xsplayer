#include "stdafx.h"
#include <streams.h>
#include <limits.h>
#include <dvdmedia.h>
#include <strsafe.h>
#include <checkbmi.h>

//-------------------------------------------------------------------------------------------------
// 基于DIB的媒体样本绘制器的实现
//-------------------------------------------------------------------------------------------------
void CDrawImage::FastRender(IMediaSample* pMediaSample, HDC hdcDraw)
{
    BOOL bRet = FALSE;
    CImageSample* pSample = (CImageSample*)pMediaSample;
    DIBData_t* pDibData = pSample->GetDIBData();

    // 难道内存DC内部有一个位块指针，可以直接指向一个位图资源的起始地址？
    SelectObject(m_MemoryDC, pDibData->hBitmap);

    BYTE* pImage = nullptr;
    HRESULT hr = pMediaSample->GetPointer(&pImage);
    if (FAILED(hr)) {
        return;
    }

    if (!m_bStretch) {
        // GDI运行时库尽量使用显卡驱动提供的GDI规范的DDI函数来加速位块传送。
        bRet = BitBlt(
            hdcDraw,                                // Target device HDC
            m_TargetRect.left,                      // X sink position
            m_TargetRect.top,                       // Y sink position
            m_TargetRect.right - m_TargetRect.left, // Destination width
            m_TargetRect.bottom - m_TargetRect.top, // Destination height
            m_MemoryDC,                             // Source device context
            m_SourceRect.left,                      // X source position
            m_SourceRect.top,                       // Y source position
            SRCCOPY);                               // Simple copy

    }
    else {
        ::SetStretchBltMode(hdcDraw, COLORONCOLOR);
        // 此API同样有机会得到显卡驱动的GDI规范的DDI函数的硬件加速。
        bRet = StretchBlt(
            hdcDraw,                                // Target device HDC
            m_TargetRect.left,                      // X sink position
            m_TargetRect.top,                       // Y sink position
            m_TargetRect.right - m_TargetRect.left, // Destination width
            m_TargetRect.bottom - m_TargetRect.top, // Destination height
            m_MemoryDC,                             // Source device HDC
            m_SourceRect.left,                      // X source position
            m_SourceRect.top,                       // Y source position
            m_SourceRect.right - m_SourceRect.left, // Source width
            m_SourceRect.bottom - m_SourceRect.top, // Source height
            SRCCOPY);                               // Simple copy
    }
}

BOOL CDrawImage::DrawImage(IMediaSample* pMediaSample, HDC hdcDraw)
{
    FastRender(pMediaSample, hdcDraw);
    EXECUTE_ASSERT(GdiFlush());
    return TRUE;
}

void CDrawImage::SetStretchMode()
{
    LONG SourceWidth = m_SourceRect.right - m_SourceRect.left;
    LONG SinkWidth = m_TargetRect.right - m_TargetRect.left;
    LONG SourceHeight = m_SourceRect.bottom - m_SourceRect.top;
    LONG SinkHeight = m_TargetRect.bottom - m_TargetRect.top;
    m_bStretch = (SourceWidth != SinkWidth) || (SourceHeight != SinkHeight);
}


//-------------------------------------------------------------------------------------------------
// 基于DIB的媒体样本分配器的实现
//-------------------------------------------------------------------------------------------------
void CImageAllocator::Free()
{
    ASSERT(m_lAllocated == m_lFree.GetCount());
    EXECUTE_ASSERT(GdiFlush());
    CImageSample* pSample;
    DIBData_t* pDibData;

    while (m_lFree.GetCount() != 0) {
        pSample = (CImageSample*)m_lFree.RemoveHead();
        pDibData = pSample->GetDIBData();
        EXECUTE_ASSERT(DeleteObject(pDibData->hBitmap));
        EXECUTE_ASSERT(CloseHandle(pDibData->hMapping));
        delete pSample;
    }

    m_lAllocated = 0;
}

STDMETHODIMP CImageAllocator::CheckSizes(__in ALLOCATOR_PROPERTIES* pRequest)
{
    //VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_pMediaType->Format();
    //if ((DWORD)pRequest->cbBuffer < vih2->bmiHeader.biSizeImage) {
    //    return E_INVALIDARG;
    //}
    if (pRequest->cbPrefix > 0) {
        return E_INVALIDARG;
    }
    //pRequest->cbBuffer = vih2->bmiHeader.biSizeImage;
    return NOERROR;
}

HRESULT CImageAllocator::Alloc(void)
{
    DIBData_t DibData;

    HRESULT hr = CBaseAllocator::Alloc();
    if (FAILED(hr)) {
        return hr;
    }

    ASSERT(m_lAllocated == 0);
    while (m_lAllocated < m_lCount) {
        // 创建并初始化一个共享内存DIB。
        hr = CreateDIB(m_lSize, DibData);
        if (FAILED(hr)) {
            return hr;
        }

        // 将DIB内存封装成媒体样本对象。
        CImageSample* pSample = CreateImageSample(DibData.pBase, m_lSize);
        if (pSample == NULL) {
            EXECUTE_ASSERT(DeleteObject(DibData.hBitmap));
            EXECUTE_ASSERT(CloseHandle(DibData.hMapping));
            return E_OUTOFMEMORY;
        }
        pSample->SetDIBData(&DibData);

        // 将已成功分配的样本添加到空闲样本列表。
        m_lFree.Add(pSample);
        m_lAllocated++;
    }
    return NOERROR;
}

CImageSample* CImageAllocator::CreateImageSample(__in_bcount(Length) LPBYTE pData, LONG Length)
{
    HRESULT hr = NOERROR;
    CImageSample* pSample;

    pSample = new CImageSample(this, TEXT(""), &hr, pData, Length);
    if (pSample == NULL || FAILED(hr)) {
        delete pSample;
        return NULL;
    }
    return pSample;
}

//
// 分配供上游解码器输出使用的共享DIB内存块，上游解码器填充内容后，
// 将DIB内存块投递给GDI渲染器进行呈现。
// 由于DIB内存块创建在共享内存中，因此GDI使用BitBlt呈现的时候可以免除
// 一次用户空间到内核空间的内存拷贝的开销，甚至可以借助显卡驱动提供的
// GDI规范的DDI实现进行硬解加速。
//
HRESULT CImageAllocator::CreateDIB(LONG InSize, DIBData_t& DibData)
{
    BYTE* pBase = nullptr; // Pointer to the actual image
    HANDLE hMapping = 0; // Handle to mapped object
    HBITMAP hBitmap = 0; // DIB section bitmap handle

    // Create a file mapping object and map into our address space
    hMapping = CreateFileMapping(hMEMORY, // Use system page file
        NULL,            // No security attributes
        PAGE_READWRITE,  // Full access to memory
        (DWORD)0,        // Less than 4Gb in size
        InSize,          // Size of buffer
        NULL);           // No name to section
    if (hMapping == NULL) {
        DWORD Error = GetLastError();
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, Error);
    }

    // 早已抛弃256色调色板模式了，只考虑32位真彩色。
    VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_pMediaType->Format();
    BITMAPINFO* bmi = (BITMAPINFO*)&(vih2->bmiHeader);
    ASSERT(bmi->bmiHeader.biBitCount == 32); // 仅支持RGB32颜色模式。
    hBitmap = CreateDIBSection((HDC)NULL,          // NO device context
        bmi,                // Format information
        DIB_RGB_COLORS,     // Use the palette
        (VOID**)&pBase,     // Pointer to image data
        hMapping,           // Mapped memory handle
        (DWORD)0);          // Offset into memory
    if (hBitmap == NULL || pBase == NULL) {
        EXECUTE_ASSERT(CloseHandle(hMapping));
        DWORD Error = GetLastError();
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, Error);
    }

    // Initialise the DIB information structure
    DibData.hBitmap = hBitmap;
    DibData.hMapping = hMapping;
    DibData.pBase = pBase;
    GetObject(hBitmap, sizeof(DIBSECTION), (VOID*)&DibData.DibSection);

    return NOERROR;
}

HRESULT CImageDisplay::RefreshDisplayType(__in_opt LPSTR szDeviceName)
{
    CAutoLock cDisplayLock(this);

    m_Display.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    m_Display.bmiHeader.biBitCount = FALSE;
    // 检测显示设备的色深，考虑多头显示设备（如：九头显卡）。
    {
        HDC hdcDisplay;
        if (szDeviceName == NULL || lstrcmpiLocaleIndependentA(szDeviceName, "DISPLAY") == 0)
            hdcDisplay = CreateDCA("DISPLAY", NULL, NULL, NULL);
        else
            hdcDisplay = CreateDCA(NULL, szDeviceName, NULL, NULL);
        if (hdcDisplay == NULL) {
            ASSERT(FALSE);
            DbgLog((LOG_ERROR, 1, L"ACK! Can't get a DC for %S", szDeviceName ? szDeviceName : "<NULL>"));
            return E_FAIL;
        }
        else {
            DbgLog((LOG_TRACE, 3, L"Created a DC for %S", szDeviceName ? szDeviceName : "<NULL>"));
        }
        // 用一个1X1的尺寸的像素格式与显示设备兼容的内存DIB间接得知显示设备的像素格式。
        HBITMAP hbm = CreateCompatibleBitmap(hdcDisplay, 1, 1);
        if (hbm)
        {
            GetDIBits(hdcDisplay, hbm, 0, 1, NULL, (BITMAPINFO*)&m_Display.bmiHeader, DIB_RGB_COLORS);
            GetDIBits(hdcDisplay, hbm, 0, 1, NULL, (BITMAPINFO*)&m_Display.bmiHeader, DIB_RGB_COLORS);
            DeleteObject(hbm);
        }
        DeleteDC(hdcDisplay);
    }
    ASSERT(CheckHeaderValidity(&m_Display));
    UpdateFormat(&m_Display);
    DbgLog((LOG_TRACE, 3, TEXT("New DISPLAY bit depth =%d"), m_Display.bmiHeader.biBitCount));
    return NOERROR;
}

BOOL CImageDisplay::CheckHeaderValidity(const VIDEOINFOHEADER2* pInput)
{
    const BITMAPINFOHEADER& bmih = pInput->bmiHeader;
    ASSERT(bmih.biWidth >= 0 && bmih.biHeight >= 0);
    ASSERT(bmih.biCompression == BI_BITFIELDS);
    ASSERT(bmih.biPlanes == 1);
    ASSERT(bmih.biSizeImage == GetBitmapSize(&bmih));
    ASSERT(bmih.biSize == sizeof(BITMAPINFOHEADER));
    return TRUE;
}

HRESULT CImageDisplay::UpdateFormat(__inout VIDEOINFOHEADER2* vih2)
{
    const BITMAPINFOHEADER& bmih = vih2->bmiHeader;
    SetRectEmpty(&vih2->rcSource);
    SetRectEmpty(&vih2->rcTarget);

    // 必须显式地指定颜色数量
    if (PALETTISED(vih2)) {
        ASSERT(vih2->bmiHeader.biClrUsed == PALETTE_ENTRIES(vih2));
        ASSERT(vih2->bmiHeader.biClrImportant == PALETTE_ENTRIES(vih2));

    }
    ASSERT(vih2->bmiHeader.biSizeImage == GetBitmapSize(&vih2->bmiHeader));

    return NOERROR;
}

HRESULT CImageDisplay::CheckVideoType(const VIDEOINFOHEADER2* vih2)
{
    if (!CheckHeaderValidity(vih2)) {
        return E_INVALIDARG;
    }

    ASSERT(m_Display.bmiHeader.biBitCount == vih2->bmiHeader.biBitCount);
    ASSERT(!PALETTISED(vih2));
    ASSERT(!PALETTISED(&m_Display));

    return NOERROR;
}

HRESULT CImageDisplay::CheckMediaType(const CMediaType* pmtIn)
{
    // Does this have a VIDEOINFOHEADER2 format block
    const GUID& ft = *pmtIn->FormatType();
    if (ft != FORMAT_VideoInfo2 || pmtIn->cbFormat < sizeof(VIDEOINFOHEADER2)) {
        NOTE("Format GUID not a VIDEOINFOHEADER2");
        return E_INVALIDARG;
    }
    ASSERT(pmtIn->Format());

    VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmtIn->Format();

    // Check the major type is MEDIATYPE_Video
    const GUID* pMajorType = pmtIn->Type();
    if (*pMajorType != MEDIATYPE_Video) {
        NOTE("Major type not MEDIATYPE_Video");
        return E_INVALIDARG;
    }

    // Check we can identify the media subtype
    const GUID* pSubType = pmtIn->Subtype();
    if (GetBitCount(pSubType) == USHRT_MAX) {
        NOTE("Invalid video media subtype");
        return E_INVALIDARG;
    }
    return CheckVideoType(vih2);
}

STDAPI CheckVideoInfo2Type(const AM_MEDIA_TYPE* pmt)
{
    if (NULL == pmt || NULL == pmt->pbFormat) {
        return E_POINTER;
    }
    if (pmt->majortype != MEDIATYPE_Video ||
        pmt->formattype != FORMAT_VideoInfo2 ||
        pmt->cbFormat < sizeof(VIDEOINFOHEADER2)) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    const VIDEOINFOHEADER2* pHeader = (const VIDEOINFOHEADER2*)pmt->pbFormat;
    if (!ValidateBitmapInfoHeader(&pHeader->bmiHeader,
        pmt->cbFormat - FIELD_OFFSET(VIDEOINFOHEADER2, bmiHeader))) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    return S_OK;
}
