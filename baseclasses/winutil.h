#pragma once

const int WND_DEF_POS_X = 0; // Initial window width
const int WND_DEF_POS_Y = 0; // Initial window height
const int WND_DEF_WIDTH = 640; // Initial window width
const int WND_DEF_HEIGHT = 480; // Initial window height
const int CAPTION = 256; // Maximum length of caption
const int TIMELENGTH = 50; // Maximum length of times
const int PROFILESTR = 128; // Normal profile string
const LONG PALETTE_VERSION = (LONG) 1; // Initial palette version
const COLORREF VIDEO_COLOUR = 0; // Defaults to black background
const HANDLE hMEMORY = (HANDLE)(-1); // Says to open as memory file

inline LONG WIDTH(LPCRECT r) { return r->right - r->left; }
inline LONG HEIGHT(LPCRECT r) { return r->bottom - r->top; }

#define SHOWSTAGE TEXT("WM_SHOWSTAGE")
#define SHOWSTAGETOP TEXT("WM_SHOWSTAGETOP")

//-------------------------------------------------------------------------------------------------
// 基于DIB的媒体样本绘制器
//-------------------------------------------------------------------------------------------------
class CDrawImage
{
protected:
    CMediaType* m_pMediaType = nullptr;
    HWND m_hwnd = 0;
    HDC m_MemoryDC = 0; // 用于引用DIB位图。
    RECT m_TargetRect = { 0 }; // 全尺寸（满输出视口）的目标矩形。
    RECT m_SourceRect = { 0 }; // 未缩放状态的全尺寸（完整视频源帧画面）源矩形。
    BOOL m_bStretch = FALSE; // Do we have to stretch the images
    BOOL m_bZoomed = FALSE; // 是否需要拉框放大。
    RECT m_ZoomedSourceRect = { 0 }; // 矩形框选择放大部分的源矩形，若1:1放大,则等同于m_SourceRect。

    void FastRender(IMediaSample* pMediaSample, HDC hdcDraw);
    void SetStretchMode();

public:
    void NotifyMediaType(__in CMediaType* pMediaType) { m_pMediaType = pMediaType; }

    HRESULT InitHostWnd(HWND hwnd) {
        m_hwnd = hwnd;
        EXECUTE_ASSERT(m_MemoryDC = CreateCompatibleDC(GetDC(hwnd)));
        return S_OK;
    }

    BOOL DrawImage(IMediaSample* pMediaSample, HDC hdcDraw);

    void SetTargetRect(const RECT* r) { m_TargetRect = *r; SetStretchMode(); }
    void GetTargetRect(__out RECT* r) { *r = m_TargetRect; }
    void SetSourceRect(const RECT* r) { m_SourceRect = *r; SetStretchMode(); }
    void GetSourceRect(__out RECT* r) { *r = m_SourceRect; }
};

struct DIBData_t {
    DIBSECTION DibSection; // Details of DIB section allocated
    HBITMAP hBitmap; // Handle to bitmap for drawing
    HANDLE hMapping; // Handle to shared memory block
    BYTE* pBase; // Pointer to base memory address
};


//-------------------------------------------------------------------------------------------------
// 基于DIB的媒体样本封装
//-------------------------------------------------------------------------------------------------
class CImageSample : public CMediaSample
{
protected:
    DIBData_t m_DIBData = { 0 };
    BOOL m_bInit = FALSE;

public:
    CImageSample(__inout CBaseAllocator* pAllocator, __in_opt LPCTSTR pName,
        __inout HRESULT* phr, __in_bcount(length) LPBYTE pBuffer, LONG length) 
        : CMediaSample(pName, pAllocator, phr, pBuffer, length) {
    }

    STDMETHODIMP_(ULONG) Release()
    {
        LONG r = CMediaSample::Release();
        return r;
    }

    void SetDIBData(__in DIBData_t* pDibData) {
        m_DIBData = *pDibData;
        m_bInit = TRUE;
    }

    __out DIBData_t* GetDIBData() {
        ASSERT(m_bInit);
        return &m_DIBData;
    }
};


//-------------------------------------------------------------------------------------------------
// 基于DIB的媒体样本分配器
//-------------------------------------------------------------------------------------------------
class CImageAllocator : public CBaseAllocator
{
protected:
    CBaseFilter* m_pFilter; // Delegate reference counts to
    CMediaType* m_pMediaType; // Pointer to the current format

    HRESULT Alloc();
    void Free();

    HRESULT CreateDIB(LONG InSize, DIBData_t& DibData);
    STDMETHODIMP CheckSizes(__in ALLOCATOR_PROPERTIES* pRequest);
    virtual CImageSample* CreateImageSample(__in_bcount(Length) LPBYTE pData, LONG Length);

public:
    CImageAllocator(__inout CBaseFilter* pFilter,__in_opt LPCTSTR pName, __inout HRESULT* phr)
        : CBaseAllocator(pName, NULL, phr, TRUE, TRUE), m_pFilter(pFilter) {
        ASSERT(phr);
        ASSERT(pFilter);
    }
    ~CImageAllocator() {
        ASSERT(m_bCommitted == FALSE);
    }

    HRESULT __stdcall GetBuffer(__deref_out IMediaSample** ppBuffer, __in_opt REFERENCE_TIME* pStartTime,
        __in_opt REFERENCE_TIME* pEndTime, DWORD dwFlags) {
        return CBaseAllocator::GetBuffer(ppBuffer, pStartTime, pEndTime, dwFlags);
    }

    HRESULT __stdcall ReleaseBuffer(IMediaSample* pBuffer) {
        return CBaseAllocator::ReleaseBuffer(pBuffer);
    }

    void NotifyMediaType(__in CMediaType* pMediaType) {
        m_pMediaType = pMediaType;
    }

    STDMETHODIMP_(ULONG) NonDelegatingAddRef() { return m_pFilter->AddRef(); }
    STDMETHODIMP_(ULONG) NonDelegatingRelease() { return m_pFilter->Release(); }

    STDMETHODIMP SetProperties(__in ALLOCATOR_PROPERTIES* pRequest, __out ALLOCATOR_PROPERTIES* pActual) {
        ALLOCATOR_PROPERTIES Adjusted = *pRequest;
        HRESULT hr = CheckSizes(&Adjusted);
        if (FAILED(hr)) {
            return hr;
        }
        return CBaseAllocator::SetProperties(&Adjusted, pActual);
    }
};


//-------------------------------------------------------------------------------------------------
// 封装了与特定显示器相关的显示输出头（VDC电路）使用的显示格式信息。
//-------------------------------------------------------------------------------------------------
class CImageDisplay : public CCritSec
{
protected:
    VIDEOINFOHEADER2 m_Display = { 0 };

public:
    CImageDisplay() {
        RefreshDisplayType(NULL);
    }

    const VIDEOINFOHEADER2* GetDisplayFormat() { return &m_Display; }
    WORD GetDisplayDepth() { return m_Display.bmiHeader.biBitCount; }

    HRESULT RefreshDisplayType(__in_opt LPSTR szDeviceName);
    static BOOL CheckHeaderValidity(const VIDEOINFOHEADER2* pInput);
    HRESULT CheckMediaType(const CMediaType* pmtIn);
    HRESULT UpdateFormat(__inout VIDEOINFOHEADER2* pVideoInfo);
    HRESULT CheckVideoType(const VIDEOINFOHEADER2* pInput);
};

//  Check a media type containing VIDEOINFOHEADER2
STDAPI CheckVideoInfo2Type(const AM_MEDIA_TYPE* pmt);
