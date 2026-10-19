// Read-only FFVA audio-level telemetry on the existing I2C control bus.
#pragma once

#include <stdint.h>

// Match the read-only XVF3610 AEC resource and its established GET commands.
#define FFVA_AEC_RESOURCE_ID 0x11
#define FFVA_AEC_GET_BYPASS 0x00
#define FFVA_AEC_GET_ERLE_CH0 0x08
#define FFVA_AEC_GET_FRAME_ADVANCE 0x0A
#define FFVA_AEC_GET_Y_CHANNELS 0x0B
#define FFVA_AEC_GET_X_CHANNELS 0x0C
// FFVA extension: four PCM16 powers, reference-active flag, frame counter.
#define FFVA_AEC_GET_LEVELS 0x11
#define FFVA_AEC_GET_ADEC_STATE 0x12
#define FFVA_AEC_GET_DEBUG 0x13
#define FFVA_AEC_GET_SUBTRACTION 0x14
#define FFVA_AEC_GET_REFERENCE_PROBE 0x15
#define FFVA_AEC_GET_ADAPTATION 0x16

typedef struct {
    uint32_t reference_power;
    uint32_t microphone_power;
    uint32_t residual_power;
    uint32_t output_power;
    uint32_t reference_active;
    uint32_t frame_counter;
} ffva_aec_telemetry_t;

// Signed delays are in 16 kHz samples. mode: 0=normal AEC, 1=delay estimation.
// This is an FFVA extension, not a stock XVF3610 command.
typedef struct {
    int32_t applied_delay_samples;
    int32_t measured_delay_samples;
    uint32_t mode;
    uint32_t estimator_enabled;
    uint32_t force_cycle_pending;
    uint32_t reference_active;
    uint32_t correction_count;
    uint32_t frame_counter;
} ffva_adec_telemetry_t;

// Diagnostic counters cover the same ten 15-ms frames as GET_LEVELS.
typedef struct {
    int32_t accepted_peak_phase;
    uint32_t accepted_peak_ratio_x100;
    int32_t current_peak_phase;
    uint32_t current_peak_ratio_x100;
    uint32_t reference_active_frames;
    uint32_t adaptation_active_frames;
    uint32_t adaptation_mode;
    uint32_t core_bypass;
    int32_t shadow_flag;
    int32_t coherence_freeze_timer;
    uint32_t frame_counter;
} ffva_aec_debug_t;

// Per-microphone powers immediately around Stage 1. The subtraction is the
// sample-wise difference between AEC input and residual after compensating
// for the AEC's 32-sample overlap-add output lag. It is the signal removed
// from the microphone, before IC/NS/AGC.
typedef struct {
    uint32_t input_power[2];
    uint32_t residual_power[2];
    uint32_t subtraction_power[2];
    uint32_t frame_counter;
} ffva_aec_subtraction_t;

// One identified 15-ms frame, measured at both sides of the tile boundary.
// Peaks are absolute PCM16 counts from the two digital speaker-reference slots.
typedef struct {
    uint32_t frame_counter;
    uint32_t tile1_peak[2];
    uint32_t num_x_channels;
    uint32_t direct_activity;
    uint32_t aec_shared_activity;
    uint32_t propagated_activity;
    uint32_t tile0_peak[2];
    uint32_t tile0_activity;
} ffva_aec_reference_probe_t;

// Last frame of a 10-frame window plus per-window activity counters. Values
// suffixed x1000 are dimensionless quantities scaled by 1000; -1 means the
// shadow/main error ratio could not be computed. Channel indices are mic 0/1.
typedef struct {
    uint32_t frame_counter;
    int32_t coherence_x1000[2];
    int32_t slow_coherence_x1000[2];
    int32_t erle_x1000[2];
    int32_t moving_erle_x1000[2];
    int32_t main_mu_x1000[2][2];
    int32_t freeze_timer[2];
    int32_t shadow_flag[2];
    int32_t shadow_better_count[2];
    int32_t shadow_reset_count[2];
    int32_t shadow_error_ratio_x1000[2];
    uint32_t main_active_frames[2];
    uint32_t freeze_active_frames[2];
    uint32_t shadow_copy_frames[2];
} ffva_aec_adaptation_t;

// Powers are mean-square PCM16 values for one 240-sample (15 ms) frame.
void ffva_aec_telemetry_update(const ffva_aec_telemetry_t *snapshot);
void ffva_aec_telemetry_read(ffva_aec_telemetry_t *snapshot);
void ffva_adec_telemetry_update(const ffva_adec_telemetry_t *snapshot);
void ffva_adec_telemetry_read(ffva_adec_telemetry_t *snapshot);
void ffva_aec_debug_update(const ffva_aec_debug_t *snapshot);
void ffva_aec_debug_read(ffva_aec_debug_t *snapshot);
void ffva_aec_subtraction_update(const ffva_aec_subtraction_t *snapshot);
void ffva_aec_subtraction_read(ffva_aec_subtraction_t *snapshot);
void ffva_aec_reference_probe_update(const ffva_aec_reference_probe_t *snapshot);
void ffva_aec_reference_probe_read(ffva_aec_reference_probe_t *snapshot);
void ffva_aec_adaptation_update(const ffva_aec_adaptation_t *snapshot);
void ffva_aec_adaptation_read(ffva_aec_adaptation_t *snapshot);
