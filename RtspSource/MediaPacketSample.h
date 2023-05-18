#pragma once

#include "ConcurrentQueue.h"

class MediaPacketSample
{
public:
    /**
      * Construct an invalid media packet sample
      */
    MediaPacketSample() {}

    MediaPacketSample(std::uint8_t* buffer, size_t bufSize, timeval presentationTime,
                      bool isRtcpSynced)
        : _buffer(buffer, buffer + bufSize)
        , _presentationTime(presentationTime)
        , _isRtcpSynced(isRtcpSynced)
    {
    }

    MediaPacketSample(const MediaPacketSample&) = delete;
    MediaPacketSample& operator=(const MediaPacketSample&) = delete;

    /**
     * Move constructor
     */
    MediaPacketSample(MediaPacketSample&& other)
        : _buffer(std::move(other._buffer))
        , _presentationTime(other._presentationTime)
        , _isRtcpSynced(other._isRtcpSynced)
    {
    }

    /**
     * Move operator
     */
    MediaPacketSample& operator=(MediaPacketSample&& other)
    {
        if (this != &other)
        {
            _buffer = std::move(other._buffer);
            _presentationTime = other._presentationTime;
            _isRtcpSynced = other._isRtcpSynced;
        }
        return *this;
    }

    ~MediaPacketSample() {}

    bool invalid() const { return size() == 0; }
    size_t size() const { return _buffer.size(); }
    const std::uint8_t* data() const { return _buffer.data(); }
    std::uint8_t* data() { return _buffer.data(); }
    const timeval& presentationTime() const { return _presentationTime; }
    bool isRtcpSynced() const { return _isRtcpSynced; }

    int64_t timestamp() const
    {
        // Convert to DirectShow units (100ns units)
        return _presentationTime.tv_sec * 10000000i64 + _presentationTime.tv_usec * 10i64;
        // Watch out for overflows
    }

private:
    std::vector<std::uint8_t> _buffer;
    timeval _presentationTime;
    bool _isRtcpSynced;
};

typedef ConcurrentQueue<MediaPacketSample> MediaPacketQueue;
