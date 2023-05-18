#pragma once

#include "DSUtil/SharedInclude.h"

#include <dxva2api.h>
#include <emmintrin.h>
#include <smmintrin.h>
#include <MMReg.h>
#include <Mfidl.h>
#include <dvdmedia.h>
#include <Shlwapi.h>
#include <Mfidl.h>
#include <evr.h>
#include <d3d9.h>

#pragma warning(push)
#pragma warning(disable:4244)
extern "C" {
#define __STDC_CONSTANT_MACROS
#include "libavcodec/avcodec.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
}
#pragma warning(pop)

#include "streams.h"

#include <initguid.h>
#include "moreuuids.h"

#include "DSUtil/DSUtil.h"
#include "DSUtil/growarray.h"

#define REF_SECOND_MULT 10000000LL
