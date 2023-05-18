
#include "stdafx.h"
#include "FixedGraph.h"

CFixedGraph::CFixedGraph(HWND hwndHost, HRESULT& hr)
{
    _hwndHost = hwndHost;
}

CFixedGraph::~CFixedGraph()
{
}

// IGraphBuilder
HRESULT CFixedGraph::RenderFile(LPCWSTR lpcwstrFile, LPCWSTR lpcwstrPlayList)
{
    return S_OK;
}

// IMediaControl
HRESULT CFixedGraph::Run()
{
    return S_OK;
}

HRESULT CFixedGraph::Pause()
{
    return S_OK;
}

HRESULT CFixedGraph::Stop()
{
    return S_OK;
}

HRESULT CFixedGraph::GetState(LONG msTimeout, OAFilterState* pfs)
{
    return S_OK;
}

// IMediaSeeking
HRESULT CFixedGraph::IsFormatSupported(const GUID* pFormat)
{
    return !pFormat ? E_POINTER : *pFormat == TIME_FORMAT_FRAME ? S_OK : S_FALSE;
}

HRESULT CFixedGraph::GetTimeFormat(GUID* pFormat)
{
    CheckPointer(pFormat, E_POINTER);
    *pFormat = TIME_FORMAT_FRAME;
    return S_OK;
}

HRESULT CFixedGraph::GetDuration(LONGLONG* pDuration)
{
    return S_OK;
}

HRESULT CFixedGraph::GetCurrentPosition(LONGLONG* pCurrent)
{
    return S_OK;
}

HRESULT CFixedGraph::SetPositions(LONGLONG* pCurrent, DWORD dwCurrentFlags, LONGLONG* pStop, DWORD dwStopFlags)
{
    return E_INVALIDARG;
}

// IVideoWindow
HRESULT CFixedGraph::put_Visible(long Visible)
{
    return S_OK;
}

HRESULT CFixedGraph::get_Visible(long* pVisible)
{
    return S_OK;
}

HRESULT CFixedGraph::SetWindowPosition(long Left, long Top, long Width, long Height)
{
    return S_OK;
}

// IBasicVideo
HRESULT CFixedGraph::SetDestinationPosition(long Left, long Top, long Width, long Height)// {return E_NOTIMPL;}
{
    return S_OK;
}

HRESULT CFixedGraph::GetVideoSize(long* pWidth, long* pHeight)
{
    if (!pWidth || !pHeight) {
        return E_POINTER;
    }

    *pWidth = 640;
    *pHeight = 480;

    return S_OK;
}

// IBasicAudio
HRESULT CFixedGraph::put_Volume(long lVolume)
{
    HRESULT hr = S_OK;
    return hr;
}

HRESULT CFixedGraph::get_Volume(long* plVolume)
{
    CheckPointer(plVolume, E_POINTER);
    HRESULT hr = S_OK;
    return hr;
}

//
// 两个重要的主媒体类型GUID
//   73646976-0000-0010-8000-00AA00389B71  'vids' == MEDIATYPE_Video
//   73647561-0000-0010-8000-00AA00389B71  'auds' == MEDIATYPE_Audio
//
HRESULT CFixedGraph::ConnectFilters(IPin* pOut, IBaseFilter* pDest)
{
    HRESULT hr = S_OK;
    IPin* pIn = nullptr;
    IEnumMediaTypes* pEnum = nullptr;
    AM_MEDIA_TYPE* mt = nullptr;

    CHECK_HR(pOut->EnumMediaTypes(&pEnum));
    // 如果pOut是解码器的输出Pin，那么枚举结果可能是YV12或者RGB32?
    CHECK_HR(pEnum->Next(1, &mt, NULL)); 
    CHECK_HR(FindPinByMajorType(pDest, mt->majortype, PINDIR_INPUT, FALSE, &pIn));
    CHECK_HR(pOut->Connect(pIn, mt));

done:
    SAFE_RELEASE(pIn);
    SAFE_RELEASE(pEnum);
    _DeleteMediaType(mt);
    return hr;
}

HRESULT CFixedGraph::ConnectFilters(IBaseFilter* pSrc, IBaseFilter* pDest)
{
    HRESULT hr = S_OK;
    IPin* pOut = nullptr;

    CHECK_HR(FindUnconnectedPin(pSrc, PINDIR_OUTPUT, &pOut));
    CHECK_HR(ConnectFilters(pOut, pDest));

done:
    SAFE_RELEASE(pOut);
    return hr;
}

HRESULT CFixedGraph::DisconnectFilter(IBaseFilter* pSrc)
{
    HRESULT hr = S_OK;

    if (pSrc == nullptr)
        return E_INVALIDARG;

    do {
        IPin* pIn = nullptr;
        hr = FindConnectedPin(pSrc, PINDIR_INPUT, &pIn);
        if (SUCCEEDED(hr)) {
            hr = pIn->Disconnect();
            SAFE_RELEASE(pIn);
        }
    } while (SUCCEEDED(hr));

    do {
        IPin* pOut = nullptr;
        hr = FindConnectedPin(pSrc, PINDIR_OUTPUT, &pOut);
        if (SUCCEEDED(hr)) {
            hr = pOut->Disconnect();
            SAFE_RELEASE(pOut);
        }
    } while (SUCCEEDED(hr));

    return hr;
}

