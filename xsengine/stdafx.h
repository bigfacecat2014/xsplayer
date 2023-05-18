#pragma once

#include <functional>

#include "DSUtil/DSUtil.h"
#include "DSUtil/BaseWindow.h"
#include "DSUtil/RegObjSafe.h"
#include "DSUtil/TraceMsg.h"
#include "DSUtil/ConcurrentQueue.h"
#include <DSUtil/SyncClock.h>

#include <atlctl.h>
#include <windowsx.h>

#include "mfcommon/ClassFactory.h"
#include "mfcommon/common.h"
#include "mfcommon/registry.h"
using namespace MediaFoundationSamples;

#include "RtspSource/IRtspSource.h"
#include "ADMVideoDecoder/ILAVVideo.h"
#include "ADMVideoRenderer/IVideoRenderer.h"

#include "xsengine/xsengine.h"

