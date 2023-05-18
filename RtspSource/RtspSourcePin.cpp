#include "stdafx.h"
#include "RtspSource.h"
#include "RtspSourcePin.h"
#include "MediaPacketSample.h"
#include "ConcurrentQueue.h"
#include "H265StreamParser.h"

const int constNALUStartCodesSize = 4;

//-------------------------------------------------------------------------------------------------
// 媒体类型解析辅助函数
//-------------------------------------------------------------------------------------------------
HRESULT GetMediaTypeH265(CMediaType& mediaType, MediaSubsession& mediaSubsession)
{
    char const* sPropVPSStr = mediaSubsession.fmtp_spropvps();
    char const* sPropSPSStr = mediaSubsession.fmtp_spropsps();
    char const* sPropPPSStr = mediaSubsession.fmtp_sproppps();

    SPropRecord* sPropRecords[3] = { nullptr, nullptr, nullptr };
    uint32_t numSPropRecords[3] = { 0, 0, 0 };
    sPropRecords[0] = ::parseSPropParameterSets(sPropVPSStr, numSPropRecords[0]);
    sPropRecords[1] = ::parseSPropParameterSets(sPropSPSStr, numSPropRecords[1]);
    sPropRecords[2] = ::parseSPropParameterSets(sPropPPSStr, numSPropRecords[2]);

    size_t decoderSpecificSize = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (sPropRecords[i] == nullptr)
            continue;

        decoderSpecificSize += 4; // 用于写入NALU单元的起始标志(0x00 0x00 0x00 0x01)

        int propNum = numSPropRecords[i]; // 第i个属性集的属性个数
        for (int j = 0; j < propNum; ++j)
        {
            const SPropRecord& prop = sPropRecords[i][j];
            decoderSpecificSize += prop.sPropLength; // 累加所有属性集的所有属性的长度
        }
    }

    // "Hide" decoder specific data in FormatBuffer
    VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)mediaType.AllocFormatBuffer(sizeof(VIDEOINFOHEADER2) + decoderSpecificSize);
    if (vih2 == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(vih2, sizeof(VIDEOINFOHEADER2) + decoderSpecificSize);

    uint32_t videoWidth = 0, videoHeight = 0;
    double videoFramerate = 0.0;

    // Move decoder specific data after FormatBuffer
    BYTE* decoderSpecific = (BYTE*)(vih2 + 1);
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (sPropRecords[i] == nullptr) // 缺失了一个预期的NALU
            continue;

        // 先写入一个NALU单元的起始标记(0x00,0x00,0x00,0x01)
        ((uint32_t*)decoderSpecific)[0] = 0x01000000;
        decoderSpecific += 4;

        uint32_t propNum = numSPropRecords[i]; // 每种属性集的属性个数（三种属性集：VPS、SPS、PPS）
        bool is_ps_ok = false;
        for (uint32_t j = 0; j < propNum; j++)
        {
            SPropRecord& prop = sPropRecords[i][j];
            BYTE code = prop.sPropBytes[0]; // 高1位是禁止位，低1位是nuh_reserved_zero_6bits（保留给LayerId ）的高1位，后一个字节的低3位是nuh_temporal_id_plus1
            BYTE type = (code & 0x7E) >> 1; // H.265 NALU has two bytes, and its format is different from H.264 NALU.第一个字节中间的6个bit就是NALU Type
            // VPS == 32,SPS == 33,PPS == 34
            if (type == 32) // VPS
            {
                H265VPSParser ps(prop.sPropBytes, prop.sPropLength);
                double fr = ps.GetFramerate();
                uint32_t vw = ps.GetWidth();
                uint32_t vh = ps.GetHeight();
                ;
            }
            else if (type == 33) // SPS
            {
                H265SPSParser ps(prop.sPropBytes, prop.sPropLength);
                videoWidth = ps.GetWidth();
                videoHeight = ps.GetHeight();
                videoFramerate = ps.GetFramerate();
                if (fabs(videoFramerate) <= 1e-6)
                    videoFramerate = 25.0f;
            }
            else if (type == 34) // PPS
            {

            }

            // 写入该NALU属性集的一个属性
            memcpy(decoderSpecific, prop.sPropBytes, prop.sPropLength);
            decoderSpecific += prop.sPropLength;
        }
    }

    delete[] sPropRecords[0];
    delete[] sPropRecords[1];
    delete[] sPropRecords[2];

    SetRect(&vih2->rcSource, 0, 0, videoWidth, videoHeight);
    SetRect(&vih2->rcTarget, 0, 0, videoWidth, videoHeight);
    // Hikon的IPC给出的RTSP点播时，竟然什么vps|sps信息都没有，全靠ffmpeg解码器从码流中分析出实际的宽高。
    // TODO:若不解码能否，从NALU中自己解析vps，而不是靠live555中的RTSP的信息？
    if (std::abs(videoFramerate) <= 1e-6) {
        videoFramerate = 25.0f;
    }
    REFERENCE_TIME timePerFrame = (REFERENCE_TIME)(UNITS_IN_100NS / videoFramerate);
    vih2->AvgTimePerFrame = timePerFrame;
    vih2->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih2->bmiHeader.biWidth = videoWidth;
    vih2->bmiHeader.biHeight = videoHeight;
    vih2->bmiHeader.biCompression = MAKEFOURCC('H', '2', '6', '5');

    mediaType.SetType(&MEDIATYPE_Video);
    mediaType.SetSubtype(&MEDIASUBTYPE_HEVC);
    mediaType.SetFormatType(&FORMAT_VideoInfo2);
    mediaType.SetTemporalCompression(TRUE);
    mediaType.SetSampleSize(0);

    return S_OK;
}

HRESULT GetMediaTypeAAC(CMediaType& mediaType, MediaSubsession& mediaSubsession)
{
    // fmtp_configuration() looks like 1490. We need to convert it to 0x14 0x90
    const char* configuration = mediaSubsession.fmtp_configuration();
    int decoderSpecificSize = strlen(configuration) / 2;
    std::vector<uint8_t> decoderSpecific(decoderSpecificSize);
    for (int i = 0; i < decoderSpecificSize; ++i)
    {
        char hexStr[] = { configuration[2 * i], configuration[2 * i + 1], '\0' };
        uint8_t hex = static_cast<uint8_t>(strtoul(hexStr, nullptr, 16));
        decoderSpecific[i] = hex;
    }

    const int samplingFreqs[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
                                    16000, 12000, 11025, 8000,  7350,  0,     0,     0 };

    const size_t waveFormatBufferSize = sizeof(WAVEFORMATEX) + decoderSpecificSize;
    WAVEFORMATEX* pWave = (WAVEFORMATEX*)mediaType.AllocFormatBuffer(waveFormatBufferSize);
    if (!pWave)
        return E_OUTOFMEMORY;
    ZeroMemory(pWave, waveFormatBufferSize);

    pWave->wFormatTag = WAVE_FORMAT_RAW_AAC1;
    pWave->nChannels = (decoderSpecific[1] & 0x78) >> 3;
    pWave->nSamplesPerSec =
        samplingFreqs[((decoderSpecific[0] & 0x7) << 1) + ((decoderSpecific[1] & 0x80) >> 7)];
    pWave->nBlockAlign = 1;
    // pWave->nAvgBytesPerSec = 0;
    // pWave->wBitsPerSample = 16; // Can be 0 I guess
    pWave->cbSize = decoderSpecificSize;
    CopyMemory(pWave + 1, decoderSpecific.data(), decoderSpecificSize);

    mediaType.SetType(&MEDIATYPE_Audio);
    mediaType.SetSubtype(&MEDIASUBTYPE_RAW_AAC1);
    mediaType.SetFormatType(&FORMAT_WaveFormatEx);
    // mediaType.SetSampleSize(256000); // Set to 1 on InitMedia
    mediaType.SetTemporalCompression(FALSE);

    return S_OK;
}

// Works only for H264/AVC1 || H265/HEVC
bool IsIdrFrame(const MediaPacketSample& mediaPacket)
{
    const uint8_t* data = mediaPacket.data();
    // Take 5 LSBs and compare with 5 (IDR)
    // More NAL types:
    // http://gentlelogic.blogspot.com/2011/11/exploring-h264-part-2-h264-bitstream.html
    return (data[0] & 0x1F) == 5;
}


//-------------------------------------------------------------------------------------------------
// RtspSourcePin implementation
//-------------------------------------------------------------------------------------------------
HRESULT RtspSourcePin::OnThreadCreate()
{
    fprintf(stderr, "%S pin: %s\n", m_pName, __FUNCTION__);
    return __super::OnThreadCreate();
}

HRESULT RtspSourcePin::OnThreadDestroy()
{
    fprintf(stderr, "%S pin: %s\n", m_pName, __FUNCTION__);
    return __super::OnThreadDestroy();
}

HRESULT RtspSourcePin::OnThreadStartPlay()
{
    fprintf(stderr, "%S pin: %s\n", m_pName, __FUNCTION__);
    ResetTimeBaselines();
    return __super::OnThreadStartPlay();
}

void RtspSourcePin::ResetTimeBaselines()
{
    // Desynchronize with RTP timestamps
    _firstSample = true;
    _rtcpSynced = false;
    _rtpPresentationTimeBaseline = 0;
    _streamTimeBaseline = 0;
    _currentPlayTime = 0;
}

REFERENCE_TIME RtspSourcePin::SynchronizeTimestamp(const MediaPacketSample& mediaSample)
{
    auto SyncWithMediaSample = [this](const MediaPacketSample& mediaSample)
    {
        CRefTime streamTime;
        m_pFilter->StreamTime(streamTime);
        uint32_t latencyMSecs = static_cast<CRtspSource*>(m_pFilter)->_latencyMSecs;
        _streamTimeBaseline = streamTime.GetUnits() + latencyMSecs * 10000i64;
        _rtpPresentationTimeBaseline = mediaSample.timestamp();
    };

    if (_firstSample)
    {
        SyncWithMediaSample(mediaSample);

        _firstSample = false;
        // If we're lucky the first sample is also synced using RTCP
        _rtcpSynced = mediaSample.isRtcpSynced();
    }
    // First sample wasn't RTCP sync'ed, try the next time
    else if (!_rtcpSynced)
    {
        _rtcpSynced = mediaSample.isRtcpSynced();
        if (_rtcpSynced)
            SyncWithMediaSample(mediaSample);
    }

    return mediaSample.timestamp() - _rtpPresentationTimeBaseline + _streamTimeBaseline;
}

REFERENCE_TIME RtspSourcePin::SynchronizeTimestamp2()
{
    if (_firstSample)
    {
        _firstSample = false;
        _rtcpSynced = true; // 无需RTCP时钟同步机制，以本机呈现时钟为准。
        _streamTimeBaseline = timeGetTime();
        DWORD delayMS = static_cast<CRtspSource*>(m_pFilter)->_latencyMSecs;
        _rtpPresentationTimeBaseline = _streamTimeBaseline + delayMS;
    }
    else {
        VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)_mediaType.Format();
        DWORD dynamicFrameIntervalMS = (DWORD)(vih2->AvgTimePerFrame / 10000);
        _rtpPresentationTimeBaseline += dynamicFrameIntervalMS;
    }
    return _rtpPresentationTimeBaseline;
}

HRESULT RtspSourcePin::GetMediaType(CMediaType* pMediaType)
{
    // We only support one MediaType - the one that is streamed
    CheckPointer(pMediaType, E_POINTER);
    CAutoLock cAutoLock(m_pFilter->pStateLock());
    FreeMediaType(*pMediaType);
    return CopyMediaType(pMediaType, &_mediaType);
}


//-------------------------------------------------------------------------------------------------
// RtspH265SourcePin implementation
//-------------------------------------------------------------------------------------------------
RtspH265SourcePin::RtspH265SourcePin(HRESULT* phr, CSource* pFilter, MediaPacketQueue* mediaPacketQueue)
    : RtspSourcePin(TEXT(""), phr, pFilter, L"")
{
    HRESULT hr = S_OK;

    _mediaPacketQueue = mediaPacketQueue;
    _mediaType.InitMediaType();
    SetH265VideoType(_mediaType, 1, 1, 25);
    _codecFourCC = DWORD('H265');
    if (phr)
        *phr = hr;
}

void RtspH265SourcePin::ResetMediaSubsession(MediaSubsession* mediaSubsession) 
{ 
    _mediaSubsession = mediaSubsession; 
    HRESULT hr = GetMediaTypeH265(_mediaType, *_mediaSubsession);
    {
        VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)_mediaType.Format();
        _initAvgTimePerFrame = vih2->AvgTimePerFrame;
        DWORD frameInterval = (DWORD)(_initAvgTimePerFrame / 10000); // 百纳秒转毫秒单位
        static_cast<CRtspSource*>(m_pFilter)->Fire_AvgFrameIntervalChanged(frameInterval);
    }
    _sendMediaType = true;
}

HRESULT RtspH265SourcePin::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pRequest)
{
    CheckPointer(pAlloc, E_POINTER);
    CheckPointer(pRequest, E_POINTER);

    CAutoLock cAutoLock(m_pFilter->pStateLock());
    {
        // Ensure a minimum number of buffers
        if (pRequest->cBuffers == 0)
            pRequest->cBuffers = ALLOCATOR_BUF_COUNT;
        pRequest->cbBuffer = ALLOCATOR_BUF_SIZE; // Should be more than enough
    }

    ALLOCATOR_PROPERTIES Actual;
    HRESULT hr = pAlloc->SetProperties(pRequest, &Actual);
    if (FAILED(hr))
        return hr;
    // Is this allocator unsuitable?
    if (Actual.cbBuffer < pRequest->cbBuffer)
        return E_FAIL;
    return S_OK;
}

HRESULT RtspH265SourcePin::FillBuffer(IMediaSample* pSample)
{
    MediaPacketSample mediaSample;
    _mediaPacketQueue->pop(mediaSample);
    if (mediaSample.invalid())
    {
        fprintf(stderr, "%S pin: End of streaming!\n", m_pName);
        return S_FALSE;
    }

    BYTE* pData;
    HRESULT hr = pSample->GetPointer(&pData);
    if (FAILED(hr))
        return hr;
    long length = pSample->GetSize();

    // Append VPS SPS and PPS to the first packet (they come out-band)
    if (_firstSample)
    {
        // Retrieve them from media type format buffer
        BYTE* decoderSpecific = (BYTE*)(((VIDEOINFOHEADER2*)_mediaType.Format()) + 1);
        ULONG decoderSpecificLength = _mediaType.FormatLength() - sizeof(VIDEOINFOHEADER2);
        memcpy_s(pData, length, decoderSpecific, decoderSpecificLength);
        pData += decoderSpecificLength;
        length -= decoderSpecificLength;
    }

    // Append 4-byte start code 00 00 00 01 in network byte order that precedes each NALU
    ((uint32_t*)pData)[0] = 0x01000000;
    pData += constNALUStartCodesSize;
    length -= constNALUStartCodesSize;

    // Finally copy media packet contens to IMediaSample
    memcpy_s(pData, length, mediaSample.data(), mediaSample.size());
    pSample->SetActualDataLength(mediaSample.size() + constNALUStartCodesSize);
    pSample->SetSyncPoint(IsIdrFrame(mediaSample));

    // 将最新的媒体类型设置在样本中，传递给下游解码器。
    if (_sendMediaType) {
        pSample->SetMediaType(&_mediaType);
        _sendMediaType = false;
    }

    // 实时源的时间戳用不着服务器给，来多少帧放多少帧，保证呈现时间间隔均匀平滑即可。
    REFERENCE_TIME ts = SynchronizeTimestamp2(); // commented by yxs
    pSample->SetTime(&ts, NULL);
#ifdef _DEBUG    
    //fprintf(stderr, "ts=%u\n", (DWORD)ts);
#endif

    // Calculate current play time (does not include offset from initial time seek)
    CRefTime streamTime;
    m_pFilter->StreamTime(streamTime);
    uint32_t latencyMSecs = static_cast<CRtspSource*>(m_pFilter)->_latencyMSecs;
    _currentPlayTime = streamTime.GetUnits() - (_streamTimeBaseline - latencyMSecs * 10000i64);

    return S_OK;
}

//-------------------------------------------------------------------------------------------------
// RtspAACSourcePin implementation
//
// https://blog.csdn.net/zymill/article/details/78778540
// AAC音频编码格式，完整名称叫做"高级音频编码（Advanced Audio Codec）”。
// AAC音频编码技术早在1997年就制定成型，当时在MPEG-2中作为了MPEG2-AAC音频编码规格之一。
// 后来，在2000年被用在MPEG-4中（ISO 14496-3 Audio），所以现在变更为MPEG-4 AAC标准。
// 也就是说，AAC已经成为MPEG4家族的主要成员之一，它是MPEG4第三部分中的音频编码系统。
// AAC可提供最多48个全音域音频通道。
// 其中，AAC音频编码在不同的领域，主要分为九种规格：
//   1.MPEG-2 AAC Main
//   2.MPEG-2 AAC LC(Low Complexity)
//   3.MPEG-2 AAC SSR(Scalable Sampling Rate)
//   4.MPEG-4 AAC Main
//  *5.MPEG-4 AAC LC(Low Complexity)
//   6.MPEG-4 AAC SSR(Scalable Sample Rate)
//   7.MPEG-4 AAC LTP(Long Term Predicition)
//  *8.MPEG-4 AAC LD(Low Delay)
//  *9.MPEG-4 AAC HE(High Efficiency) AACPlusV1 / V2(3GPP)
//
//-------------------------------------------------------------------------------------------------
RtspAACSourcePin::RtspAACSourcePin(HRESULT* phr, CSource* pFilter,
    MediaSubsession* mediaSubsession, MediaPacketQueue* mediaPacketQueue)
    : RtspSourcePin(TEXT("RtspSourcePin"), phr, pFilter, L"Audio")
{
    _mediaSubsession = mediaSubsession;
    _mediaPacketQueue = mediaPacketQueue;

    assert(0 == strcmp(_mediaSubsession->mediumName(), "audio"));
    assert(0 == strcmp(_mediaSubsession->codecName(), "MPEG4-GENERIC"));

    _mediaType.InitMediaType();
    HRESULT hr = GetMediaTypeAAC(_mediaType, *_mediaSubsession);
    _codecFourCC = DWORD('mp4a');

    if (phr)
        *phr = hr;
}

void RtspAACSourcePin::ResetMediaSubsession(MediaSubsession* mediaSubsession)
{
    _mediaSubsession = mediaSubsession;
}

HRESULT RtspAACSourcePin::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pRequest)
{
    CheckPointer(pAlloc, E_POINTER);
    CheckPointer(pRequest, E_POINTER);

    CAutoLock cAutoLock(m_pFilter->pStateLock());
    {
        // Ensure a minimum number of buffers
        if (pRequest->cBuffers == 0)
            pRequest->cBuffers = ALLOCATOR_BUF_COUNT;
        pRequest->cbBuffer = ALLOCATOR_BUF_SIZE;
    }

    ALLOCATOR_PROPERTIES Actual;
    HRESULT hr = pAlloc->SetProperties(pRequest, &Actual);
    if (FAILED(hr))
        return hr;
    // Is this allocator unsuitable?
    if (Actual.cbBuffer < pRequest->cbBuffer)
        return E_FAIL;
    return S_OK;
}

HRESULT RtspAACSourcePin::FillBuffer(IMediaSample* pSample)
{
    MediaPacketSample mediaSample;
    _mediaPacketQueue->pop(mediaSample);
    if (mediaSample.invalid())
    {
        fprintf(stderr, "%S pin: End of streaming!\n", m_pName);
        return S_FALSE;
    }

    BYTE* pData;
    HRESULT hr = pSample->GetPointer(&pData);
    if (FAILED(hr))
        return hr;
    long length = pSample->GetSize();
    {
        // No appending - just copy raw data
        memcpy_s(pData, length, mediaSample.data(), mediaSample.size());
        pSample->SetActualDataLength(mediaSample.size());
        pSample->SetSyncPoint(FALSE);
    }

    REFERENCE_TIME ts = SynchronizeTimestamp(mediaSample); // commented by yxs
    pSample->SetTime(&ts, NULL);

    // Calculate current play time (does not include offset from initial time seek)
    CRefTime streamTime;
    m_pFilter->StreamTime(streamTime);
    uint32_t latencyMSecs = static_cast<CRtspSource*>(m_pFilter)->_latencyMSecs;
    _currentPlayTime = streamTime.GetUnits() - (_streamTimeBaseline - latencyMSecs * 10000i64);

    return S_OK;
}
