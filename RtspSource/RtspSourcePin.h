#pragma once

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "ConcurrentQueue.h"
#include "RtspAsyncRequest.h"
#include "MediaPacketSample.h"
#include "IRtspSource.h"

// TODO:�ռ�����취��������ϣ���������ı��˼���ع���
// �������ԣ���д�߼����������һ�����ʵ��һ�������ٷֵĻ������ԡ�
// ��ͬ����֮���������Ϲ�ϵ���þ���������ɡ�
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
    REFERENCE_TIME _initAvgTimePerFrame = 0; // ��λ������
    DWORD _codecFourCC = 0;
    bool _sendMediaType = false; // �Ƿ���Ҫ�������и������µ�ý��������Ϣ��
};

class RtspH265SourcePin : public RtspSourcePin
{
public:
    enum { ALLOCATOR_BUF_SIZE = 256 * 1024 };
    enum { ALLOCATOR_BUF_COUNT = 10 }; // ������ʱ���10 * 40ms < 500ms

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