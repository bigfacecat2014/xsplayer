//////////////////////////////////////////////////////////////////////////
//
// trace.h : Functions to return the names of constants.
// 
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//////////////////////////////////////////////////////////////////////////

#pragma once

#include "logging.h"
#include <mfidl.h>

#define MF_TRACE_H_CASE(x) case x: return L#x

namespace MediaFoundationSamples
{

    // IMPORTANT: No function here can return a NULL pointer - caller assumes
    // the return value is a valid null-terminated string. You should only
    // use these functions for debugging purposes.

    // Media Foundation event names (subset)
    inline const WCHAR* EventName(MediaEventType met)
    {
        switch (met)
        {
            MF_TRACE_H_CASE(MEError);
            MF_TRACE_H_CASE(MEExtendedType);
            MF_TRACE_H_CASE(MESessionTopologySet);
            MF_TRACE_H_CASE(MESessionTopologiesCleared);
            MF_TRACE_H_CASE(MESessionStarted);
            MF_TRACE_H_CASE(MESessionPaused);
            MF_TRACE_H_CASE(MESessionStopped);
            MF_TRACE_H_CASE(MESessionClosed);
            MF_TRACE_H_CASE(MESessionEnded);
            MF_TRACE_H_CASE(MESessionRateChanged);
            MF_TRACE_H_CASE(MESessionScrubSampleComplete);
            MF_TRACE_H_CASE(MESessionCapabilitiesChanged);
            MF_TRACE_H_CASE(MESessionTopologyStatus);
            MF_TRACE_H_CASE(MESessionNotifyPresentationTime);
            MF_TRACE_H_CASE(MENewPresentation);
            MF_TRACE_H_CASE(MELicenseAcquisitionStart);
            MF_TRACE_H_CASE(MELicenseAcquisitionCompleted);
            MF_TRACE_H_CASE(MEIndividualizationStart);
            MF_TRACE_H_CASE(MEIndividualizationCompleted);
            MF_TRACE_H_CASE(MEEnablerProgress);
            MF_TRACE_H_CASE(MEEnablerCompleted);
            MF_TRACE_H_CASE(MEPolicyError);
            MF_TRACE_H_CASE(MEPolicyReport);
            MF_TRACE_H_CASE(MEBufferingStarted);
            MF_TRACE_H_CASE(MEBufferingStopped);
            MF_TRACE_H_CASE(MEConnectStart);
            MF_TRACE_H_CASE(MEConnectEnd);
            MF_TRACE_H_CASE(MEReconnectStart);
            MF_TRACE_H_CASE(MEReconnectEnd);
            MF_TRACE_H_CASE(MERendererEvent);
            MF_TRACE_H_CASE(MESessionStreamSinkFormatChanged);
            MF_TRACE_H_CASE(MESourceStarted);
            MF_TRACE_H_CASE(MEStreamStarted);
            MF_TRACE_H_CASE(MESourceSeeked);
            MF_TRACE_H_CASE(MEStreamSeeked);
            MF_TRACE_H_CASE(MENewStream);
            MF_TRACE_H_CASE(MEUpdatedStream);
            MF_TRACE_H_CASE(MESourceStopped);
            MF_TRACE_H_CASE(MEStreamStopped);
            MF_TRACE_H_CASE(MESourcePaused);
            MF_TRACE_H_CASE(MEStreamPaused);
            MF_TRACE_H_CASE(MEEndOfPresentation);
            MF_TRACE_H_CASE(MEEndOfStream);
            MF_TRACE_H_CASE(MEMediaSample);
            MF_TRACE_H_CASE(MEStreamTick);
            MF_TRACE_H_CASE(MEStreamThinMode);
            MF_TRACE_H_CASE(MEStreamFormatChanged);
            MF_TRACE_H_CASE(MESourceRateChanged);
            MF_TRACE_H_CASE(MEEndOfPresentationSegment);
            MF_TRACE_H_CASE(MESourceCharacteristicsChanged);
            MF_TRACE_H_CASE(MESourceRateChangeRequested);
            MF_TRACE_H_CASE(MESourceMetadataChanged);
            MF_TRACE_H_CASE(MESequencerSourceTopologyUpdated);
            MF_TRACE_H_CASE(MEStreamSinkStarted);
            MF_TRACE_H_CASE(MEStreamSinkStopped);
            MF_TRACE_H_CASE(MEStreamSinkPaused);
            MF_TRACE_H_CASE(MEStreamSinkRateChanged);
            MF_TRACE_H_CASE(MEStreamSinkRequestSample);
            MF_TRACE_H_CASE(MEStreamSinkMarker);
            MF_TRACE_H_CASE(MEStreamSinkPrerolled);
            MF_TRACE_H_CASE(MEStreamSinkScrubSampleComplete);
            MF_TRACE_H_CASE(MEStreamSinkFormatChanged);
            MF_TRACE_H_CASE(MEStreamSinkDeviceChanged);
            MF_TRACE_H_CASE(MEQualityNotify);
            MF_TRACE_H_CASE(MESinkInvalidated);
            MF_TRACE_H_CASE(MEAudioSessionNameChanged);
            MF_TRACE_H_CASE(MEAudioSessionVolumeChanged);
            MF_TRACE_H_CASE(MEAudioSessionDeviceRemoved);
            MF_TRACE_H_CASE(MEAudioSessionServerShutdown);
            MF_TRACE_H_CASE(MEAudioSessionGroupingParamChanged);
            MF_TRACE_H_CASE(MEAudioSessionIconChanged);
            MF_TRACE_H_CASE(MEAudioSessionFormatChanged);
            MF_TRACE_H_CASE(MEAudioSessionDisconnected);
            MF_TRACE_H_CASE(MEAudioSessionExclusiveModeOverride);
            MF_TRACE_H_CASE(MEPolicyChanged);
            MF_TRACE_H_CASE(MEContentProtectionMessage);
            MF_TRACE_H_CASE(MEPolicySet);

        default:
            return L"Unknown event";
        }
    }

    // Names of VARIANT data types. 
    inline const WCHAR* VariantTypeName(const PROPVARIANT& prop)
    {
        switch (prop.vt & VT_TYPEMASK)
        {
            MF_TRACE_H_CASE(VT_EMPTY);
            MF_TRACE_H_CASE(VT_NULL);
            MF_TRACE_H_CASE(VT_I2);
            MF_TRACE_H_CASE(VT_I4);
            MF_TRACE_H_CASE(VT_R4);
            MF_TRACE_H_CASE(VT_R8);
            MF_TRACE_H_CASE(VT_CY);
            MF_TRACE_H_CASE(VT_DATE);
            MF_TRACE_H_CASE(VT_BSTR);
            MF_TRACE_H_CASE(VT_DISPATCH);
            MF_TRACE_H_CASE(VT_ERROR);
            MF_TRACE_H_CASE(VT_BOOL);
            MF_TRACE_H_CASE(VT_VARIANT);
            MF_TRACE_H_CASE(VT_UNKNOWN);
            MF_TRACE_H_CASE(VT_DECIMAL);
            MF_TRACE_H_CASE(VT_I1);
            MF_TRACE_H_CASE(VT_UI1);
            MF_TRACE_H_CASE(VT_UI2);
            MF_TRACE_H_CASE(VT_UI4);
            MF_TRACE_H_CASE(VT_I8);
            MF_TRACE_H_CASE(VT_UI8);
            MF_TRACE_H_CASE(VT_INT);
            MF_TRACE_H_CASE(VT_UINT);
            MF_TRACE_H_CASE(VT_VOID);
            MF_TRACE_H_CASE(VT_HRESULT);
            MF_TRACE_H_CASE(VT_PTR);
            MF_TRACE_H_CASE(VT_SAFEARRAY);
            MF_TRACE_H_CASE(VT_CARRAY);
            MF_TRACE_H_CASE(VT_USERDEFINED);
            MF_TRACE_H_CASE(VT_LPSTR);
            MF_TRACE_H_CASE(VT_LPWSTR);
            MF_TRACE_H_CASE(VT_RECORD);
            MF_TRACE_H_CASE(VT_INT_PTR);
            MF_TRACE_H_CASE(VT_UINT_PTR);
            MF_TRACE_H_CASE(VT_FILETIME);
            MF_TRACE_H_CASE(VT_BLOB);
            MF_TRACE_H_CASE(VT_STREAM);
            MF_TRACE_H_CASE(VT_STORAGE);
            MF_TRACE_H_CASE(VT_STREAMED_OBJECT);
            MF_TRACE_H_CASE(VT_STORED_OBJECT);
            MF_TRACE_H_CASE(VT_BLOB_OBJECT);
            MF_TRACE_H_CASE(VT_CF);
            MF_TRACE_H_CASE(VT_CLSID);
            MF_TRACE_H_CASE(VT_VERSIONED_STREAM);
        default:
            return L"Unknown VARIANT type";
        }
    }

    // Names of topology node types.
    inline const WCHAR* TopologyNodeTypeName(MF_TOPOLOGY_TYPE nodeType)
    {
        switch (nodeType)
        {
            MF_TRACE_H_CASE(MF_TOPOLOGY_OUTPUT_NODE);
            MF_TRACE_H_CASE(MF_TOPOLOGY_SOURCESTREAM_NODE);
            MF_TRACE_H_CASE(MF_TOPOLOGY_TRANSFORM_NODE);
            MF_TRACE_H_CASE(MF_TOPOLOGY_TEE_NODE);
        default:
            return L"Unknown node type";
        }
    }

    inline const WCHAR* MFTMessageName(MFT_MESSAGE_TYPE msg)
    {
        switch (msg)
        {
            MF_TRACE_H_CASE(MFT_MESSAGE_COMMAND_FLUSH);
            MF_TRACE_H_CASE(MFT_MESSAGE_COMMAND_DRAIN);
            MF_TRACE_H_CASE(MFT_MESSAGE_SET_D3D_MANAGER);
            MF_TRACE_H_CASE(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING);
            MF_TRACE_H_CASE(MFT_MESSAGE_NOTIFY_END_STREAMING);
            MF_TRACE_H_CASE(MFT_MESSAGE_NOTIFY_END_OF_STREAM);
            MF_TRACE_H_CASE(MFT_MESSAGE_NOTIFY_START_OF_STREAM);
        default:
            return L"Unknown message";
        }
    }

}; // namespace MediaFoundationSamples
