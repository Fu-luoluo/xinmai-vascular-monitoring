#ifndef __CLOUD_REPORT_H
#define __CLOUD_REPORT_H

#include "measure_ctrl.h"
#include "pwv.h"

#ifdef __cplusplus
extern "C" {
#endif

void CloudReport_SessionEnd(measureEndReason_t reason, const pwv_session_t *snap);

#ifdef __cplusplus
}
#endif

#endif /* __CLOUD_REPORT_H */
