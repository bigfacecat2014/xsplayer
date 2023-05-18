//-------------------------------------------------------------------------------------------------
// 灵活抽象都是以增加不确定性和理解成本为代价的。
// 固定功能流水线，不求灵活可编程，但求确定和稳固。
//-------------------------------------------------------------------------------------------------
#pragma once
#include "BaseGraph.h"

class CFixedGraph : public CBaseGraph
{
public:
    CFixedGraph(HWND hwndHost, HRESULT& hr);
    virtual ~CFixedGraph();

    // IGraphBuilder
    STDMETHODIMP RenderFile(LPCWSTR lpcwstrFile, LPCWSTR lpcwstrPlayList);

    // IMediaControl
    STDMETHODIMP Run();
    STDMETHODIMP Pause();
    STDMETHODIMP Stop();
    STDMETHODIMP GetState(LONG msTimeout, OAFilterState* pfs);

    // IMediaSeeking
    STDMETHODIMP IsFormatSupported(const GUID* pFormat);
    STDMETHODIMP GetTimeFormat(GUID* pFormat);
    STDMETHODIMP GetDuration(LONGLONG* pDuration);
    STDMETHODIMP GetCurrentPosition(LONGLONG* pCurrent);
    STDMETHODIMP SetPositions(LONGLONG* pCurrent, DWORD dwCurrentFlags, LONGLONG* pStop, DWORD dwStopFlags);

    // IVideoWindow
    STDMETHODIMP put_Visible(long Visible);
    STDMETHODIMP get_Visible(long* pVisible);
    STDMETHODIMP SetWindowPosition(long Left, long Top, long Width, long Height);

    // IBasicVideo
    STDMETHODIMP SetDestinationPosition(long Left, long Top, long Width, long Height);
    STDMETHODIMP GetVideoSize(long* pWidth, long* pHeight);

    // IBasicAudio
    STDMETHODIMP put_Volume(long lVolume);
    STDMETHODIMP get_Volume(long* plVolume);

    HRESULT ConnectFilters(IPin* pOut, IBaseFilter* pDest);
    HRESULT ConnectFilters(IBaseFilter* pSrc, IBaseFilter* pDest);
    HRESULT DisconnectFilter(IBaseFilter* pSrc);

protected:
    HWND _hwndHost = 0;
};

