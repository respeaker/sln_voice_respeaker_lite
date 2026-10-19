// Copyright 2022-2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

#ifndef AUDIO_PIPELINE_DSP_H_
#define AUDIO_PIPELINE_DSP_H_

#include <stdint.h>
#include "app_conf.h"
#include "aec_telemetry.h"

/* Pipeline config */
#define AP_MAX_Y_CHANNELS (2)
#define AP_MAX_X_CHANNELS (2)

/* AEC config */
#define AEC_MAIN_FILTER_PHASES    (10)
#define AEC_SHADOW_FILTER_PHASES    (5)

#include "aec.h"
#include "agc.h"
#include "ic.h"
#include "ns.h"
#include "adec.h"

/* Note: Changing the order here will effect the channel order for
 * audio_pipeline_input() and audio_pipeline_output()
 */
typedef struct {
    int32_t samples[appconfAUDIO_PIPELINE_CHANNELS][appconfAUDIO_PIPELINE_FRAME_ADVANCE];
    int32_t aec_reference_audio_samples[appconfAUDIO_PIPELINE_CHANNELS][appconfAUDIO_PIPELINE_FRAME_ADVANCE];
    int32_t mic_samples_passthrough[appconfAUDIO_PIPELINE_CHANNELS][appconfAUDIO_PIPELINE_FRAME_ADVANCE];

    /* Below is additional context needed by other stages on a per frame basis */
    float_s32_t vnr_pred_flag;
    float_s32_t max_ref_energy;
    float_s32_t aec_corr_factor;
    int32_t ref_active_flag;
    uint32_t ref_probe_peak[2];
    uint32_t ref_probe_num_x_channels;
    uint32_t ref_probe_direct_activity;
    uint32_t ref_probe_aec_shared_activity;
    ffva_aec_adaptation_t adaptation_probe;
    uint32_t adaptation_probe_valid;
    uint32_t aec_residual_power;
    uint32_t aec_input_power[2];
    uint32_t aec_residual_power_per_mic[2];
    uint32_t aec_subtraction_power[2];
    int32_t adec_applied_delay_samples;
    int32_t adec_measured_delay_samples;
    uint32_t adec_mode;
    uint32_t adec_estimator_enabled;
    uint32_t adec_force_cycle_pending;
    int32_t debug_current_peak_phase;
    uint32_t debug_current_peak_ratio_x100;
    int32_t debug_accepted_peak_phase;
    uint32_t debug_accepted_peak_ratio_x100;
    uint32_t debug_adaptation_mode;
    uint32_t debug_core_bypass;
    int32_t debug_shadow_flag;
    uint32_t debug_mu_active;
    int32_t debug_mu_coh_timer;
} frame_data_t;

typedef struct ic_stage_ctx {
    ic_state_t DWORD_ALIGNED state;
} ic_stage_ctx_t;

typedef struct ns_stage_ctx {
    ns_state_t DWORD_ALIGNED state;
} ns_stage_ctx_t;

typedef struct agc_stage_ctx {
    agc_meta_data_t md;
    agc_state_t DWORD_ALIGNED state;
} agc_stage_ctx_t;

#endif /* AUDIO_PIPELINE_DSP_H_ */
