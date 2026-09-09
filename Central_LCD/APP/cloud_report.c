#include "cloud_report.h"
#include "host_link.h"
#include "bsp_uart.h"
#include <stdio.h>

static const char *cloud_reason_str(measureEndReason_t reason)
{
    switch(reason) {
        case MEASURE_END_FIRST_PWV:   return "first_pwv";
        case MEASURE_END_SIGNAL_LOST: return "signal_lost";
        case MEASURE_END_TIMEOUT:     return "complete";
        case MEASURE_END_PULSE_TARGET: return "pulse_target";
        default:                      return "unknown";
    }
}

void CloudReport_SessionEnd(measureEndReason_t reason, const pwv_session_t *snap)
{
    char line[192];
    int  n;

    if(snap == NULL) {
        return;
    }

    if(snap->pwv_valid) {
        n = snprintf(line, sizeof(line),
                     "{\"event\":\"session_end\",\"reason\":\"%s\","
                     "\"w_hr\":%u,\"w_spo2\":%u,\"f_hr\":%u,\"f_spo2\":%u,"
                     "\"pwv\":%.1f,\"pwv_valid\":1}\r\n",
                     cloud_reason_str(reason),
                     (unsigned)snap->w_hr, (unsigned)snap->w_spo2,
                     (unsigned)snap->f_hr, (unsigned)snap->f_spo2,
                     (double)snap->pwv);
    } else {
        n = snprintf(line, sizeof(line),
                     "{\"event\":\"session_end\",\"reason\":\"%s\","
                     "\"w_hr\":%u,\"w_spo2\":%u,\"f_hr\":%u,\"f_spo2\":%u,"
                     "\"pwv\":0.0,\"pwv_valid\":0}\r\n",
                     cloud_reason_str(reason),
                     (unsigned)snap->w_hr, (unsigned)snap->w_spo2,
                     (unsigned)snap->f_hr, (unsigned)snap->f_spo2);
    }

    if(n > 0 && n < (int)sizeof(line) && !HostLink_IsOtaActive()) {
        BSP_UART1_SendString(line);
    }
}
