// Copyright 2022-2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

/* STD headers */
#include <string.h>
#include <stdint.h>
#include <xcore/hwtimer.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"
#include "stream_buffer.h"

/* Library headers */
#include "generic_pipeline.h"
#include "aec.h"
#include "agc.h"
#include "ic.h"
#include "ns.h"
#include "adec.h"

/* App headers */
#include "app_conf.h"
#include "audio_pipeline.h"
#include "audio_pipeline_dsp.h"
#include "platform/driver_instances.h"
#include "aec_telemetry.h"

#if appconfAUDIO_PIPELINE_FRAME_ADVANCE != 240
#error This pipeline is only configured for 240 frame advance
#endif

#if ON_TILE(0)
static ic_stage_ctx_t DWORD_ALIGNED ic_stage_state = {};
static ns_stage_ctx_t DWORD_ALIGNED ns_stage_state = {};
static agc_stage_ctx_t DWORD_ALIGNED agc_stage_state = {};

static uint32_t pcm16_power(const int32_t *samples)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < appconfAUDIO_PIPELINE_FRAME_ADVANCE; ++i) {
        const int64_t sample = samples[i] >> 16;
        sum += sample * sample;
    }
    return (uint32_t)(sum / appconfAUDIO_PIPELINE_FRAME_ADVANCE);
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

static void update_aec_telemetry(frame_data_t *frame_data)
{
    static uint64_t power_sums[4];
    static uint64_t subtraction_power_sums[2][3];
    static uint32_t frames;
    static uint32_t reference_active;
    static uint32_t reference_active_frames;
    static uint32_t adaptation_active_frames;
    static uint32_t frame_counter;
    static uint32_t correction_count;
    static int32_t previous_delay;

    if (frame_data->adaptation_probe_valid)
        ffva_aec_adaptation_update(&frame_data->adaptation_probe);

    const uint32_t ref_0 = pcm16_power(frame_data->aec_reference_audio_samples[0]);
    const uint32_t ref_1 = pcm16_power(frame_data->aec_reference_audio_samples[1]);
    power_sums[0] += ref_0 > ref_1 ? ref_0 : ref_1;
    power_sums[1] += pcm16_power(frame_data->mic_samples_passthrough[0]);
    power_sums[2] += frame_data->aec_residual_power;
    power_sums[3] += pcm16_power(frame_data->samples[0]);
    for (size_t ch = 0; ch < 2; ++ch) {
        subtraction_power_sums[ch][0] += frame_data->aec_input_power[ch];
        subtraction_power_sums[ch][1] += frame_data->aec_subtraction_power[ch];
        subtraction_power_sums[ch][2] += frame_data->aec_residual_power_per_mic[ch];
    }
    reference_active |= frame_data->ref_active_flag != 0;
    reference_active_frames += frame_data->ref_active_flag != 0;
    adaptation_active_frames += frame_data->debug_mu_active != 0;
    if (frame_data->adec_applied_delay_samples != previous_delay) {
        previous_delay = frame_data->adec_applied_delay_samples;
        ++correction_count;
    }
    ++frames;
    if (frames == 10) {
        ffva_aec_telemetry_t levels = {
            .reference_power = (uint32_t)(power_sums[0] / frames),
            .microphone_power = (uint32_t)(power_sums[1] / frames),
            .residual_power = (uint32_t)(power_sums[2] / frames),
            .output_power = (uint32_t)(power_sums[3] / frames),
            .reference_active = reference_active,
            .frame_counter = frame_counter + frames,
        };
        ffva_adec_telemetry_t adec = {
            .applied_delay_samples = frame_data->adec_applied_delay_samples,
            .measured_delay_samples = frame_data->adec_measured_delay_samples,
            .mode = frame_data->adec_mode,
            .estimator_enabled = frame_data->adec_estimator_enabled,
            .force_cycle_pending = frame_data->adec_force_cycle_pending,
            .reference_active = reference_active,
            .correction_count = correction_count,
            .frame_counter = frame_counter + frames,
        };
        ffva_aec_debug_t debug = {
            .accepted_peak_phase = frame_data->debug_accepted_peak_phase,
            .accepted_peak_ratio_x100 = frame_data->debug_accepted_peak_ratio_x100,
            .current_peak_phase = frame_data->debug_current_peak_phase,
            .current_peak_ratio_x100 = frame_data->debug_current_peak_ratio_x100,
            .reference_active_frames = reference_active_frames,
            .adaptation_active_frames = adaptation_active_frames,
            .adaptation_mode = frame_data->debug_adaptation_mode,
            .core_bypass = frame_data->debug_core_bypass,
            .shadow_flag = frame_data->debug_shadow_flag,
            .coherence_freeze_timer = frame_data->debug_mu_coh_timer,
            .frame_counter = frame_counter + frames,
        };
        ffva_aec_subtraction_t subtraction = {
            .frame_counter = frame_counter + frames,
        };
        ffva_aec_reference_probe_t reference_probe = {
            .frame_counter = frame_counter + frames,
            .tile1_peak = {frame_data->ref_probe_peak[0], frame_data->ref_probe_peak[1]},
            .num_x_channels = frame_data->ref_probe_num_x_channels,
            .direct_activity = frame_data->ref_probe_direct_activity,
            .aec_shared_activity = frame_data->ref_probe_aec_shared_activity,
            .propagated_activity = (uint32_t)(frame_data->ref_active_flag != 0),
            .tile0_peak = {pcm16_peak(frame_data->aec_reference_audio_samples[0]),
                           pcm16_peak(frame_data->aec_reference_audio_samples[1])},
            .tile0_activity = (uint32_t)(frame_data->ref_active_flag != 0),
        };
        for (size_t ch = 0; ch < 2; ++ch) {
            subtraction.input_power[ch] = (uint32_t)(subtraction_power_sums[ch][0] / frames);
            subtraction.subtraction_power[ch] = (uint32_t)(subtraction_power_sums[ch][1] / frames);
            subtraction.residual_power[ch] = (uint32_t)(subtraction_power_sums[ch][2] / frames);
        }
        ffva_aec_telemetry_update(&levels);
        ffva_adec_telemetry_update(&adec);
        ffva_aec_debug_update(&debug);
        ffva_aec_subtraction_update(&subtraction);
        ffva_aec_reference_probe_update(&reference_probe);
        frame_counter += frames;
        memset(power_sums, 0, sizeof(power_sums));
        memset(subtraction_power_sums, 0, sizeof(subtraction_power_sums));
        reference_active = 0;
        reference_active_frames = 0;
        adaptation_active_frames = 0;
        frames = 0;
    }
}

static void *audio_pipeline_input_i(void *input_app_data)
{
    frame_data_t *frame_data;

    frame_data = pvPortMalloc(sizeof(frame_data_t));
    memset(frame_data, 0x00, sizeof(frame_data_t));

    size_t bytes_received = 0;
    bytes_received = rtos_intertile_rx_len(
            intertile_ctx,
            appconfAUDIOPIPELINE_PORT,
            portMAX_DELAY);

    xassert(bytes_received == sizeof(frame_data_t));

    rtos_intertile_rx_data(
            intertile_ctx,
            frame_data,
            bytes_received);

    return frame_data;
}

static int audio_pipeline_output_i(frame_data_t *frame_data,
                                   void *output_app_data)
{
    return audio_pipeline_output(output_app_data,
                               (int32_t **)frame_data->samples,
                               6,
                               appconfAUDIO_PIPELINE_FRAME_ADVANCE);
}

static void stage_vnr_and_ic(frame_data_t *frame_data)
{
#if appconfAUDIO_PIPELINE_SKIP_IC_AND_VNR
#else
    int32_t DWORD_ALIGNED ic_output[appconfAUDIO_PIPELINE_FRAME_ADVANCE];
    float_s32_t input_vnr_pred;
    ic_process_frame(&ic_stage_state.state,
                     frame_data->samples[0],
                     frame_data->samples[1],
                     ic_output,
                     &input_vnr_pred);
    frame_data->vnr_pred_flag = input_vnr_pred;

    /* Intentionally ignoring comms ch from here on out */
    memcpy(frame_data->samples, ic_output, appconfAUDIO_PIPELINE_FRAME_ADVANCE * sizeof(int32_t));
#endif
}

static void stage_ns(frame_data_t *frame_data)
{
#if appconfAUDIO_PIPELINE_SKIP_NS
#else
    int32_t DWORD_ALIGNED ns_output[appconfAUDIO_PIPELINE_FRAME_ADVANCE];
    configASSERT(NS_FRAME_ADVANCE == appconfAUDIO_PIPELINE_FRAME_ADVANCE);
    ns_process_frame(
                &ns_stage_state.state,
                ns_output,
                frame_data->samples[0]);
    memcpy(frame_data->samples, ns_output, appconfAUDIO_PIPELINE_FRAME_ADVANCE * sizeof(int32_t));
#endif
}

static void stage_agc(frame_data_t *frame_data)
{
#if appconfAUDIO_PIPELINE_SKIP_AGC
#else
    int32_t DWORD_ALIGNED agc_output[appconfAUDIO_PIPELINE_FRAME_ADVANCE];
    configASSERT(AGC_FRAME_ADVANCE == appconfAUDIO_PIPELINE_FRAME_ADVANCE);

    agc_stage_state.md.vnr_flag = frame_data->vnr_pred_flag;
    agc_stage_state.md.aec_ref_power = frame_data->max_ref_energy;
    agc_stage_state.md.aec_corr_factor = frame_data->aec_corr_factor;
    agc_stage_state.md.ref_active_flag = frame_data->ref_active_flag;

    agc_process_frame(
            &agc_stage_state.state,
            agc_output,
            frame_data->samples[0],
            &agc_stage_state.md);
    memcpy(frame_data->samples, agc_output, appconfAUDIO_PIPELINE_FRAME_ADVANCE * sizeof(int32_t));
#endif
    update_aec_telemetry(frame_data);
}

static void initialize_pipeline_stages(void)
{
    ic_init(&ic_stage_state.state);

    ns_init(&ns_stage_state.state);

#if defined(appconfFFVA_AGC_FIXED_GAIN_25) && appconfFFVA_AGC_FIXED_GAIN_25
    agc_init(&agc_stage_state.state, &AGC_PROFILE_FIXED_GAIN);
#else
    agc_init(&agc_stage_state.state, &AGC_PROFILE_ASR);
#endif
    agc_stage_state.md = agc_meta_data_init();
}

void audio_pipeline_init(
    void *input_app_data,
    void *output_app_data)
{
    const int stage_count = 3;

    const pipeline_stage_t stages[] = {
        (pipeline_stage_t)stage_vnr_and_ic,
        (pipeline_stage_t)stage_ns,
        (pipeline_stage_t)stage_agc,
    };

    const configSTACK_DEPTH_TYPE stage_stack_sizes[] = {
        configMINIMAL_STACK_SIZE + RTOS_THREAD_STACK_SIZE(stage_vnr_and_ic) + RTOS_THREAD_STACK_SIZE(audio_pipeline_input_i),
        configMINIMAL_STACK_SIZE + RTOS_THREAD_STACK_SIZE(stage_ns),
        configMINIMAL_STACK_SIZE + RTOS_THREAD_STACK_SIZE(stage_agc) + RTOS_THREAD_STACK_SIZE(audio_pipeline_output_i),
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

#endif /* ON_TILE(0)*/
