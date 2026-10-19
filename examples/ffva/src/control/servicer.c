// Copyright 2024-2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#define DEBUG_UNIT SERVICER_TASK
#ifndef DEBUG_PRINT_ENABLE_SERVICER_TASK
    #define DEBUG_PRINT_ENABLE_SERVICER_TASK 0
#endif
#include "debug_print.h"
#include <stdio.h>
#include <platform.h>
#include "platform/platform_conf.h"
#include "device_control_i2c.h"
#include "servicer.h"
#include "dfu_servicer.h"
#include "aec_telemetry.h"
#include "app_conf.h"

static void store_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = value >> 24;
    dst[1] = value >> 16;
    dst[2] = value >> 8;
    dst[3] = value;
}

// XVF3610-compatible values are big-endian and GET_ERLE contains two
// mantissa/exponent pairs. FFVA reports PCM16 mean-square power with exponent 0.
static control_ret_t aec_telemetry_read_cmd(control_cmd_t cmd, uint8_t *payload)
{
    ffva_aec_telemetry_t snapshot;
    ffva_aec_telemetry_read(&snapshot);
    switch (CONTROL_CMD_CLEAR_READ(cmd)) {
    case FFVA_AEC_GET_BYPASS:
        store_be32(payload, appconfAUDIO_PIPELINE_SKIP_AEC || snapshot.frame_counter == 0);
        return CONTROL_SUCCESS;
    case FFVA_AEC_GET_ERLE_CH0:
        store_be32(payload, snapshot.microphone_power);
        store_be32(payload + 4, 0);
        store_be32(payload + 8, snapshot.residual_power);
        store_be32(payload + 12, 0);
        return CONTROL_SUCCESS;
    case FFVA_AEC_GET_FRAME_ADVANCE:
        store_be32(payload, appconfAUDIO_PIPELINE_FRAME_ADVANCE);
        return CONTROL_SUCCESS;
    case FFVA_AEC_GET_Y_CHANNELS:
    case FFVA_AEC_GET_X_CHANNELS:
        store_be32(payload, 2);
        return CONTROL_SUCCESS;
    case FFVA_AEC_GET_LEVELS:
        store_be32(payload, snapshot.reference_power);
        store_be32(payload + 4, snapshot.microphone_power);
        store_be32(payload + 8, snapshot.residual_power);
        store_be32(payload + 12, snapshot.output_power);
        store_be32(payload + 16, snapshot.reference_active);
        store_be32(payload + 20, snapshot.frame_counter);
        return CONTROL_SUCCESS;
    case FFVA_AEC_GET_ADEC_STATE: {
        ffva_adec_telemetry_t adec;
        ffva_adec_telemetry_read(&adec);
        if (adec.frame_counter == 0) {
            return CONTROL_BAD_COMMAND; // Fixed-delay pipeline has no ADEC state.
        }
        store_be32(payload, (uint32_t)adec.applied_delay_samples);
        store_be32(payload + 4, (uint32_t)adec.measured_delay_samples);
        store_be32(payload + 8, adec.mode);
        store_be32(payload + 12, adec.estimator_enabled);
        store_be32(payload + 16, adec.force_cycle_pending);
        store_be32(payload + 20, adec.reference_active);
        store_be32(payload + 24, adec.correction_count);
        store_be32(payload + 28, adec.frame_counter);
        return CONTROL_SUCCESS;
    }
    case FFVA_AEC_GET_DEBUG: {
        ffva_aec_debug_t debug;
        ffva_aec_debug_read(&debug);
        if (debug.frame_counter == 0) {
            return CONTROL_BAD_COMMAND;
        }
        store_be32(payload, (uint32_t)debug.accepted_peak_phase);
        store_be32(payload + 4, debug.accepted_peak_ratio_x100);
        store_be32(payload + 8, (uint32_t)debug.current_peak_phase);
        store_be32(payload + 12, debug.current_peak_ratio_x100);
        store_be32(payload + 16, debug.reference_active_frames);
        store_be32(payload + 20, debug.adaptation_active_frames);
        store_be32(payload + 24, debug.adaptation_mode);
        store_be32(payload + 28, debug.core_bypass);
        store_be32(payload + 32, (uint32_t)debug.shadow_flag);
        store_be32(payload + 36, (uint32_t)debug.coherence_freeze_timer);
        store_be32(payload + 40, debug.frame_counter);
        return CONTROL_SUCCESS;
    }
    case FFVA_AEC_GET_SUBTRACTION: {
        ffva_aec_subtraction_t subtraction;
        ffva_aec_subtraction_read(&subtraction);
        if (subtraction.frame_counter == 0) {
            return CONTROL_BAD_COMMAND;
        }
        store_be32(payload, subtraction.input_power[0]);
        store_be32(payload + 4, subtraction.subtraction_power[0]);
        store_be32(payload + 8, subtraction.residual_power[0]);
        store_be32(payload + 12, subtraction.input_power[1]);
        store_be32(payload + 16, subtraction.subtraction_power[1]);
        store_be32(payload + 20, subtraction.residual_power[1]);
        store_be32(payload + 24, subtraction.frame_counter);
        return CONTROL_SUCCESS;
    }
    case FFVA_AEC_GET_REFERENCE_PROBE: {
        ffva_aec_reference_probe_t probe;
        ffva_aec_reference_probe_read(&probe);
        if (probe.frame_counter == 0) {
            return CONTROL_BAD_COMMAND;
        }
        store_be32(payload, probe.frame_counter);
        store_be32(payload + 4, probe.tile1_peak[0]);
        store_be32(payload + 8, probe.tile1_peak[1]);
        store_be32(payload + 12, probe.num_x_channels);
        store_be32(payload + 16, probe.direct_activity);
        store_be32(payload + 20, probe.aec_shared_activity);
        store_be32(payload + 24, probe.propagated_activity);
        store_be32(payload + 28, probe.tile0_peak[0]);
        store_be32(payload + 32, probe.tile0_peak[1]);
        store_be32(payload + 36, probe.tile0_activity);
        return CONTROL_SUCCESS;
    }
    case FFVA_AEC_GET_ADAPTATION: {
        ffva_aec_adaptation_t probe;
        ffva_aec_adaptation_read(&probe);
        if (probe.frame_counter == 0) return CONTROL_BAD_COMMAND;
        uint8_t *out = payload;
        store_be32(out, probe.frame_counter);
        out += 4;
        for (size_t ch = 0; ch < 2; ++ch) {
#define STORE_ADAPT(value) do { store_be32(out, (uint32_t)(value)); out += 4; } while (0)
            STORE_ADAPT(probe.coherence_x1000[ch]);
            STORE_ADAPT(probe.slow_coherence_x1000[ch]);
            STORE_ADAPT(probe.erle_x1000[ch]);
            STORE_ADAPT(probe.moving_erle_x1000[ch]);
            STORE_ADAPT(probe.main_mu_x1000[ch][0]);
            STORE_ADAPT(probe.main_mu_x1000[ch][1]);
            STORE_ADAPT(probe.freeze_timer[ch]);
            STORE_ADAPT(probe.shadow_flag[ch]);
            STORE_ADAPT(probe.shadow_better_count[ch]);
            STORE_ADAPT(probe.shadow_reset_count[ch]);
            STORE_ADAPT(probe.shadow_error_ratio_x1000[ch]);
            STORE_ADAPT(probe.main_active_frames[ch]);
            STORE_ADAPT(probe.freeze_active_frames[ch]);
            STORE_ADAPT(probe.shadow_copy_frames[ch]);
#undef STORE_ADAPT
        }
        return CONTROL_SUCCESS;
    }
    default:
        return CONTROL_BAD_COMMAND;
    }
}

#if appconfI2C_DFU_ENABLED && ON_TILE(I2C_CTRL_TILE_NO)
static device_control_t device_control_i2c_ctx_s;
device_control_t *device_control_i2c_ctx = (device_control_t *) &device_control_i2c_ctx_s;
device_control_t *device_control_ctxs[APP_CONTROL_TRANSPORT_COUNT] = {
        (device_control_t *) &device_control_i2c_ctx_s,
};
#endif

//-----------------Servicer read write callback functions-----------------------//
DEVICE_CONTROL_CALLBACK_ATTR
control_ret_t read_cmd(control_resid_t resid, control_cmd_t cmd, uint8_t *payload, size_t payload_len, void *app_data)
{
    control_ret_t ret = CONTROL_SUCCESS;
    servicer_t *servicer = (servicer_t*)app_data;

    // For read commands, payload[0] is reserved from status. So payload_len is one more than the payload_len stored in the resource command map
    payload_len -= 1;
    uint8_t *payload_ptr = &payload[1]; //Excluding the status byte, which is updated later.

    debug_printf("Servicer ID %d on tile %d received READ command %02x for resid %02x\n\t",servicer->id, THIS_XCORE_TILE, cmd, resid);
    debug_printf("The command is requesting %d bytes\n\t", payload_len);


    control_resource_info_t *current_res_info = get_res_info(resid, servicer);
    xassert(current_res_info != NULL); // This should never happen
    control_cmd_info_t *current_cmd_info;
    ret = validate_cmd(&current_cmd_info, current_res_info, cmd, payload_ptr, payload_len);
    if(ret != CONTROL_SUCCESS)
    {
        payload[0] = ret; // Update status in byte 0
        return ret;
    }
    // Both DFU (resource 0xF0) and read-only AEC telemetry (0x11) are
    // registered with this servicer. Dispatch either validated resource.
    ret = servicer_read_cmd(current_res_info, cmd, payload_ptr, payload_len);
    payload[0] = ret;
    return ret;
}

DEVICE_CONTROL_CALLBACK_ATTR
control_ret_t write_cmd(control_resid_t resid, control_cmd_t cmd, const uint8_t *payload, size_t payload_len, void *app_data)
{
    control_ret_t ret = CONTROL_SUCCESS;
    servicer_t *servicer = (servicer_t*)app_data;
    //debug_printf("Device control WRITE. Servicer ID %d\n\t", servicer->id);

    debug_printf("Servicer ID %d on tile %d received WRITE command %02x for resid %02x\n\t", servicer->id, THIS_XCORE_TILE, cmd, resid);
    debug_printf("The command has %d bytes\n\t", payload_len);

    control_resource_info_t *current_res_info = get_res_info(resid, servicer);
    xassert(current_res_info != NULL);
    control_cmd_info_t *current_cmd_info;
    ret = validate_cmd(&current_cmd_info, current_res_info, cmd, payload, payload_len);
    if(ret != CONTROL_SUCCESS)
    {
        return ret;
    }
    // Check if command is for the servicer itself
    if(current_res_info->resource == servicer->res_info[0].resource)
    {
        ret = servicer_write_cmd(current_res_info, cmd, payload, payload_len);
        return ret;
    }
    return CONTROL_ERROR;
}

// Initialise packet payload pointers to point to valid memory.


//-----------------Servicer helper functions-----------------------//
// Return a pointer to the control_cmd_info_t structure for a given command ID. Return NULL if command not found in the
// command map for the resource.
control_cmd_info_t* get_cmd_info(uint8_t cmd_id, const control_resource_info_t *res_info)
{
    for(int i=0; i<res_info->command_map.num_commands; i++)
    {
        if(res_info->command_map.commands[i].cmd_id == cmd_id)
        {
            return &res_info->command_map.commands[i];
        }
    }
    return NULL;
}

// Return a pointer to the servicer's control_resource_info_t structure for a given resource ID.
// Return NULL if the resource ID is not found in the list of resources serviced by the servicer.
control_resource_info_t* get_res_info(control_resid_t resource, const servicer_t *servicer)
{
    for(int res=0; res<servicer->num_resources; res++)
    {
        // Get the cmd_info for the current command
        if(servicer->res_info[res].resource == resource)
        {
            return &servicer->res_info[res];
        }
    }
    return NULL;
}

// Validate the command from the host against that commands information in the stored command_map
control_ret_t validate_cmd(control_cmd_info_t **cmd_info,
                            control_resource_info_t *res_info,
                            control_cmd_t cmd,
                            const uint8_t *payload,
                            size_t payload_len)
{
    control_ret_t ret = CONTROL_SUCCESS;
    *cmd_info = get_cmd_info(CONTROL_CMD_CLEAR_READ(cmd), res_info);
    if(*cmd_info == NULL)
    {
        return SERVICER_WRONG_COMMAND_ID;
    }

    // Validate non special command length
    // Don't do payload check for special commands since for the last filter chunk, host might request less than the payload length specified in the cmd_map

    if(payload_len != (*cmd_info)->bytes_per_val * (*cmd_info)->num_vals)
    {
        return SERVICER_WRONG_COMMAND_LEN;
    }
    // Payload validation happens either on the host or in the specific command handlers. No generic payload validation is done in the servicer.

    return ret;
}

// Process write commands directed to the servicer resource
control_ret_t servicer_write_cmd(control_resource_info_t *res_info, control_cmd_t cmd, const uint8_t *payload, size_t payload_len)
{
    // Handle write commands directed to a servicer resource
    switch(res_info->resource)
    {
        case DFU_CONTROLLER_SERVICER_RESID:
            return dfu_servicer_write_cmd(res_info, cmd, payload, payload_len);
        break;
        case FFVA_AEC_RESOURCE_ID:
            return CONTROL_BAD_COMMAND; // Telemetry resource is strictly read-only.
    }
    return CONTROL_BAD_COMMAND;
}

// Process read commands directed to the servicer resource
control_ret_t servicer_read_cmd(control_resource_info_t *res_info, control_cmd_t cmd, uint8_t *payload, size_t payload_len)
{
    control_ret_t ret = CONTROL_SUCCESS;
     // Handle read commands directed to a servicer resource
    switch(res_info->resource)
    {
        case DFU_CONTROLLER_SERVICER_RESID:
            ret = dfu_servicer_read_cmd(res_info, cmd, payload, payload_len);
            break;
        case FFVA_AEC_RESOURCE_ID:
            ret = aec_telemetry_read_cmd(cmd, payload);
            break;
    }
    return ret;
}
