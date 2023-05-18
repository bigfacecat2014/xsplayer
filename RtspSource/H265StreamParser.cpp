#include "stdafx.h"
#include "H265StreamParser.h"

#include "BitVector.hh"
#include "H264or5VideoStreamFramer.hh"
#define SPS_MAX_SIZE 1000

namespace
{
    const uint8_t subWidthChroma[] = {1, 2, 2, 1};
    const uint8_t subHeightChroma[] = {1, 2, 1, 1};
}

static void s_h265_profile_tier_level(BitVector& bv, uint32_t max_sub_layers_minus1)
{
    bv.skipBits(96);

    uint32_t i;
    Boolean sub_layer_profile_present_flag[7], sub_layer_level_present_flag[7];
    for (i = 0; i < max_sub_layers_minus1; ++i) {
        sub_layer_profile_present_flag[i] = bv.get1BitBoolean();
        sub_layer_level_present_flag[i] = bv.get1BitBoolean();
    }
    if (max_sub_layers_minus1 > 0) {
        bv.skipBits(2 * (8 - max_sub_layers_minus1)); // reserved_zero_2bits
    }
    for (i = 0; i < max_sub_layers_minus1; ++i) {
        if (sub_layer_profile_present_flag[i]) {
            bv.skipBits(88);
        }
        if (sub_layer_level_present_flag[i]) {
            bv.skipBits(8); // sub_layer_level_idc[i]
        }
    }
}

static void s_h265_analyze_vui_parameters(BitVector& bv, uint32_t& num_units_in_tick, uint32_t& time_scale)
{
    Boolean aspect_ratio_info_present_flag = bv.get1BitBoolean();
    if (aspect_ratio_info_present_flag) {
        uint32_t aspect_ratio_idc = bv.getBits(8);
        if (aspect_ratio_idc == 255/*Extended_SAR*/) {
            bv.skipBits(32); // sar_width; sar_height
        }
    }
    Boolean overscan_info_present_flag = bv.get1BitBoolean();
    if (overscan_info_present_flag) {
        bv.skipBits(1); // overscan_appropriate_flag
    }
    Boolean video_signal_type_present_flag = bv.get1BitBoolean();
    if (video_signal_type_present_flag) {
        bv.skipBits(4); // video_format; video_full_range_flag
        Boolean colour_description_present_flag = bv.get1BitBoolean();
        if (colour_description_present_flag) {
            bv.skipBits(24); // colour_primaries; transfer_characteristics; matrix_coefficients
        }
    }
    Boolean chroma_loc_info_present_flag = bv.get1BitBoolean();
    if (chroma_loc_info_present_flag) {
        (void)bv.get_expGolomb(); // chroma_sample_loc_type_top_field
        (void)bv.get_expGolomb(); // chroma_sample_loc_type_bottom_field
    }
    // H.265特殊处理
    {
        bv.skipBits(3); // neutral_chroma_indication_flag, field_seq_flag, frame_field_info_present_flag
        Boolean default_display_window_flag = bv.get1BitBoolean();
        if (default_display_window_flag) {
            (void)bv.get_expGolomb(); // def_disp_win_left_offset
            (void)bv.get_expGolomb(); // def_disp_win_right_offset
            (void)bv.get_expGolomb(); // def_disp_win_top_offset
            (void)bv.get_expGolomb(); // def_disp_win_bottom_offset
        }
    }
    Boolean timing_info_present_flag = bv.get1BitBoolean();
    if (timing_info_present_flag) {
        num_units_in_tick = bv.getBits(32);
        time_scale = bv.getBits(32);
        // H.265特殊处理
        {
            Boolean vui_poc_proportional_to_timing_flag = bv.get1BitBoolean();
            if (vui_poc_proportional_to_timing_flag) {
                uint32_t vui_num_ticks_poc_diff_one_minus1 = bv.get_expGolomb();
            }
        }
    }
}

#define VPS_MAX_SIZE 1000 // larger than the largest possible VPS (Video Parameter Set) NAL unit

H265VPSParser::H265VPSParser(uint8_t* vps, uint32_t vpsSize)
    : _vps(SPS_MAX_SIZE), _width(0), _height(0), _framerate(0)
{
    uint32_t num_units_in_tick = 0;
    uint32_t time_scale = 0; // 用于计算FPS

    // Begin by making a copy of the NAL unit data, removing any 'emulation prevention' bytes:
    removeH264or5EmulationBytes(_vps.data(), _vps.size(), vps, vpsSize);
    
    // 解析临时比特向量中的VPS字段
    {
        BitVector bv(_vps.data(), 0, 8 * vpsSize);

        uint32_t i;

        bv.skipBits(8); // forbidden_zero_bit; nal_ref_idc; nal_unit_type
        uint32_t profile_idc = bv.getBits(8);
        bv.skipBits(4); // vps_video_parameter_set_id
        bv.skipBits(1); // vps_base_layer_internal_flag
        bv.skipBits(1); // vps_base_layer_available_flag
        bv.skipBits(6); // vps_max_layers_minus1
        uint32_t vps_max_sub_layers_minus1 = bv.getBits(3);
        bv.skipBits(1); // vps_temporal_id_nesting_flag
        bv.skipBits(16); // vps_reserved_0xffff_16bits
        s_h265_profile_tier_level(bv, vps_max_sub_layers_minus1);
        Boolean vps_sub_layer_ordering_info_present_flag = bv.get1BitBoolean();
        for (i = (vps_sub_layer_ordering_info_present_flag ? 0 : vps_max_sub_layers_minus1); i <= vps_max_sub_layers_minus1; ++i) {
            (void)bv.get_expGolomb(); // vps_max_dec_pic_buffering_minus1[i]
            (void)bv.get_expGolomb(); // vps_max_num_reorder_pics[i]
            (void)bv.get_expGolomb(); // vps_max_latency_increase_plus1[i]
        }
        uint32_t vps_max_layer_id = bv.getBits(6);
        uint32_t vps_num_layer_sets_minus1 = bv.get_expGolomb();
        for (i = 1; i <= vps_num_layer_sets_minus1; ++i) {
            bv.skipBits(vps_max_layer_id + 1); // layer_id_included_flag[i][0..vps_max_layer_id]
        }
        Boolean vps_timing_info_present_flag = bv.get1BitBoolean();
        if (vps_timing_info_present_flag) {
            num_units_in_tick = bv.getBits(32);
            time_scale = bv.getBits(32);
            Boolean vps_poc_proportional_to_timing_flag = bv.get1BitBoolean();
            if (vps_poc_proportional_to_timing_flag) {
                uint32_t vps_num_ticks_poc_diff_one_minus1 = bv.get_expGolomb();
            }
        }
        Boolean vps_extension_flag = bv.get1BitBoolean();
    }
    if (num_units_in_tick != 0)
        _framerate = time_scale / (2.0 * num_units_in_tick);
}

H265SPSParser::H265SPSParser(uint8_t* sps, uint32_t spsSize)
    : _sps(SPS_MAX_SIZE), _width(0), _height(0), _framerate(0)
{
    spsSize = removeH264or5EmulationBytes(_sps.data(), _sps.size(), sps, spsSize);
    uint32_t num_units_in_tick = 0;
    uint32_t time_scale = 0; // used to calc FPS

    BitVector bv(_sps.data(), 0, 8 * spsSize);
    {
        uint32_t i;

        bv.skipBits(16); // nal_unit_header
        bv.skipBits(4); // sps_video_parameter_set_id
        uint32_t sps_max_sub_layers_minus1 = bv.getBits(3);
        bv.skipBits(1); // sps_temporal_id_nesting_flag
        s_h265_profile_tier_level(bv, sps_max_sub_layers_minus1);
        (void)bv.get_expGolomb(); // sps_seq_parameter_set_id
        uint32_t chroma_format_idc = bv.get_expGolomb();
        if (chroma_format_idc == 3) bv.skipBits(1); // separate_colour_plane_flag
        uint32_t pic_width_in_luma_samples = bv.get_expGolomb();
        uint32_t pic_height_in_luma_samples = bv.get_expGolomb();
        _width = pic_width_in_luma_samples;
        _height = pic_height_in_luma_samples;
        Boolean conformance_window_flag = bv.get1BitBoolean();
        if (conformance_window_flag) {
            uint32_t conf_win_left_offset = bv.get_expGolomb();
            uint32_t conf_win_right_offset = bv.get_expGolomb();
            uint32_t conf_win_top_offset = bv.get_expGolomb();
            uint32_t conf_win_bottom_offset = bv.get_expGolomb();
        }
        (void)bv.get_expGolomb(); // bit_depth_luma_minus8
        (void)bv.get_expGolomb(); // bit_depth_chroma_minus8
        uint32_t log2_max_pic_order_cnt_lsb_minus4 = bv.get_expGolomb();
        Boolean sps_sub_layer_ordering_info_present_flag = bv.get1BitBoolean();
        for (i = (sps_sub_layer_ordering_info_present_flag ? 0 : sps_max_sub_layers_minus1);
            i <= sps_max_sub_layers_minus1; ++i) {
            (void)bv.get_expGolomb(); // sps_max_dec_pic_buffering_minus1[i]
            (void)bv.get_expGolomb(); // sps_max_num_reorder_pics[i]
            (void)bv.get_expGolomb(); // sps_max_latency_increase[i]
        }
        (void)bv.get_expGolomb(); // log2_min_luma_coding_block_size_minus3
        (void)bv.get_expGolomb(); // log2_diff_max_min_luma_coding_block_size
        (void)bv.get_expGolomb(); // log2_min_transform_block_size_minus2
        (void)bv.get_expGolomb(); // log2_diff_max_min_transform_block_size
        (void)bv.get_expGolomb(); // max_transform_hierarchy_depth_inter
        (void)bv.get_expGolomb(); // max_transform_hierarchy_depth_intra
        Boolean scaling_list_enabled_flag = bv.get1BitBoolean();
        if (scaling_list_enabled_flag) {
            Boolean sps_scaling_list_data_present_flag = bv.get1BitBoolean();
            if (sps_scaling_list_data_present_flag) {
                // scaling_list_data()
                uint32_t realSizeId = 0;
                for (uint32_t sizeId = 0; sizeId < 4; ++sizeId) {
                    realSizeId = (sizeId == 3u ? 2u : 6u);
                    for (uint32_t matrixId = 0; matrixId < realSizeId; ++matrixId) {
                        Boolean scaling_list_pred_mode_flag = bv.get1BitBoolean();
                        if (!scaling_list_pred_mode_flag) {
                            (void)bv.get_expGolomb(); // scaling_list_pred_matrix_id_delta[sizeId][matrixId]
                        }
                        else {
                            uint32_t const c = 1 << (4 + (sizeId << 1));
                            uint32_t coefNum = c < 64 ? c : 64;
                            if (sizeId > 1) {
                                (void)bv.get_expGolomb(); // scaling_list_dc_coef_minus8[sizeId][matrixId]
                            }
                            for (i = 0; i < coefNum; ++i) {
                                (void)bv.get_expGolomb(); // scaling_list_delta_coef
                            }
                        }
                    }
                }
            }
        }
        bv.skipBits(2); // amp_enabled_flag, sample_adaptive_offset_enabled_flag
        Boolean pcm_enabled_flag = bv.get1BitBoolean();
        if (pcm_enabled_flag) {
            bv.skipBits(8); // pcm_sample_bit_depth_luma_minus1, pcm_sample_bit_depth_chroma_minus1
            (void)bv.get_expGolomb(); // log2_min_pcm_luma_coding_block_size_minus3
            (void)bv.get_expGolomb(); // log2_diff_max_min_pcm_luma_coding_block_size
            bv.skipBits(1); // pcm_loop_filter_disabled_flag
        }
        uint32_t num_short_term_ref_pic_sets = bv.get_expGolomb();
        uint32_t num_negative_pics = 0, prev_num_negative_pics = 0;
        uint32_t num_positive_pics = 0, prev_num_positive_pics = 0;
        for (i = 0; i < num_short_term_ref_pic_sets; ++i) {
            Boolean inter_ref_pic_set_prediction_flag = False;
            if (i != 0) {
                inter_ref_pic_set_prediction_flag = bv.get1BitBoolean();
            }
            if (inter_ref_pic_set_prediction_flag) {
                if (i == num_short_term_ref_pic_sets) {
                    // This can't happen here, but it's in the spec, so we include it for completeness
                    (void)bv.get_expGolomb(); // delta_idx_minus1
                }
                bv.skipBits(1); // delta_rps_sign
                (void)bv.get_expGolomb(); // abs_delta_rps_minus1
                uint32_t NumDeltaPocs = prev_num_negative_pics + prev_num_positive_pics; // correct???
                for (uint32_t j = 0; j < NumDeltaPocs; ++j) {
                    Boolean used_by_curr_pic_flag = bv.get1BitBoolean();
                    if (!used_by_curr_pic_flag) bv.skipBits(1); // use_delta_flag[j]
                }
            }
            else {
                prev_num_negative_pics = num_negative_pics;
                num_negative_pics = bv.get_expGolomb();
                prev_num_positive_pics = num_positive_pics;
                num_positive_pics = bv.get_expGolomb();
                uint32_t k;
                for (k = 0; k < num_negative_pics; ++k) {
                    (void)bv.get_expGolomb(); // delta_poc_s0_minus1[k]
                    bv.skipBits(1); // used_by_curr_pic_s0_flag[k]
                }
                for (k = 0; k < num_positive_pics; ++k) {
                    (void)bv.get_expGolomb(); // delta_poc_s1_minus1[k]
                    bv.skipBits(1); // used_by_curr_pic_s1_flag[k]
                }
            }
        }
        Boolean long_term_ref_pics_present_flag = bv.get1BitBoolean();
        if (long_term_ref_pics_present_flag) {
            uint32_t num_long_term_ref_pics_sps = bv.get_expGolomb();
            for (i = 0; i < num_long_term_ref_pics_sps; ++i) {
                bv.skipBits(log2_max_pic_order_cnt_lsb_minus4); // lt_ref_pic_poc_lsb_sps[i]
                bv.skipBits(1); // used_by_curr_pic_lt_sps_flag[1]
            }
        }
        bv.skipBits(2); // sps_temporal_mvp_enabled_flag, strong_intra_smoothing_enabled_flag
        Boolean vui_parameters_present_flag = bv.get1BitBoolean();
        if (vui_parameters_present_flag) {
            s_h265_analyze_vui_parameters(bv, num_units_in_tick, time_scale);
            _framerate = (float)(time_scale) / (float)(num_units_in_tick);;
        }
        Boolean sps_extension_flag = bv.get1BitBoolean();
    }
 }