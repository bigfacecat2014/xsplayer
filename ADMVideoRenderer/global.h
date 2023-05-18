#pragma once

#include "IVideoRenderer.h"

namespace VideoRenderer {

    // ��Ⱦ������ͳ����Ϣ��ע�⣺�ȴ��������һ֡���ٸ���ͳ����Ϣ�����һ֡�������������
    struct Statistics_t {
        enum { CHANNEL_COUNT = INPUT_PIN_COUNT  };
        enum { FRAME_COUNT = 300 }; // ����30FPS��ý��Դ��ͳ��ʱ����ԼΪ10�롣

        //---------------------------------------------------------------------------------------------
        // ����֡ͳ��
        //---------------------------------------------------------------------------------------------

        // ÿͨ������֡�ܼ�
        uint64_t totalInputFrames[CHANNEL_COUNT]; // ÿͨ���ۼ������С֡����С�ڵ���FRAME_COUNT˵��ͳ������δ���ƣ�Ӧ�Խ����ֶ���Ϊ��͵ĳ�����
        uint64_t discardedInputFrames[CHANNEL_COUNT]; //  ÿͨ���ۼƶ���������֡������Щ��������֡δ���ϳɡ�
        uint64_t mixedInputFrames[CHANNEL_COUNT]; //  ÿͨ�������˺ϳɵ�֡������ʹ����ϳ��˴�֡���ϳɵĴ�֡Ҳ���ܱ�������
        int64_t lastInputFrameIndex[CHANNEL_COUNT]; // ��ͳ�������ռ�����������FRAME_COUNTȡģ���ơ���ʼΪ-1����ʾ��ǰ��֡������������ȷ��

        // ����֡����ʱ����ͳ��ѭ����������
        REFERENCE_TIME lastFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT]; // ���һ֡������������1֡Ϊ0��
        REFERENCE_TIME minFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT];
        REFERENCE_TIME maxFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT];
        REFERENCE_TIME avgFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT]; // �����ڼ���ý��Դ���FRAME_COUNT֡�ڵ�ƽ��FPS��
        REFERENCE_TIME stdDevFrameInputInterval[CHANNEL_COUNT][FRAME_COUNT]; // Ԥ�ڵ�������ʵ�ʵ������Ĳ��

        //---------------------------------------------------------------------------------------------
        // ����֡ͳ��
        //---------------------------------------------------------------------------------------------

        // �����ӳٶ��� = ʵ�ʳ���ʱ�� - ý��������PTS��������ʾ�ӳ٣�������ʾ��ǰ��
        uint64_t totalMixedFrames; // �ۼ��ѺϳɵĴ�֡����
        uint64_t discardedPresentFrames; // �ۼ��Ѻϳɵ���δ�����ִ�֡����
        int64_t lastPresentFrameIndex; // ��ͳ�������ռ�����������FRAME_COUNTȡģ���ơ���ʼΪ-1����ʾ��ǰ��֡������������ȷ��

        // �ϳɳ��������֡�����ӳٶ���ͳ��ѭ����������
        REFERENCE_TIME lastFramePresentJitter[FRAME_COUNT]; // �����ֵ�һ֡�Ķ�����
        REFERENCE_TIME minFramePresentJitter[FRAME_COUNT];
        REFERENCE_TIME maxFramePresentJitter[FRAME_COUNT];
        REFERENCE_TIME avgFramePresentJitter[FRAME_COUNT]; // ������������������������0��
        REFERENCE_TIME stdDevFramePresentJitter[FRAME_COUNT]; // ��������ķ�������

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
