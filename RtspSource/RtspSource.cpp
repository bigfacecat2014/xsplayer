#include "stdafx.h"
#include "IRtspSource.h"
#include "RtspSource.h"
#include "RtspSourcePin.h"
#include "ProxyMediaSink.h"
#include "GroupsockHelper.hh"

#ifdef _DEBUG
#define RTSP_CLIENT_VERBOSITY_LEVEL 1
#else
#define RTSP_CLIENT_VERBOSITY_LEVEL 0
#endif

namespace
{
    typedef int millisecond_t;

    const char* RtspClientAppName = "";
    const int RtspClientVerbosityLevel = RTSP_CLIENT_VERBOSITY_LEVEL;
    const millisecond_t defaultLatency = 0;
    const int recvBufferVideo = RtspH265SourcePin::ALLOCATOR_BUF_SIZE; // 太小会导致尺寸较大的高清I帧丢失引起的花屏。
    const int recvBufferAudio = RtspAACSourcePin::ALLOCATOR_BUF_SIZE;
    const millisecond_t packetReorderingThresholdTime = 0; // TCP不需要重新排列
    const millisecond_t interPacketGapMaxTime = 2 * 1000; // 两个媒体包的时间间隔不得超过这个阈值,否则执行断线重连。
    const millisecond_t firstCallTimeoutTime = 2 * 1000;
    const bool forceMulticastOnUnspecified = false;

    bool IsSubsessionSupported(MediaSubsession& mediaSubsession);
    void SetThreadName(DWORD dwThreadID, char* threadName);
}

class RtspClient : public ::RTSPClient
{
public:
    static RtspClient* CreateRtspClient(CRtspSource* filter, UsageEnvironment& env,
                                        char const* rtspUrl, int verbosityLevel = 0,
                                        char const* applicationName = nullptr,
                                        portNumBits tunnelOverHttpPortNum = 0)
    {
        return new RtspClient(filter, env, rtspUrl, verbosityLevel, applicationName, tunnelOverHttpPortNum);
    }

protected:
    RtspClient(CRtspSource* filter, UsageEnvironment& env, char const* rtspUrl,
               int verbosityLevel, char const* applicationName, portNumBits tunnelOverHttpPortNum)
        : ::RTSPClient(env, rtspUrl, verbosityLevel, applicationName, tunnelOverHttpPortNum, -1),
          filter(filter)
    {
    }

    virtual ~RtspClient()
    {
        // If true, we'd have a memleak
        _ASSERT(!mediaSession);
        _ASSERT(!subsession);
        _ASSERT(!iter);
    }

public:
    CRtspSource* filter = nullptr;
    MediaSession* mediaSession = nullptr;
    MediaSubsession* subsession = nullptr;
    MediaSubsessionIterator* iter = nullptr;
};

CUnknown* WINAPI CRtspSource::CreateInstance(LPUNKNOWN lpunk, HRESULT* phr)
{
    CRtspSource* filter = new CRtspSource(lpunk, phr);
    if (phr && SUCCEEDED(*phr))
    {
        if (!filter)
            *phr = E_OUTOFMEMORY;
        else
            *phr = S_OK;
    }
    else if (phr)
    {
        if (filter)
            delete filter;
        return NULL;
    }

    return filter;
}

CRtspSource::CRtspSource(IUnknown* pUnk, HRESULT* phr)
    : CSource(TEXT("RtspSourceFilter"), pUnk, CLSID_NULL)
    , _streamOverTcp(FALSE)
    , _tunnelOverHttpPort(0U)
    , _autoReconnectionMSecs(0)
    , _latencyMSecs(defaultLatency)
    , _sendLivenessCommand(false)
    , _state(State::Initial)
    , _scheduler(BasicTaskScheduler::createNew())
    , _env(BasicUsageEnvironment::createNew(*_scheduler))
    , _totNumPacketsReceived(0)
    , _interPacketGapCheckTimerTask(nullptr)
    , _reconnectionTimerTask(nullptr)
    , _firstCallTimeoutTask(nullptr)
    , _livenessCommandTask(nullptr)
    , _sessionTimerTask(nullptr)
    , _sessionTimeout(60)
    , _rtsp(nullptr)
    , _numSubsessions(0)
    , _sessionDuration(0)
    , _initialSeekTime(0)
    , _endTime(0)
    , _workerThread(&CRtspSource::WorkerThread, this)
{
    _h265Pin = new RtspH265SourcePin(phr, this, &_h265MediaPacketQueue);
}

CRtspSource::~CRtspSource()
{
    _requestQueue.push(RtspAsyncRequest(RtspSource::Done, ""));
    _workerThread.join();
    SAFE_DELETE(_h265Pin);
    SAFE_DELETE(_aacPin);
}

HRESULT CRtspSource::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    if (riid == IID_IFileSourceFilter)
        return GetInterface((IFileSourceFilter*)this, ppv);
    else if (riid == IID_IRtspSourceCommand)
        return GetInterface((RtspSource::ICommand*)this, ppv);
    return CSource::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CRtspSource::GetState(DWORD dwMSecs, __out FILTER_STATE* State)
{
    CheckPointer(State, E_POINTER);
    *State = m_State;
    if (m_State == State_Paused)
        return VFW_S_CANT_CUE; // We're live - dont buffer anything
    return S_OK;
}

namespace
{
    const char* GetFilterStateName(FILTER_STATE fs)
    {
        switch (fs)
        {
        case State_Stopped:
            return "Stopped";
        case State_Paused:
            return "Paused";
        case State_Running:
            return "Running";
        }
        return "";
    }
}

HRESULT CRtspSource::OpenURL(PCWSTR url, PCWSTR userName, PCWSTR password)
{
    _rtspUrlOrigin = url;
    ws2s(_rtspUrlOrigin, _rtspUrl);
    {
        std::string _userName;
        std::string _password;
        ws2s(userName, _userName);
        ws2s(password, _password);
        _authenticator.setUsernameAndPassword(_userName.c_str(), _password.c_str());
    }
    RtspFutureResult result = AsyncOpenUrl(_rtspUrl);
    RtspSource::ErrorCode ec = result.get();
    if (ec != RtspSource::Success) {
        // 重试的逻辑让用户自己做，再调用一次OpenURL即可。
        (*_env) << "Error: " << (int)ec << "\n";
        return E_FAIL;
    }

    return S_OK;
}

HRESULT CRtspSource::Stop()
{
    fprintf(stderr,"%s - state: %s\n", __FUNCTION__, GetFilterStateName(m_State));

    AsyncShutdown().get();
    return CSource::Stop();
}

HRESULT CRtspSource::Pause()
{
    fprintf(stderr,"%s - state: %s\n", __FUNCTION__, GetFilterStateName(m_State));
    if (m_State == State_Stopped) {
        // 清空历史画面，暂停后就会激活RtspSourcePin,会从live555收到新的帧。
        _h265MediaPacketQueue.clear();
        _aacMediaPacketQueue.clear();
    }

    return CSource::Pause(); // active all output pins
}

HRESULT CRtspSource::Run(REFERENCE_TIME tStart)
{
    fprintf(stderr,"%s\n", __FUNCTION__);

    // Need to reopen the session if we teardowned previous one
    if (_state == State::Initial) {
        // 只可能是Stop()后再Run()的情况，才会进入此分支。
        // 若是OpenURL() -> Pause() -> Run()，则此时状态应该是State::ReadyToPlay才对。
        RtspFutureResult result = AsyncOpenUrl(_rtspUrl);
        RtspSource::ErrorCode ec = result.get();
        if (ec) {
            (*_env) << "Error: " << (int)ec << "\n";
            return E_FAIL;
        }
    }

    HRESULT hr = CSource::Run(tStart);
    if (SUCCEEDED(hr)) {
        AsyncPlay().get();
    }

    return hr;
}

void CRtspSource::SetChannelId(int channel)
{
    _channelId = channel;
}

void CRtspSource::SetInitialSeekTime(DOUBLE secs)
{
    // Valid call only until first LoadFile call
    _initialSeekTime = secs;
}

void CRtspSource::SetStreamingOverTcp(BOOL streamOverTcp)
{
    // Valid call only until first LoadFile call
    _streamOverTcp = streamOverTcp ? true : false;
}

void CRtspSource::SetTunnelingOverHttpPort(WORD tunnelOverHttpPort)
{
    // Valid call only until first LoadFile call
    _tunnelOverHttpPort = tunnelOverHttpPort;
}

void CRtspSource::SetAutoReconnectionPeriod(DWORD dwMSecs)
{
    // Valid before reconnection is scheduled
    _autoReconnectionMSecs = dwMSecs;
}

void CRtspSource::SetLatency(DWORD dwMSecs)
{
    // Valid call only until first RTP packet arrival or after Stop/Run
    // This value is only used for first packet synchronization
    // - either it's first RTCP synced or just the very first packet
    _latencyMSecs = dwMSecs;
}

void CRtspSource::SetSendLivenessCommand(BOOL sendLiveness)
{
    _sendLivenessCommand = sendLiveness ? true : false;
}

void CRtspSource::SetNotifyReceiver(RtspSource::INotify* receiver)
{
    _notifyReceiver = receiver;
}

void CRtspSource::Fire_AvgFrameIntervalChanged(DWORD frameInterval)
{
    _notifyReceiver->OnFrameIntervalChanged(_channelId, frameInterval);
}

void CRtspSource::OpenUrl(const std::string& url)
{
    // Should never fail (only when out of memory)
    _rtsp = RtspClient::CreateRtspClient(this, *_env, url.c_str(),
        RtspClientVerbosityLevel, RtspClientAppName, _tunnelOverHttpPort);
    if (!_rtsp) {
        _currentRequest.SetValue(RtspSource::ClientCreateFailed);
        _state = State::Initial;
        return;
    }
    _firstCallTimeoutTask = _scheduler->scheduleDelayedTask(
        firstCallTimeoutTime * 1000, CRtspSource::DescribeRequestTimeout, this);
    // Returns only CSeq number
    _rtsp->sendDescribeCommand(HandleDescribeResponse, &_authenticator);
}

void CRtspSource::HandleDescribeResponse(RTSPClient* client, int resultCode, char* resultString)
{
    RtspClient* myClient = static_cast<RtspClient*>(client);
    myClient->filter->HandleDescribeResponse(resultCode, resultString);
}

void CRtspSource::HandleDescribeResponse(int resultCode, char* resultString)
{
    // Don't need this anymore - we got a response in time
    if (_firstCallTimeoutTask != nullptr)
        _scheduler->unscheduleDelayedTask(_firstCallTimeoutTask);

    if (resultCode != 0)
    {
        delete[] resultString;

        CloseClient(); // No session to close yet
        if (ScheduleNextReconnect())
            return;

        // Couldn't connect to the server
        if (resultCode == -WSAENOTCONN) {
            _state = State::Initial;
            _currentRequest.SetValue(RtspSource::ServerNotReachable);
        }
        else {
            _state = State::Initial;
            _currentRequest.SetValue(RtspSource::DescribeFailed);
        }

        return;
    }

    MediaSession* mediaSession = MediaSession::createNew(*_env, resultString);
    delete[] resultString;
    if (!mediaSession) // SDP is invalid or out of memory
    {
        CloseClient(); // No session to close to yet
        if (ScheduleNextReconnect())
            return;

        _currentRequest.SetValue(RtspSource::SdpInvalid);
        _state = State::Initial;

        return;
    }
    // Sane check
    else if (!mediaSession->hasSubsessions())
    {
        // Close media session (don't wait for a response)
        _rtsp->sendTeardownCommand(*mediaSession, nullptr, &_authenticator);
        Medium::close(mediaSession);

        // Close client
        CloseClient();
        if (ScheduleNextReconnect())
            return;

        _currentRequest.SetValue(RtspSource::NoSubsessions);
        _state = State::Initial;

        return;
    }

    // Start setuping media session
    MediaSubsessionIterator* iter = new MediaSubsessionIterator(*mediaSession);
    _rtsp->mediaSession = mediaSession;
    _rtsp->iter = iter;
    _numSubsessions = 0;

    SetupSubsession();
}

void CRtspSource::SetupSubsession()
{
    MediaSubsessionIterator* iter = _rtsp->iter;
    MediaSubsession* subsession = iter->next();
    _rtsp->subsession = subsession;
    // There's still some subsession to be setup
    if (subsession != nullptr)
    {
        if (!IsSubsessionSupported(*subsession))
        {
            // Ignore unsupported subsessions
            SetupSubsession();
            return;
        }
        if (!subsession->initiate())
        {
            /// TODO: Ignore or quit?
            SetupSubsession();
            return;
        }

        RTPSource* rtpSource = subsession->rtpSource();
        if (rtpSource)
        {
            rtpSource->setPacketReorderingThresholdTime(packetReorderingThresholdTime);
            int recvBuffer = 0;
            if (!strcmp(subsession->mediumName(), "video"))
                recvBuffer = recvBufferVideo;
            else if (!strcmp(subsession->mediumName(), "audio"))
                recvBuffer = recvBufferAudio;

            // Increase receive buffer for rather big packets (like H.265 IDR)
            if (recvBuffer > 0 && rtpSource->RTPgs())
                ::increaseReceiveBufferTo(*_env, rtpSource->RTPgs()->socketNum(), recvBuffer);
        }

        _rtsp->sendSetupCommand(*subsession, HandleSetupResponse, False, _streamOverTcp,
                                forceMulticastOnUnspecified && !_streamOverTcp, &_authenticator);
        return;
    }

    // We iterated over all available subsessions
    delete _rtsp->iter;
    _rtsp->iter = nullptr;

    // How many subsession we set up? If none then something is wrong and we shouldn't proceed
    // further
    if (_numSubsessions == 0)
    {
        CloseSession();
        CloseClient();

        if (ScheduleNextReconnect())
            return;

        _state = State::Initial;
        _currentRequest.SetValue(RtspSource::NoSubsessionsSetup);

        return;
    }

    if (_state != State::Reconnecting)
    {
        _state = State::ReadyToPlay;
        _currentRequest.SetValue(RtspSource::Success);
    }
    else
    {
        // Autostart playing if we're reconnecting
        _state = State::Playing;
        Play();
    }
}

void CRtspSource::HandleSetupResponse(RTSPClient* client, int resultCode, char* resultString)
{
    RtspClient* myClient = static_cast<RtspClient*>(client);
    myClient->filter->HandleSetupResponse(resultCode, resultString);
}

void CRtspSource::HandleSetupResponse(int resultCode, char* resultString)
{
    if (resultCode == 0)
    {
        delete[] resultString;
        MediaSubsession* subsession = _rtsp->subsession;

        if (0 == strcmp(subsession->mediumName(), "video"))
        {
            assert(0 == strcmp(subsession->codecName(), "H265"));
            subsession->sink = new ProxyMediaSink(*_env, *subsession, _h265MediaPacketQueue, recvBufferVideo, false);
            _h265Pin->ResetMediaSubsession(subsession);
        }
        else if (0 == strcmp(subsession->mediumName(), "audio"))
        {
            assert(0 == strcmp(subsession->codecName(), "MPEG4-GENERIC"));
            HRESULT hr;
            subsession->sink = new ProxyMediaSink(*_env, *subsession, _aacMediaPacketQueue, recvBufferAudio, true);
            if (_aacPin == nullptr)
                _aacPin = new RtspAACSourcePin(&hr, this, subsession, &_aacMediaPacketQueue);
            else
                _aacPin->ResetMediaSubsession(subsession);
        }

        // What about text medium ?
        if (subsession->sink == nullptr)
        {
            // unsupported medium or out of memory
            SetupSubsession();
            return;
        }

        subsession->miscPtr = _rtsp;
        subsession->sink->startPlaying(*(subsession->readSource()), HandleSubsessionFinished, subsession);

        // Set a handler to be called if a RTCP "BYE" arrives for this subsession
        if (subsession->rtcpInstance() != nullptr)
            subsession->rtcpInstance()->setByeHandler(HandleSubsessionByeHandler, subsession);

        ++_numSubsessions;
    }
    else
    {
        (*_env) << "SETUP failed, server response: " << resultString;
        delete[] resultString;
    }

    SetupSubsession();
}

void CRtspSource::Play()
{
    MediaSession& mediaSession = *_rtsp->mediaSession;

    const float scale = 1.0f; // No trick play

    _sessionDuration = mediaSession.playEndTime() - _initialSeekTime;
    _sessionDuration = std::max<double>(0.0, _sessionDuration);

    // For duration equal to 0 we got live stream with no end time (-1)
    _endTime = _sessionDuration > 0.0 ? _initialSeekTime + _sessionDuration : -1.0;

    const char* absStartTime = mediaSession.absStartTime();
    if (absStartTime != nullptr)
    {
        // Either we or the server have specified that seeking should be done by 'absolute' time:
        _rtsp->sendPlayCommand(mediaSession, HandlePlayResponse, absStartTime,
                               mediaSession.absEndTime(), scale, &_authenticator);
    }
    else
    {
        // Normal case: Seek by relative time (NPT):
        _rtsp->sendPlayCommand(mediaSession, HandlePlayResponse, _initialSeekTime, _endTime, scale,
                               &_authenticator);
    }
}

void CRtspSource::HandlePlayResponse(RTSPClient* client, int resultCode, char* resultString)
{
    RtspClient* myClient = static_cast<RtspClient*>(client);
    myClient->filter->HandlePlayResponse(resultCode, resultString);
}

void CRtspSource::HandlePlayResponse(int resultCode, char* resultString)
{
    if (resultCode == 0)
    {
        _currentRequest.SetValue(RtspSource::Success);
        // State is already Playing
        _totNumPacketsReceived = 0;
        _sessionTimeout =
            _rtsp->sessionTimeoutParameter() != 0 ? _rtsp->sessionTimeoutParameter() : 60;

        // Create timerTask for disconnection recognition
        _interPacketGapCheckTimerTask = _scheduler->scheduleDelayedTask(
            interPacketGapMaxTime * 1000, &CRtspSource::CheckInterPacketGaps, this);
        // Create timerTask for session keep-alive (use OPTIONS request to sustain session)
        if (_sendLivenessCommand)
        {
            _livenessCommandTask = _scheduler->scheduleDelayedTask(
                _sessionTimeout / 3 * 1000000, &CRtspSource::SendLivenessCommand, this);
        }

        if (_sessionDuration > 0)
        {
            double rangeAdjustment =
                (_rtsp->mediaSession->playEndTime() - _rtsp->mediaSession->playStartTime()) -
                (_endTime - _initialSeekTime);
            if (_sessionDuration + rangeAdjustment > 0.0)
                _sessionDuration += rangeAdjustment;
            int64_t uSecsToDelay = (int64_t)(_sessionDuration * 1000000.0);
            _sessionTimerTask = _scheduler->scheduleDelayedTask(
                uSecsToDelay, &CRtspSource::HandleMediaEnded, this);
        }
    }
    else
    {
        UnscheduleAllDelayedTasks();
        CloseSession();
        CloseClient();

        if (_autoReconnectionMSecs > 0)
        {
            _reconnectionTimerTask = _scheduler->scheduleDelayedTask(
                _autoReconnectionMSecs * 1000, &CRtspSource::Reconnect, this);
            _state = State::Reconnecting;
            _currentRequest.SetValue(RtspSource::PlayFailed);
        }
        else
        {

            _state = State::Initial;
            _currentRequest.SetValue(RtspSource::PlayFailed);

            // Notify output pins PLAY command failed
            _h265MediaPacketQueue.push(MediaPacketSample());
            _aacMediaPacketQueue.push(MediaPacketSample());
        }
    }

    delete[] resultString;
}

void CRtspSource::CloseSession()
{
    if (!_rtsp)
        return; // sane check
    MediaSession* mediaSession = _rtsp->mediaSession;
    if (mediaSession != nullptr)
    {
        // Don't bother waiting for response
        _rtsp->sendTeardownCommand(*mediaSession, nullptr, &_authenticator);
        // Close media sinks
        MediaSubsessionIterator iter(*mediaSession);
        MediaSubsession* subsession;
        while ((subsession = iter.next()) != nullptr)
        {
            Medium::close(subsession->sink);
            subsession->sink = nullptr;
        }
        // Close media session itself
        Medium::close(mediaSession);
        _rtsp->mediaSession = nullptr;
    }
}

void CRtspSource::CloseClient()
{
    // Shutdown RTSP client
    Medium::close(_rtsp);
    _rtsp = nullptr;
}

void CRtspSource::HandleSubsessionFinished(void* clientData)
{
    MediaSubsession* subsession = static_cast<MediaSubsession*>(clientData);
    RtspClient* rtsp = static_cast<RtspClient*>(subsession->miscPtr);
    // Close finished media subsession
    Medium::close(subsession->sink);
    subsession->sink = nullptr;
    // Check if there's at least one active subsession
    MediaSession& media_session = subsession->parentSession();
    MediaSubsessionIterator iter(media_session);
    while ((subsession = iter.next()) != nullptr)
    {
        if (subsession->sink != nullptr)
            return;
    }
    // No more subsessions active - close the session
    CRtspSource* self = rtsp->filter;
    self->UnscheduleAllDelayedTasks();
    self->CloseSession();
    self->CloseClient();
    self->_state = State::Initial;
    self->_h265MediaPacketQueue.push(MediaPacketSample());
    self->_aacMediaPacketQueue.push(MediaPacketSample());
    // No request to reply to
}

void CRtspSource::HandleSubsessionByeHandler(void* clientData)
{
    fprintf(stderr,"BYE!\n");
    // We were given a RTCP BYE packet - server close the connection (for example: session timeout)
    HandleSubsessionFinished(clientData);
}

void CRtspSource::UnscheduleAllDelayedTasks()
{
    if (_firstCallTimeoutTask != nullptr)
        _scheduler->unscheduleDelayedTask(_firstCallTimeoutTask);
    if (_interPacketGapCheckTimerTask != nullptr)
        _scheduler->unscheduleDelayedTask(_interPacketGapCheckTimerTask);
    if (_reconnectionTimerTask != nullptr)
        _scheduler->unscheduleDelayedTask(_reconnectionTimerTask);
    if (_livenessCommandTask != nullptr)
        _scheduler->unscheduleDelayedTask(_livenessCommandTask);
    if (_sessionTimerTask != nullptr)
        _scheduler->unscheduleDelayedTask(_sessionTimerTask);
}

void CRtspSource::Shutdown()
{
    UnscheduleAllDelayedTasks();
    CloseSession();
    CloseClient();

    _state = State::Initial;
    _currentRequest.SetValue(RtspSource::Success);

    // Notify pins we are tearing down
    _h265MediaPacketQueue.push(MediaPacketSample());
    _aacMediaPacketQueue.push(MediaPacketSample());
}

bool CRtspSource::ScheduleNextReconnect()
{
    if (_state == State::Reconnecting)
    {
        _reconnectionTimerTask = _scheduler->scheduleDelayedTask(
            _autoReconnectionMSecs * 1000, &CRtspSource::Reconnect, this);
        // state is still Reconnecting
        _currentRequest.SetValue(RtspSource::ReconnectFailed);
        return true;
    }
    return false;
}

/*
 * Task:_firstCallTimeoutTask
 * Allows for customized timeout on first call to the target RTSP server
 * Viable only in SettingUp an Reconnecing state.
 */
void CRtspSource::DescribeRequestTimeout(void* clientData)
{
    CRtspSource* self = static_cast<CRtspSource*>(clientData);
    self->DescribeRequestTimeout();
}

void CRtspSource::DescribeRequestTimeout()
{
    _ASSERT(_state == State::SettingUp || _state == State::Reconnecting);
    _firstCallTimeoutTask = nullptr;

    if (_reconnectionTimerTask != nullptr)
        _scheduler->unscheduleDelayedTask(_reconnectionTimerTask);

    CloseClient(); // No session to close yet
    if (ScheduleNextReconnect())
        return;

    _state = State::Initial;
    _currentRequest.SetValue(RtspSource::ServerNotReachable);
}

/*
 * Task:_interPacketGapCheckTimerTask:
 * Periodically calculates how many packets arrived allowing to detect connection lost.
 * Viable only in Playing state.
 */
void CRtspSource::CheckInterPacketGaps(void* clientData)
{
    CRtspSource* self = static_cast<CRtspSource*>(clientData);
    self->CheckInterPacketGaps();
}

void CRtspSource::CheckInterPacketGaps()
{
    _ASSERT(_state == State::Playing);
    _interPacketGapCheckTimerTask = nullptr;

    // Aliases
    UsageEnvironment& env = *_env;
    MediaSession& mediaSession = *_rtsp->mediaSession;

    // Check each subsession, counting up how many packets have been received
    MediaSubsessionIterator iter(mediaSession);
    MediaSubsession* subsession;
    uint32_t newTotNumPacketsReceived = 0;
    while ((subsession = iter.next()) != nullptr)
    {
        RTPSource* src = subsession->rtpSource();
        if (src == nullptr)
            continue;
        newTotNumPacketsReceived += src->receptionStatsDB().totNumPacketsReceived();
    }
    fprintf(stderr,"Channel(%d) total number of packets received: %u, queued packets: %u|%u\n", _channelId,
             newTotNumPacketsReceived, (DWORD)_h265MediaPacketQueue.size(), (DWORD)_aacMediaPacketQueue.size());
    // 自上次检查以来，一直到现在都没有收到新的媒体包了？可能媒体源枯竭(EndOfStream)了？或者断网了？
    if (newTotNumPacketsReceived == _totNumPacketsReceived)
    {
        fprintf(stderr,"No packets has been received since last time!\n");

        if (_livenessCommandTask != nullptr)
            _scheduler->unscheduleDelayedTask(_livenessCommandTask);
        if (_sessionTimerTask != nullptr)
            _scheduler->unscheduleDelayedTask(_sessionTimerTask);

        // 不自动重连，直接结束本次推流。
        if (_autoReconnectionMSecs == 0)
        {
            _h265MediaPacketQueue.push(MediaPacketSample());
            _aacMediaPacketQueue.push(MediaPacketSample());
        }
        // 调度重连任务
        else
        {
            // It's VoD - need to recalculate initial time seek for reconnect PLAY command
            if (_sessionDuration > 0)
            {
                // Retrieve current play time from output pins
                REFERENCE_TIME currentPlayTime = 0;
                if (_h265Pin)
                {
                    currentPlayTime = _h265Pin->CurrentPlayTime();
                    // Get minimum of two NPT
                    if (_aacPin)
                        currentPlayTime = std::min<REFERENCE_TIME>(currentPlayTime, _aacPin->CurrentPlayTime());
                }
                else if (_aacPin)
                {
                    currentPlayTime = _aacPin->CurrentPlayTime();
                }

                _initialSeekTime += static_cast<double>(currentPlayTime) / UNITS_IN_100NS;
            }

            // Notify pin to desynchronize
            if (_h265Pin)
                _h265Pin->ResetTimeBaselines();
            if (_aacPin)
                _aacPin->ResetTimeBaselines();

            // Finally schedule reconnect task
            _reconnectionTimerTask = _scheduler->scheduleDelayedTask(
                _autoReconnectionMSecs * 1000, &CRtspSource::Reconnect, this);
        }
    }
    else
    {
        _totNumPacketsReceived = newTotNumPacketsReceived;
        // Schedule next inspection
        _interPacketGapCheckTimerTask = _scheduler->scheduleDelayedTask(
            interPacketGapMaxTime * 1000, &CRtspSource::CheckInterPacketGaps, this);
    }
}

/*
 * Task:_livenessCommandTask:
 * Periodically requests OPTION command to the server to keep alive the session
 * Viable only in Playing state.
 */
void CRtspSource::SendLivenessCommand(void* clientData)
{
    CRtspSource* self = static_cast<CRtspSource*>(clientData);
    _ASSERT(self);
    _ASSERT(self->_state == State::Playing);

    self->_livenessCommandTask = nullptr;
    self->_rtsp->sendOptionsCommand(HandleOptionsResponse_Liveness, &self->_authenticator);
}

void CRtspSource::HandleOptionsResponse_Liveness(RTSPClient* client, int resultCode, char* resultString)
{
    // If something bad happens between OPTIONS request and response, response handler shouldn't be call
    RtspClient* myClient = static_cast<RtspClient*>(client);
    myClient->filter->HandleOptionsResponse_Liveness(resultCode, resultString);
}

void CRtspSource::HandleOptionsResponse_Liveness(int resultCode, char* resultString)
{
    _ASSERT(_state == State::Playing);

    // Used as a liveness command
    delete[] resultString;

    if (resultCode == 0)
    {
        // Schedule next keep-alive request if there wasn't any error along the way
        _livenessCommandTask = _scheduler->scheduleDelayedTask(
            _sessionTimeout / 3 * 1000000, &CRtspSource::SendLivenessCommand, this);
    }
}

/*
 * Task:_reconnectionTimerTask
 * Tries to reopen the connection and start to play from the moment connection was lost
 * Viable in Playing and Reconnecting (reattempt) state
 */
void CRtspSource::Reconnect(void* clientData)
{
    CRtspSource* self = static_cast<CRtspSource*>(clientData);
    self->Reconnect();
}

void CRtspSource::Reconnect()
{
    _ASSERT(_state == State::Playing || _state == State::Reconnecting);
    _reconnectionTimerTask = nullptr;

    // Called from worker thread as a delayed task
    fprintf(stderr,"Reconnect now!\n");
    AsyncReconnect();
}

/*
 * Task:_sessionTimerTask
 * Perform shutdown when media come to end (for VOD)
 * Viable only in Playing state.
 */
void CRtspSource::HandleMediaEnded(void* clientData)
{
    fprintf(stderr,"Media ended!\n");
    CRtspSource* self = static_cast<CRtspSource*>(clientData);
    _ASSERT(self->_state == State::Playing);
    self->_sessionTimerTask = nullptr;
    self->AsyncShutdown();
}

void CRtspSource::WorkerThread()
{
    SetThreadName(-1, "RTSP source thread");
    bool done = false;

    // Uses internals of RtspSourceFilter
    auto GetRtspSourceStateString = [](State state) {
        switch (state) {
        case State::Initial: return "Initial";
        case State::SettingUp: return "SettingUp";
        case State::ReadyToPlay: return "ReadyToPlay";
        case State::Playing: return "Playing";
        case State::Reconnecting: return "Reconnecting";
        default: return "Unknown";
        }
    };

    RtspAsyncRequest req;

    while (!done) {
        // In the middle of request - ignore any incoming requests untill done
        if (_state == State::SettingUp) {
            _scheduler->SingleStep();
            continue;
        }

        if (!_requestQueue.try_pop(req)) {
            // No requests to process to - make a single step
            _scheduler->SingleStep();
            continue;
        }

        fprintf(stderr,"[WorkerThread] -  State: %s, Request: %s]\n",
            GetRtspSourceStateString(_state),
            GetRtspAsyncRequestTypeString(req.GetOpCode()));

        // Process requests
        switch (_state) {
        case State::Initial:
            switch (req.GetOpCode())
            {
            // Start opening url
            case RtspSource::Open:
                _currentRequest = std::move(req);
                _state = State::SettingUp;
                OpenUrl(_currentRequest.GetArg());
                break;

            // Wrong transitions
            case RtspSource::Play:
            case RtspSource::Reconnect:
                req.SetValue(RtspSource::WrongState);
                break;

            case RtspSource::Stop:
                // Needed if filter is re-started and fails to start running for some reason
                // and also output pins threads are already started and waiting for packets.
                // This is because Pause() is called before Run() which can fail if filter is
                // restarted
                _h265MediaPacketQueue.push(MediaPacketSample());
                _aacMediaPacketQueue.push(MediaPacketSample());
                req.SetValue(RtspSource::Success);
                break;

            // Finish this thread
            case RtspSource::Done:
                done = true;
                req.SetValue(RtspSource::Success);
                break;
            }
            break;

        case State::ReadyToPlay:
            switch (req.GetOpCode()) {
            // Wrong transition
            case RtspSource::Open:
            case RtspSource::Reconnect:
                req.SetValue(RtspSource::WrongState);
                break;

            // Start media streaming
            case RtspSource::Play:
                _currentRequest = std::move(req);
                _state = State::Playing;
                Play();
                break;

            // Back down from streaming - close media session and its sink(s)
            case RtspSource::Stop:
                _currentRequest = std::move(req);
                Shutdown();
                break;

            // Order from the dtor - finish this thread
            case RtspSource::Done:
                _currentRequest = std::move(req);
                Shutdown();
                done = true;
                break;
            }
            break;

        case State::Playing:
            switch (req.GetOpCode()) {
            // Wrong transition
            case RtspSource::Open:
            case RtspSource::Play:
                req.SetValue(RtspSource::WrongState);
                break;

            // Try to reconnect
            case RtspSource::Reconnect:
                _currentRequest = std::move(req);
                _state = State::Reconnecting;
                UnscheduleAllDelayedTasks();
                CloseSession();
                CloseClient();
                OpenUrl(_rtspUrl);
                break;

            // Back down from streaming - close media session and its sink(s)
            case RtspSource::Stop:
                _currentRequest = std::move(req);
                Shutdown();
                break;

            // Order from the dtor - finish this thread
            case RtspSource::Done:
                _currentRequest = std::move(req);
                Shutdown();
                done = true;
                break;
            }
            break;

        case State::Reconnecting:
            switch (req.GetOpCode()) {
            // Wrong transition
            case RtspSource::Open:
            case RtspSource::Play:
                req.SetValue(RtspSource::WrongState);
                break;

            // Try another round
            case RtspSource::Reconnect:
                _currentRequest = std::move(req); // 不能立即给出结果，会有RTSP异步响应，决定下一步，所以要保持当前请求对象。
                // Session and client should be null here
                _ASSERT(!_rtsp);
                OpenUrl(_rtspUrl);
                break;

            // Giveup trying to reconnect
            case RtspSource::Stop:
                _currentRequest = std::move(req);
                Shutdown();
                break;

            case RtspSource::Done:
                _currentRequest = std::move(req);
                Shutdown();
                done = true;
                break;
            }
            break;

        default:
            // should never come here
            _ASSERT(false);
            break;
        }
    }
}

namespace
{
    bool IsSubsessionSupported(MediaSubsession& mediaSubsession)
    {
        if (0 == strcmp(mediaSubsession.mediumName(), "video"))
        {
            if (0 == strcmp(mediaSubsession.codecName(), "H265"))
                return true;
        }
        else if (0 == strcmp(mediaSubsession.mediumName(), "audio"))
        {
            if (0 == _stricmp(mediaSubsession.codecName(), "MPEG4-GENERIC"))
                return true;
        }
        return false;
    }

    const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push, 8)
    typedef struct tagTHREADNAME_INFO
    {
        DWORD dwType;     // Must be 0x1000.
        LPCSTR szName;    // Pointer to name (in user addr space).
        DWORD dwThreadID; // Thread ID (-1=caller thread).
        DWORD dwFlags;    // Reserved for future use, must be zero.
    } THREADNAME_INFO;
#pragma pack(pop)

    void SetThreadName(DWORD dwThreadID, char* threadName)
    {
        THREADNAME_INFO info = {0x1000, threadName, dwThreadID, 0};

        __try
        {
            RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}


//-------------------------------------------------------------------------------------------------
// RtspSourceFilter module exported API
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
// 血的教训：
//     不要设计大而全的万能模块，会把问题搞得极为复杂。
// 从底层开始构造一些不可再分的原子类型的基础模块，然后通过一些函数它们组合起来使用。
//-------------------------------------------------------------------------------------------------
HRESULT WINAPI RtspSource_CreateInstance(IBaseFilter** ppObj)
{
    HRESULT hr = S_OK;

    CRtspSource* o = new CRtspSource(nullptr, &hr);
    ULONG ul = o->AddRef();
    *ppObj = static_cast<IBaseFilter*>(o);

    return hr;
}