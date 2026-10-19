// Copyright 2026 XMOS LIMITED.
// Read-only snapshot shared by the tile-0 audio pipeline and I2C servicer.
#include "aec_telemetry.h"

#include "FreeRTOS.h"
#include "task.h"

static ffva_aec_telemetry_t latest;
static ffva_adec_telemetry_t adec_latest;
static ffva_aec_debug_t debug_latest;
static ffva_aec_subtraction_t subtraction_latest;
static ffva_aec_reference_probe_t reference_probe_latest;
static ffva_aec_adaptation_t adaptation_latest;

void ffva_aec_telemetry_update(const ffva_aec_telemetry_t *snapshot)
{
    taskENTER_CRITICAL();
    latest = *snapshot;
    taskEXIT_CRITICAL();
}

void ffva_aec_telemetry_read(ffva_aec_telemetry_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = latest;
    taskEXIT_CRITICAL();
}

void ffva_adec_telemetry_update(const ffva_adec_telemetry_t *snapshot)
{
    taskENTER_CRITICAL();
    adec_latest = *snapshot;
    taskEXIT_CRITICAL();
}

void ffva_adec_telemetry_read(ffva_adec_telemetry_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = adec_latest;
    taskEXIT_CRITICAL();
}

void ffva_aec_debug_update(const ffva_aec_debug_t *snapshot)
{
    taskENTER_CRITICAL();
    debug_latest = *snapshot;
    taskEXIT_CRITICAL();
}

void ffva_aec_debug_read(ffva_aec_debug_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = debug_latest;
    taskEXIT_CRITICAL();
}

void ffva_aec_subtraction_update(const ffva_aec_subtraction_t *snapshot)
{
    taskENTER_CRITICAL();
    subtraction_latest = *snapshot;
    taskEXIT_CRITICAL();
}

void ffva_aec_subtraction_read(ffva_aec_subtraction_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = subtraction_latest;
    taskEXIT_CRITICAL();
}

void ffva_aec_reference_probe_update(const ffva_aec_reference_probe_t *snapshot)
{
    taskENTER_CRITICAL();
    reference_probe_latest = *snapshot;
    taskEXIT_CRITICAL();
}

void ffva_aec_reference_probe_read(ffva_aec_reference_probe_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = reference_probe_latest;
    taskEXIT_CRITICAL();
}

void ffva_aec_adaptation_update(const ffva_aec_adaptation_t *snapshot)
{
    taskENTER_CRITICAL();
    adaptation_latest = *snapshot;
    taskEXIT_CRITICAL();
}

void ffva_aec_adaptation_read(ffva_aec_adaptation_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = adaptation_latest;
    taskEXIT_CRITICAL();
}
