#pragma once

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#include "MediaPacketSample.h"
#include "RtspSource.h"

/*
 * Media sink that accumulates received frames into given queue
 */
class ProxyMediaSink : public MediaSink
{
public:
    ProxyMediaSink(UsageEnvironment& env, MediaSubsession& subsession,
                   MediaPacketQueue& mediaPacketQueue, size_t receiveBufferSize, bool isNullSink);
    virtual ~ProxyMediaSink();

    static void afterGettingFrame(void* clientData, uint32_t frameSize, uint32_t numTruncatedBytes,
                                  struct timeval presentationTime, uint32_t durationInMicroseconds);

    void afterGettingFrame(uint32_t frameSize, uint32_t numTruncatedBytes,
                           struct timeval presentationTime, uint32_t durationInMicroseconds);

private:
    virtual Boolean continuePlaying();

private:
    size_t _receiveBufferSize = 0;
    uint8_t* _receiveBuffer = nullptr;
    MediaSubsession& _subsession;
    MediaPacketQueue& _mediaPacketQueue;
    bool _isNullSink = false; // 空接收器，什么也不会记住。
};
