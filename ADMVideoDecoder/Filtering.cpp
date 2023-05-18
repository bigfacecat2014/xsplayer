/*
 *      Copyright (C) 2010-2019 Hendrik Leppkes
 *      http://www.1f0.de
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "stdafx.h"
#include "LAVVideo.h"

static void lav_free_lavframe(void* opaque, uint8_t* data)
{
    LAVFrame* frame = (LAVFrame*)opaque;
    FreeLAVFrameBuffers(frame);
    SAFE_CO_FREE(opaque);
}

static void lav_unref_frame(void* opaque, uint8_t* data)
{
    AVBufferRef* buf = (AVBufferRef*)opaque;
    av_buffer_unref(&buf);
}

static void avfilter_free_lav_buffer(LAVFrame* pFrame)
{
    av_frame_free((AVFrame**)&pFrame->priv_data);
}
