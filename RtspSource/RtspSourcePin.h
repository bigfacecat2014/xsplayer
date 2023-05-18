#pragma once

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "ConcurrentQueue.h"
#include "RtspAsyncRequest.h"
#include "MediaPacketSample.h"
#include "IRtspSource.h"

// TODO:终极解决办法：采用组合，采用组件的编程思想重构。
// 根据特性，编写逻辑处理组件。一个组件实现一个不可再分的基本特性。
// 不同特性之间的任意组合关系采用决策树来完成。
class RtspSourcePin : public CSourceStream
{
public:
    RtspSourcePin(LPCTSTR pObjectName, HRESULT* phr, CSource* pms, LPCWSTR pName)
        : CSourceStream(pObjectName, phr, pms, pName) {}
    void ResetTimeBaselines();
    REFERENCE_TIME CurrentPlayTime() const { return _currentPlayTime; }

    HRESULT GetMediaType(CMediaType* pMediaType) override;
    STDMETHODIMP Notify(IBaseFilter* pSelf, Quality q) override { return E_FAIL; }

protected:
    HRESULT OnThreadCreate() override;
    HRESULT OnThreadDestroy() override;
    HRESULT OnThreadStartPlay() override;
    REFERENCE_TIME SynchronizeTimestamp(const MediaPacketSample& mediaSample);
    REFERENCE_TIME SynchronizeTimestamp2();

protected:
    REFERENCE_TIME _currentPlayTime = 0;
    REFERENCE_TIME _rtpPresentationTimeBaseline = 0;
    REFERENCE_TIME _streamTimeBaseline = 0;
    bool _firstSample = true;
    bool _rtcpSynced = false;

    MediaSubsession* _mediaSubsession = nullptr;
    MediaPacketQueue* _mediaPacketQueue = nullptr; // weak_ptr
    CMediaType _mediaType;
    REFERENCE_TIME _initAvgTimePerFrame = 0; // 单位：毫秒
    DWORD _codecFourCC = 0;
    bool _sendMediaType = false; // 是否需要在样本中附带最新的媒体类型信息。
};

class RtspH265SourcePin : public RtspSourcePin
{
public:
    enum { ALLOCATOR_BUF_SIZE = 256 * 1024 };
    enum { ALLOCATOR_BUF_COUNT = 10 }; // 队列延时最大10 * 40ms < 500ms

    RtspH265SourcePin(HRESULT* phr, CSource* pFilter, MediaPacketQueue* mediaPacketQueue);
    void ResetMediaSubsession(MediaSubsession* mediaSubsession);
    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pRequest) override;
    HRESULT FillBuffer(IMediaSample* pSample) override;
};

class RtspAACSourcePin : public RtspSourcePin
{
public:
    enum { ALLOCATOR_BUF_SIZE = 8 * 1024 };
    enum { ALLOCATOR_BUF_COUNT = 10 };

    RtspAACSourcePin(HRESULT* phr, CSource* pFilter, MediaSubsession* mediaSubsession, MediaPacketQueue* mediaPacketQueue);
    void ResetMediaSubsession(MediaSubsession* mediaSubsession);
    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pRequest) override;
    HRESULT FillBuffer(IMediaSample* pSample) override;
};