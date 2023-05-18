#pragma once

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "ConcurrentQueue.h"
#include "RtspAsyncRequest.h"
#include "MediaPacketSample.h"
#include "IRtspSource.h"

class RtspSourcePin;
class RtspH265SourcePin;
class RtspAACSourcePin;

class CRtspSource : public CSource, public RtspSource::ICommand
{
public:

    // live555取流状态。
    // 注意：只能在live555的schedule loop线程内使用这些状态。
    enum class State
    {
        Initial,
        SettingUp,
        ReadyToPlay,
        Playing,
        Reconnecting
    };

    static CUnknown* WINAPI CreateInstance(IUnknown* pUnk, HRESULT* phr);

    CRtspSource(IUnknown* pUnk, HRESULT* phr);
    virtual ~CRtspSource();

    CRtspSource(const CRtspSource&) = delete;
    CRtspSource& operator=(const CRtspSource&) = delete;

    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;

    STDMETHODIMP GetState(DWORD dwMSecs, __out FILTER_STATE* State) override;

    // CBaseFilter
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME tStart) override;

    // RtspSource::ICommand
    STDMETHOD_(void, SetChannelId(int channel));
    STDMETHODIMP_(void) SetInitialSeekTime(DOUBLE secs);
    STDMETHODIMP_(void) SetStreamingOverTcp(BOOL streamOverTcp);
    STDMETHODIMP_(void) SetTunnelingOverHttpPort(WORD tunnelOverHttpPort);
    STDMETHODIMP_(void) SetAutoReconnectionPeriod(DWORD dwMSecs);
    STDMETHODIMP_(void) SetLatency(DWORD dwMSecs);
    STDMETHODIMP_(void) SetSendLivenessCommand(BOOL sendLiveness);
    STDMETHODIMP_(void) SetNotifyReceiver(RtspSource::INotify* receiver);
    STDMETHOD(OpenURL(PCWSTR url, PCWSTR userName, PCWSTR password));

    void Fire_AvgFrameIntervalChanged(DWORD frameInterval);

    STDMETHODIMP QueryInterface(REFIID riid, __deref_out void** ppv) {
        return GetOwner()->QueryInterface(riid, ppv);
    };
    STDMETHODIMP_(ULONG) AddRef() {
        ULONG r = GetOwner()->AddRef();
        return r;
    };
    STDMETHODIMP_(ULONG) Release() {
        ULONG r = GetOwner()->Release();
        return r;
    };

private:
    friend class RtspSourcePin;
    friend class RtspH265SourcePin;
    friend class RtspAACSourcePin;

    // 在此套间线程执行的方法
    RtspFutureResult PostAsyncRequest(RtspSource::OpCode oc, const std::string& arg) {
        RtspAsyncRequest req(oc, arg);
        RtspFutureResult result(req.GetFutureResult()); // 先从承诺书中复制出用于兑现未来的凭据
        _requestQueue.push(std::move(req)); // 再掏空入队列
        return result;
    }
    RtspFutureResult AsyncOpenUrl(const std::string& url) {
        return PostAsyncRequest(RtspSource::Open, url);
    }
    RtspFutureResult AsyncPlay() {
        return PostAsyncRequest(RtspSource::Play, "");
    }
    RtspFutureResult AsyncShutdown() {
        return PostAsyncRequest(RtspSource::Stop, "");
    }
    RtspFutureResult AsyncReconnect() {
        return PostAsyncRequest(RtspSource::Reconnect, "");
    }

    // 在live555 scheduler工作线程上下文中执行的方法
    void OpenUrl(const std::string& url);
    void Play();
    void Shutdown();
    void Reconnect();
    void CloseSession();
    void CloseClient();
    void SetupSubsession();
    bool ScheduleNextReconnect();
    void DescribeRequestTimeout();
    void UnscheduleAllDelayedTasks();

    // Thin proxies for real handlers
    static void HandleOptionsResponse_Liveness(RTSPClient* client, int resultCode, char* resultString);
    static void HandleDescribeResponse(RTSPClient* client, int resultCode, char* resultString);
    static void HandleSetupResponse(RTSPClient* client, int resultCode, char* resultString);
    static void HandlePlayResponse(RTSPClient* client, int resultCode, char* resultString);

    // Called when a stream's subsession (e.g., audio or video substream) ends
    static void HandleSubsessionFinished(void* clientData);
    // Called when a RTCP "BYE" is received for a subsession
    static void HandleSubsessionByeHandler(void* clientData);
    // Called at the end of a stream's expected duration
    // (if the stream has not already signaled its end using a RTCP "BYE")
    static void CheckInterPacketGaps(void* clientData);
    static void Reconnect(void* clientData);
    static void DescribeRequestTimeout(void* clientData);
    static void SendLivenessCommand(void* clientData);
    static void HandleMediaEnded(void* clientData);

    // "Real" handlers
    void HandleOptionsResponse_Liveness(int resultCode, char* resultString);
    void HandleDescribeResponse(int resultCode, char* resultString);
    void HandleSetupResponse(int resultCode, char* resultString);
    void HandlePlayResponse(int resultCode, char* resultString);
    void CheckInterPacketGaps();

    void WorkerThread();

private:
    int _channelId = -1;
    RtspSource::INotify* _notifyReceiver = nullptr;
    RtspH265SourcePin* _h265Pin = nullptr;
    RtspAACSourcePin* _aacPin = nullptr;
    MediaPacketQueue _h265MediaPacketQueue;
    MediaPacketQueue _aacMediaPacketQueue;

    bool _streamOverTcp;
    uint16_t _tunnelOverHttpPort;
    uint32_t _autoReconnectionMSecs;
    std::mutex _criticalSection;
    uint32_t _latencyMSecs;
    bool _sendLivenessCommand;

    volatile State _state;

    struct env_deleter
    {
        void operator()(BasicUsageEnvironment* ptr) const { ptr->reclaim(); }
    };
    std::unique_ptr<BasicTaskScheduler0> _scheduler;
    std::unique_ptr<BasicUsageEnvironment, env_deleter> _env;

    Authenticator _authenticator;
    std::wstring _rtspUrlOrigin;
    std::string _rtspUrl;

    uint32_t _sessionTimeout;
    uint32_t _totNumPacketsReceived;
    TaskToken _interPacketGapCheckTimerTask;
    TaskToken _reconnectionTimerTask;
    TaskToken _firstCallTimeoutTask;
    TaskToken _livenessCommandTask;
    TaskToken _sessionTimerTask;

    class RtspClient* _rtsp;
    int _numSubsessions;

    double _sessionDuration;
    double _initialSeekTime;
    double _endTime;

    ConcurrentQueue<RtspAsyncRequest> _requestQueue;
    RtspAsyncRequest _currentRequest;
    ConcurrentQueue<RtspAsyncRequest> _completeQueue;
    std::thread _workerThread;
};
