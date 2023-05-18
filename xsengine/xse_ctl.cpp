#include "stdafx.h"

#ifdef _ADM_ACTIVEX

#include <initguid.h>
#include "xsengine_guid.h"
#include "xsengine_ctl.h"
#include "xse_ctl.h"

CXSEngine::TimerIdMap CXSEngine::m_timer_id_map;
volatile long CXSEngine::_instanceCount = 0;

#endif // end #ifdef _ADM_ACTIVEX
