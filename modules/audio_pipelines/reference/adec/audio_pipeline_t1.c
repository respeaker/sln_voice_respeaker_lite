// Copyright 2022-2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

/* STD headers */
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <xcore/hwtimer.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"
#include "stream_buffer.h"

/* Library headers */
#include "generic_pipeline.h"
#include "adec.h"

/* App headers */
#include "app_conf.h"
#include "audio_pipeline.h"
#include "audio_pipeline_dsp.h"
#include "platform/driver_instances.h"
#include "stage1.h"

#if appconfAUDIO_PIPELINE_FRAME_ADVANCE != 240
#error This pipeline is only configured for 240 frame advance
#endif

#if ON_TILE(1)
// Stage1 - AEC, DE, ADEC
static stage1_t DWORD_ALIGNED stage_1_state;
static aec_conf_t aec_de_mode_conf;
static aec_conf_t aec_non_de_mode_conf;
static adec_config_t adec_conf;

// The AEC overlap-add emits error[240:480], while the current input frame
// occupies y[272:512]. Thus its output is 32 samples (2 ms) behind input_y.
enum { AEC_OUTPUT_LAG_SAMPLES = AEC_PROC_FRAME_LENGTH - 2 * AEC_FRAME_ADVANCE };
static int32_t previous_input_tail[AEC_MAX_Y_CHANNELS][AEC_OUTPUT_LAG_SAMPLES];
static uint32_t adaptation_frame_counter;
static uint32_t main_active_frames[2];
static uint32_t freeze_active_frames[2];
static uint32_t shadow_copy_frames[2];

static int32_t scaled_x1000(float_s32_t value)
{
    const double scaled = 1000.0 * float_s32_to_double(value);
    if (scaled > INT32_MAX) return INT32_MAX;
    if (scaled < INT32_MIN) return INT32_MIN;
    return (int32_t)scaled;
}

static int32_t shadow_error_ratio_x1000(const aec_state_t *aec, size_t ch)
{
    const double main_error = float_s32_to_double(aec->main_state.overall_Error[ch]);
    if (main_error <= 0.0) return -1;
    const double ratio = 1000.0 * float_s32_to_double(aec->shadow_state.overall_Error[ch]) / main_error;
    if (ratio > INT32_MAX) return INT32_MAX;
    return (int32_t)ratio;
}

static uint32_t pcm16_peak(const int32_t *samples)
{
    uint32_t peak = 0;
    for (size_t i = 0; i < appconfAUDIO_PIPELINE_FRAME_ADVANCE; ++i) {
        const int32_t value = samples[i] >> 16;
        const uint32_t magnitude = value < 0 ? (uint32_t)-value : (uint32_t)value;
        if (magnitude > peak) peak = magnitude;
    }
    return peak;
}

static void measure_aligned_aec_output(const int32_t *input, const int32_t *residual,
                                       int32_t *previous_tail, int estimator_passthrough,
                                       uint32_t *input_power, uint32_t *residual_power,
                                       uint32_t *subtraction_power)
{
    uint64_t input_sum = 0;
    uint64_t residual_sum = 0;
    uint64_t subtraction_sum = 0;
    for (size_t i = 0; i < appconfAUDIO_PIPELINE_FRAME_ADVANCE; ++i) {
        // During delay estimation Stage 1 explicitly replaces the AEC output
        // with the undelayed input, so only the normal AEC output needs this
        // overlap-add alignment.
        const int32_t aligned_input = estimator_passthrough ? input[i] :
            (i < AEC_OUTPUT_LAG_SAMPLES ? previous_tail[i] : input[i - AEC_OUTPUT_LAG_SAMPLES]);
        const int64_t y = aligned_input >> 16;
        const int64_t e = residual[i] >> 16;
        const int64_t removed = y - e;
        input_sum += y * y;
        residual_sum += e * e;
        subtraction_sum += removed * removed;
    }
    memcpy(previous_tail, input + appconfAUDIO_PIPELINE_FRAME_ADVANCE - AEC_OUTPUT_LAG_SAMPLES,
           sizeof(int32_t) * AEC_OUTPUT_LAG_SAMPLES);
    *input_power = (uint32_t)(input_sum / appconfAUDIO_PIPELINE_FRAME_ADVANCE);
    *residual_power = (uint32_t)(residual_sum / appconfAUDIO_PIPELINE_FRAME_ADVANCE);
    *subtraction_power = (uint32_t)(subtraction_sum / appconfAUDIO_PIPELINE_FRAME_ADVANCE);
}

static void *audio_pipeline_input_i(void *input_app_data)
{
    frame_data_t *frame_data;

    frame_data = pvPortMalloc(sizeof(frame_data_t));
    memset(frame_data, 0x00, sizeof(frame_data_t));

    audio_pipeline_input(input_app_data,
                       (int32_t **)frame_data->aec_reference_audio_samples,
                       4,
                       appconfAUDIO_PIPELINE_FRAME_ADVANCE);

    frame_data->vnr_pred_flag = AGC_META_DATA_NO_VNR;

    memcpy(frame_data->samples, frame_data->mic_samples_passthrough, sizeof(frame_data->samples));

    return frame_data;
}

static int audio_pipeline_output_i(frame_data_t *frame_data,
                                   void *output_app_data)
{
    rtos_intertile_tx(intertile_ctx,
                      appconfAUDIOPIPELINE_PORT,
                      frame_data,
                      sizeof(frame_data_t));
    return AUDIO_PIPELINE_FREE_FRAME;
}

static void stage_aec(frame_data_t *frame_data)
{
#if appconfAUDIO_PIPELINE_SKIP_AEC
#else
    int32_t DWORD_ALIGNED stage_1_out[AEC_MAX_Y_CHANNELS][appconfAUDIO_PIPELINE_FRAME_ADVANCE];
    const uint32_t num_x_channels = stage_1_state.aec_state.main_state.shared_state->num_x_channels;
    // Stage 1 can change delay-estimation mode at the end of this call. Save
    // the mode that actually produced the output we are about to measure.
    const int estimator_passthrough = stage_1_state.delay_estimator_enabled;

    stage1_process_frame(&stage_1_state,
                          &stage_1_out[0],
                          &frame_data->max_ref_energy,
                          &frame_data->aec_corr_factor,
                          &frame_data->ref_active_flag,
                          frame_data->samples,
                          frame_data->aec_reference_audio_samples);

    // Stage 1's detector ran on this same reference array. A positive delay
    // modifies only the microphone array; a negative delay modifies this array
    // in place before detection. Preserve the channel count used by this frame
    // because Stage 1 can switch between DE (one X) and normal AEC (two X) at
    // the end of the call.
    frame_data->ref_probe_peak[0] = pcm16_peak(frame_data->aec_reference_audio_samples[0]);
    frame_data->ref_probe_peak[1] = pcm16_peak(frame_data->aec_reference_audio_samples[1]);
    frame_data->ref_probe_num_x_channels = num_x_channels;
    frame_data->ref_probe_direct_activity = aec_detect_input_activity(
        frame_data->aec_reference_audio_samples, stage_1_state.ref_active_threshold, num_x_channels);
    frame_data->ref_probe_aec_shared_activity =
        stage_1_state.aec_state.main_state.shared_state->ref_active_flag;

    // Stage 1 may have delayed input_y in place. Compare each output against
    // that same input stream shifted by the AEC's 32-sample overlap-add lag.
    for (size_t ch = 0; ch < 2; ++ch) {
        measure_aligned_aec_output(frame_data->samples[ch], stage_1_out[ch], previous_input_tail[ch],
                                   estimator_passthrough, &frame_data->aec_input_power[ch],
                                   &frame_data->aec_residual_power_per_mic[ch],
                                   &frame_data->aec_subtraction_power[ch]);
    }
    memcpy(frame_data->samples, stage_1_out, AEC_MAX_Y_CHANNELS * appconfAUDIO_PIPELINE_FRAME_ADVANCE * sizeof(int32_t));
    frame_data->aec_residual_power = frame_data->aec_residual_power_per_mic[0];
    frame_data->adec_applied_delay_samples = stage_1_state.delay_state.delay_samples;
    frame_data->adec_measured_delay_samples = stage_1_state.adec_state.last_measured_delay;
    frame_data->adec_mode = stage_1_state.adec_state.mode;
    frame_data->adec_estimator_enabled = stage_1_state.delay_estimator_enabled;
    frame_data->adec_force_cycle_pending = stage_1_state.adec_state.adec_config.force_de_cycle_trigger;
    frame_data->debug_current_peak_phase = stage_1_state.debug_current_peak_phase;
    frame_data->debug_current_peak_ratio_x100 = stage_1_state.debug_current_peak_ratio_x100;
    frame_data->debug_accepted_peak_phase = stage_1_state.debug_accepted_peak_phase;
    frame_data->debug_accepted_peak_ratio_x100 = stage_1_state.debug_accepted_peak_ratio_x100;
    frame_data->debug_adaptation_mode =
        stage_1_state.aec_state.main_state.shared_state->config_params.coh_mu_conf.adaption_config;
    frame_data->debug_core_bypass =
        stage_1_state.aec_state.main_state.shared_state->config_params.aec_core_conf.bypass;
    frame_data->debug_shadow_flag =
        stage_1_state.aec_state.main_state.shared_state->shadow_filter_params.shadow_flag[0];
    frame_data->debug_mu_active =
        stage_1_state.aec_state.main_state.shared_state->coh_mu_state[0].coh_mu[0].mant != 0 ||
        stage_1_state.aec_state.main_state.shared_state->coh_mu_state[0].coh_mu[1].mant != 0;
    frame_data->debug_mu_coh_timer =
        stage_1_state.aec_state.main_state.shared_state->coh_mu_state[0].mu_coh_timer;
    const aec_state_t *aec = &stage_1_state.aec_state;
    const aec_shared_filter_state_t *shared = aec->main_state.shared_state;
    ++adaptation_frame_counter;
    for (size_t ch = 0; ch < 2; ++ch) {
        const coherence_mu_params_t *coh = &shared->coh_mu_state[ch];
        main_active_frames[ch] += coh->coh_mu[0].mant != 0 || coh->coh_mu[1].mant != 0;
        freeze_active_frames[ch] += coh->mu_coh_timer > 0;
        shadow_copy_frames[ch] += shared->shadow_filter_params.shadow_flag[ch] == COPY;
    }
    if (adaptation_frame_counter % 10 == 0) {
        ffva_aec_adaptation_t *probe = &frame_data->adaptation_probe;
        probe->frame_counter = adaptation_frame_counter;
        for (size_t ch = 0; ch < 2; ++ch) {
            const coherence_mu_params_t *coh = &shared->coh_mu_state[ch];
            probe->coherence_x1000[ch] = scaled_x1000(coh->coh);
            probe->slow_coherence_x1000[ch] = scaled_x1000(coh->coh_slow);
            probe->erle_x1000[ch] = scaled_x1000(coh->erle);
            probe->moving_erle_x1000[ch] = scaled_x1000(coh->mov_erle);
            for (size_t x = 0; x < 2; ++x)
                probe->main_mu_x1000[ch][x] = scaled_x1000(coh->coh_mu[x]);
            probe->freeze_timer[ch] = coh->mu_coh_timer;
            probe->shadow_flag[ch] = shared->shadow_filter_params.shadow_flag[ch];
            probe->shadow_better_count[ch] = shared->shadow_filter_params.shadow_better_count[ch];
            probe->shadow_reset_count[ch] = shared->shadow_filter_params.shadow_reset_count[ch];
            probe->shadow_error_ratio_x1000[ch] = shadow_error_ratio_x1000(aec, ch);
            probe->main_active_frames[ch] = main_active_frames[ch];
            probe->freeze_active_frames[ch] = freeze_active_frames[ch];
            probe->shadow_copy_frames[ch] = shadow_copy_frames[ch];
            main_active_frames[ch] = 0;
            freeze_active_frames[ch] = 0;
            shadow_copy_frames[ch] = 0;
        }
        frame_data->adaptation_probe_valid = 1;
    }
#endif
}

static void initialize_pipeline_stages(void)
{
    aec_non_de_mode_conf.num_y_channels = 2;
    aec_non_de_mode_conf.num_x_channels = 2;
    aec_non_de_mode_conf.num_main_filt_phases = AEC_MAIN_FILTER_PHASES;
    aec_non_de_mode_conf.num_shadow_filt_phases = AEC_SHADOW_FILTER_PHASES;
    aec_non_de_mode_conf.tdist = &aec_tdist_chans2_threads1;

    aec_de_mode_conf.num_y_channels = 1;
    aec_de_mode_conf.num_x_channels = 1;
    aec_de_mode_conf.num_main_filt_phases = 30;
    aec_de_mode_conf.num_shadow_filt_phases = 0;
    aec_de_mode_conf.tdist = &aec_tdist_chans2_threads1;

    // Disable ADEC's automatic mode. We only want to estimate and correct for the delay at startup
    adec_conf.bypass = 1; // Bypass automatic DE correction
    adec_conf.force_de_cycle_trigger = 1; // Force a delay correction cycle, so that delay correction happens once after initialisation. Make sure this is set back to 0 after adec has requested a transition into DE mode once, to stop any further delay correction (automatic or forced) by ADEC
    stage1_init(&stage_1_state, &aec_de_mode_conf, &aec_non_de_mode_conf, &adec_conf);
}

void audio_pipeline_init(
    void *input_app_data,
    void *output_app_data)
{
    const int stage_count = 1;

    const pipeline_stage_t stages[] = {
        (pipeline_stage_t)stage_aec,
    };

    const configSTACK_DEPTH_TYPE stage_stack_sizes[] = {
        configMINIMAL_STACK_SIZE + RTOS_THREAD_STACK_SIZE(stage_aec) + RTOS_THREAD_STACK_SIZE(audio_pipeline_output_i) + RTOS_THREAD_STACK_SIZE(audio_pipeline_input_i),

    };

    initialize_pipeline_stages();

    generic_pipeline_init((pipeline_input_t)audio_pipeline_input_i,
                        (pipeline_output_t)audio_pipeline_output_i,
                        input_app_data,
                        output_app_data,
                        stages,
                        (const size_t*) stage_stack_sizes,
                        appconfAUDIO_PIPELINE_TASK_PRIORITY,
                        stage_count);
}
#endif /* ON_TILE(1) */
