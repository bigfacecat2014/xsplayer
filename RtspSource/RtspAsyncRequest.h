#pragma once

#include "IRtspSource.h"
#include <system_error>
#include <future>
#include <string>

typedef std::future<RtspSource::ErrorCode> RtspFutureResult;

class RtspAsyncRequest
{
public:
    explicit RtspAsyncRequest(RtspSource::OpCode oc, const std::string& arg)
    {
        _opCode = oc;
        _arg = arg;
    }
    ~RtspAsyncRequest() {}
    RtspAsyncRequest() : _opCode(RtspSource::Unknown) {}
    RtspAsyncRequest(const RtspAsyncRequest&) = delete;
    RtspAsyncRequest& operator=(const RtspAsyncRequest&) = delete;
    RtspAsyncRequest(RtspAsyncRequest&& other) { *this = std::move(other); }
    RtspAsyncRequest& operator=(RtspAsyncRequest&& other)
    {
        if (this != &other)
        {
            _opCode = other._opCode;
            _arg = std::move(other._arg);
            _promise = std::move(other._promise);
        }
        return *this;
    }

    RtspSource::OpCode GetOpCode() const { return _opCode; }
    const std::string& GetArg() const { return _arg; }

    void SetValue(RtspSource::ErrorCode ec) {  _promise.set_value(ec); }
    RtspFutureResult GetFutureResult() { return _promise.get_future(); }

    void SetArg(const std::string& a) { _arg = a; }

private:
    RtspSource::OpCode _opCode; // 异步操作码
    std::string _arg; // 异步操作参数
    std::promise<RtspSource::ErrorCode> _promise; // 承诺给出一个在未来给出的错误码。
};

inline const char* GetRtspAsyncRequestTypeString(RtspSource::OpCode oc)
{
#ifdef _DEBUG
    switch (oc)
    {
    case RtspSource::Open: return "Open";
    case RtspSource::Play: return "Play";
    case RtspSource::Stop: return "Stop";
    case RtspSource::Reconnect: return "Reconnect";
    case RtspSource::Done: return "Done";
    case RtspSource::Unknown:
    default: return "Unknown";
    }
#else
    return "";
#endif
}