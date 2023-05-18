#pragma once

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#define VC_EXTRALEAN        // Exclude rarely-used stuff from Windows headers
#define _WIN32_WINNT 0x0601 // Windows 7 or later
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS  // some CString constructors will be explicit

#ifndef STRICT_TYPED_ITEMIDS
#define STRICT_TYPED_ITEMIDS
#endif

// C runtime
#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC   // include Microsoft memory leak detection procedures
#include <crtdbg.h>
#endif
#include <assert.h>
#include <corecrt_math_defines.h>

// C++ runtime
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <list>
#include <map>
#include <queue>
#include <algorithm>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <chrono>

// Win32 runtime
#include <Windows.h>
#include <VersionHelpers.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <Shlobj.h>
#include <Commctrl.h>
#include <comutil.h>  
#include <strmif.h>
#include <Vfw.h>
#include <mmeapi.h>
#include <MMReg.h>
#include <dvdmedia.h>
#include <emmintrin.h>
#include <mfapi.h>
#include <comcat.h>
#include <strsafe.h>
#include <objsafe.h>
#include <Mferror.h>
#include <gdiplus.h>

// D3D runtime
#include <dxgi.h>
#include <d3d9.h>
#include <d3d10_1.h>
#include <d3d10.h>
#include <evr.h>
#include <evr9.h>

// ATL runtime
#include <atlbase.h>
#include <atlutil.h>
#include <atlcoll.h>
#include <atltypes.h>
#include <atltime.h>
#include <atlsimpstr.h>
#include <atlstr.h>
#include <atlimage.h>
#include <atlpath.h>
#include <cstringt.h>



