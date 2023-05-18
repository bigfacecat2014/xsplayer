#include "stdafx.h"
#include "ProxyMediaSink.h"

ProxyMediaSink::ProxyMediaSink(UsageEnvironment& env, MediaSubsession& subsession,
    MediaPacketQueue& mediaPacketQueue, size_t receiveBufferSize, bool isNullSink)
    : MediaSink(env)
    , _receiveBufferSize(receiveBufferSize)
    , _receiveBuffer(new uint8_t[receiveBufferSize])
    , _subsession(subsession)
    , _mediaPacketQueue(mediaPacketQueue)
    , _isNullSink(isNullSink)
{
}

ProxyMediaSink::~ProxyMediaSink() { delete[] _receiveBuffer; }

void ProxyMediaSink::afterGettingFrame(void* clientData, uint32_t frameSize,
    uint32_t numTruncatedBytes, struct timeval presentationTime, uint32_t durationInMicroseconds)
{
    ProxyMediaSink* sink = static_cast<ProxyMediaSink*>(clientData);
    sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

void ProxyMediaSink::afterGettingFrame(uint32_t frameSize, uint32_t numTruncatedBytes,
    struct timeval presentationTime, uint32_t durationInMicroseconds)
{
    if (numTruncatedBytes == 0)
    {
        if (!_isNullSink) {
            bool isRtcpSynced = _subsession.rtpSource() && _subsession.rtpSource()->hasBeenSynchronizedUsingRTCP();
            _mediaPacketQueue.push(MediaPacketSample(_receiveBuffer, frameSize, presentationTime, isRtcpSynced));
        }
    }
    else
    {
    }
    continuePlaying();
}

Boolean ProxyMediaSink::continuePlaying()
{
    if (fSource == nullptr)
        return False;
    fSource->getNextFrame(_receiveBuffer, _receiveBufferSize, afterGettingFrame, this, onSourceClosure, this);
    return True;
}
