#pragma once

#include "IVideoRenderer.h"

namespace VideoRenderer {

    // 渲染器性能统计信息。注意：先处理完最后一帧，再更新统计信息，最后一帧被纳入计数器。
    struct Statistics_t {
        enum { CHANNEL_COUNT = INPUT_PIN_COUNT  };
        enum { FRAME_COUNT = 300 }; // 对于30FPS的媒体源，统计时间跨度约为10秒。

        //---------------------------------------------------------------------------------------------
        // 输入帧统计
        //---------------------------------------------------------------------------------------------

        // 每通道输入帧总计
        uint64_t totalInputFrames[CHANNEL_COUNT]; // 每通道累计输入的小帧数，小于等于FRAME_COUNT说明统计样本未回绕，应以将此字段作为差方和的除数。
        uint64_t discardedInputFrames[CHANNEL_COUNT]; //  每通道累计丢弃的输入帧数，这些被丢弃的帧未被合成。
        uint64_t mixedInputFrames[CHANNEL_COUNT]; //  每通道参与了合成的帧数。即使参与合成了大帧，合成的大帧也可能被丢弃。
        int64_t lastInputFrameIndex[CHANNEL_COUNT]; // 在统计样本空间内索引，对FRAME_COUNT取模回绕。初始为-1，表示当前无帧，保持语义正确。

        // 输入帧到达时间间隔统计循环缓冲区。
        REFERENCE_TIME lastFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT]; // 最后一帧的输入间隔。第1帧为0。
        REFERENCE_TIME minFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT];
        REFERENCE_TIME maxFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT];
        REFERENCE_TIME avgFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT]; // 可用于计算媒体源最近FRAME_COUNT帧内的平均FPS。
        REFERENCE_TIME stdDevFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT]; // 预期到达间隔与实际到达间隔的差方均

        //---------------------------------------------------------------------------------------------
        // 呈现帧统计
        //---------------------------------------------------------------------------------------------

        // 呈现延迟抖动 = 实际呈现时间 - 媒体样本的PTS。正数表示延迟，负数表示提前。
        uint64_t totalMixedFrames; // 累计已合成的大帧数。
        uint64_t discardedPresentFrames; // 累计已合成但是未被呈现大帧数。
        int64_t lastPresentFrameIndex; // 在统计样本空间内索引，对FRAME_COUNT取模回绕。初始为-1，表示当前无帧，保持语义正确。

        // 合成呈现器输出帧呈现延迟抖动统计循环缓冲区。
        REFERENCE_TIME lastFramePresentJitter[FRAME_COUNT]; // 最后呈现的一帧的抖动。
        REFERENCE_TIME minFramePresentJitter[FRAME_COUNT];
        REFERENCE_TIME maxFramePresentJitter[FRAME_COUNT];
        REFERENCE_TIME avgFramePresentJitter[FRAME_COUNT]; // 长期来看，可能无限趋近于0。
        REFERENCE_TIME stdDevFramePresentJitter[FRAME_COUNT]; // 抖动（差）的方均根。

        Statistics_t() {
            memset(this, 0, sizeof(Statistics_t));
            for (int i = 0; i < CHANNEL_COUNT; ++i) {
                lastInputFrameIndex[i] = -1;
            }
            lastPresentFrameIndex = -1;
        }
    }; // end struct Statistics_t

} // end namespace VideoRenderer

#include "BaseRenderer.h"
#include "GDIRenderer.h"
