#include "host_link.h"
#include "ota.h"
#include "ota_store.h"
#include "ota_uart.h"
#include "bsp_uart.h"
#include "CONFIG.h"
#include "CH58x_common.h"
#include "flash_layout.h"
#include "flash_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_LINK_Q_DEPTH              4U
#define HOST_LINK_Q_LINE_MAX           192U
#define HOST_LINK_WIFI_LIST_MAX        768U
#define HOST_LINK_WIFI_STATUS_MAX      192U
#define HOST_LINK_TX_LINE_MAX          256U
#define HOST_LINK_MAX_POLL_PER_TICK      8U
#define HOST_LINK_MAX_DISPATCH_PER_TICK  6U
#define HOST_LINK_MAX_OTA_BYTES_PER_TICK 512U
#define OTA_BINARY_TIMEOUT_MS        30000U
#define OTA_FRAME_TIMEOUT_MS           200U
#define OTA_WIRE_DELIM                 0x7EU
#define OTA_WIRE_ESCAPE                0x7DU
#define OTA_WIRE_ESCAPE_XOR            0x20U
#define HOST_LINK_TASK_EVT              0x0001U
#define HOST_LINK_TASK_PERIOD_MS        1U

static HostLinkEventFn s_event_fn = NULL;
static uint8_t         s_ota_active = 0U;
static uint32_t        s_ota_last_tick = 0U;
static uint32_t        s_ota_frame_last_tick = 0U;
static uint8_t         s_ota_trace_lines = 0U;
static uint8_t         s_ota_wire_buf[IAP_LEN + 1U];
static uint16_t        s_ota_wire_len = 0U;
static uint8_t         s_ota_wire_escape = 0U;
static char            s_ota_hex_buf[(64U * 2U) + 1U];
static uint8_t         s_ota_data_buf[64U];
static uint32_t        s_rx_line_count = 0U;
static uint32_t        s_rx_wifi_list_count = 0U;
static uint32_t        s_rx_wifi_status_count = 0U;

static char            s_poll_line_buf[BSP_UART1_RX_LINE_MAX];
static char            s_wifi_list_buf[HOST_LINK_WIFI_LIST_MAX];
static uint8_t         s_wifi_list_pending = 0U;
static char            s_wifi_status_buf[HOST_LINK_WIFI_STATUS_MAX];
static uint8_t         s_wifi_status_pending = 0U;

static char            s_q_lines[HOST_LINK_Q_DEPTH][HOST_LINK_Q_LINE_MAX];
static uint8_t         s_q_head = 0U;
static uint8_t         s_q_tail = 0U;
static uint8_t         s_q_count = 0U;
static uint8_t         s_task_id = 0xFFU;

static void host_link_reset_ota_wire(void);
static uint16_t host_link_process_event(uint8_t task_id, uint16_t events);

static void host_link_send_line(const char *json_body)
{
    char line[HOST_LINK_TX_LINE_MAX];

    if(json_body == NULL || s_ota_active) {
        return;
    }
    snprintf(line, sizeof(line), "%s\r\n", json_body);
    BSP_UART1_SendString(line);
}

static void host_link_send_ota_ack(const char *phase, uint8_t ok)
{
    char line[96];

    snprintf(line, sizeof(line),
             "{\"event\":\"ota_ack\",\"phase\":\"%s\",\"ok\":%u}\r\n",
             phase, (unsigned)ok);
    BSP_UART1_SendString(line);
}

static uint8_t host_link_json_find_uint(const char *json, const char *key,
                                        uint32_t *out)
{
    char pattern[32];
    const char *p;
    char *end;

    if(json == NULL || key == NULL || out == NULL) {
        return 0U;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if(p == NULL) {
        return 0U;
    }
    p += strlen(pattern);
    *out = (uint32_t)strtoul(p, &end, 10);
    return (end != p) ? 1U : 0U;
}

static uint8_t host_link_json_find_cmd(const char *json, char *cmd, size_t cmd_len)
{
    const char *p;
    const char *start;
    size_t n;

    if(json == NULL || cmd == NULL || cmd_len == 0U) {
        return 0U;
    }
    p = strstr(json, "\"cmd\"");
    if(p == NULL || (p = strchr(p, ':')) == NULL ||
       (p = strchr(p, '"')) == NULL) {
        return 0U;
    }
    start = ++p;
    p = strchr(start, '"');
    if(p == NULL) {
        return 0U;
    }
    n = (size_t)(p - start);
    if(n >= cmd_len) {
        return 0U;
    }
    memcpy(cmd, start, n);
    cmd[n] = '\0';
    return 1U;
}

static uint8_t host_link_json_find_string(const char *json, const char *key,
                                          char *out, size_t out_len)
{
    char pattern[32];
    const char *start;
    const char *end;
    size_t len;

    if(json == NULL || key == NULL || out == NULL || out_len == 0U) {
        return 0U;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    start = strstr(json, pattern);
    if(start == NULL) {
        return 0U;
    }
    start += strlen(pattern);
    end = strchr(start, '"');
    if(end == NULL) {
        return 0U;
    }
    len = (size_t)(end - start);
    if(len + 1U > out_len) {
        return 0U;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1U;
}

static uint8_t host_link_hex_nibble(char c, uint8_t *out)
{
    if(c >= '0' && c <= '9') {
        *out = (uint8_t)(c - '0');
        return 1U;
    }
    if(c >= 'a' && c <= 'f') {
        *out = (uint8_t)(c - 'a' + 10);
        return 1U;
    }
    if(c >= 'A' && c <= 'F') {
        *out = (uint8_t)(c - 'A' + 10);
        return 1U;
    }
    return 0U;
}

static void host_link_handle_ota_data(const char *json)
{
    uint32_t offset;
    size_t hex_len;
    size_t i;

    if(!s_ota_active || !host_link_json_find_uint(json, "off", &offset) ||
       !host_link_json_find_string(json, "data", s_ota_hex_buf,
                                   sizeof(s_ota_hex_buf))) {
        PRINT("OTA data reject header\r\n");
        host_link_send_ota_ack("data", 0U);
        return;
    }
    hex_len = strlen(s_ota_hex_buf);
    if(hex_len == 0U || (hex_len & 1U) != 0U ||
       hex_len > (sizeof(s_ota_data_buf) * 2U)) {
        PRINT("OTA data reject length=%u\r\n", (unsigned)hex_len);
        host_link_send_ota_ack("data", 0U);
        return;
    }
    for(i = 0U; i < (hex_len / 2U); i++) {
        uint8_t hi;
        uint8_t lo;
        if(!host_link_hex_nibble(s_ota_hex_buf[i * 2U], &hi) ||
           !host_link_hex_nibble(s_ota_hex_buf[(i * 2U) + 1U], &lo)) {
            PRINT("OTA data reject hex off=%lu\r\n", (unsigned long)offset);
            host_link_send_ota_ack("data", 0U);
            return;
        }
        s_ota_data_buf[i] = (uint8_t)((hi << 4) | lo);
    }

    if((offset % W25Q_SECTOR_SIZE) == 0U &&
       FlashStore_EraseSector(FLASH_PART_OTA_IMAGE_BASE + offset) != 0U) {
        PRINT("OTA data erase reject off=%lu\r\n", (unsigned long)offset);
        host_link_send_ota_ack("data", 0U);
        return;
    }
    if(OtaStore_WriteChunk(offset, s_ota_data_buf, (uint32_t)(hex_len / 2U)) != 0U) {
        PRINT("OTA data write reject off=%lu len=%u expect=%lu\r\n",
              (unsigned long)offset, (unsigned)(hex_len / 2U),
              (unsigned long)OtaStore_GetBytesWritten());
        host_link_send_ota_ack("data", 0U);
        return;
    }
    WWDG_SetCounter(0);
    host_link_send_ota_ack("data", 1U);
}

static void host_link_finish_ota(void)
{
    if(!s_ota_active) {
        host_link_send_ota_ack("finish", 0U);
        return;
    }
    BSP_UART1_SetOtaBinaryMode(0U);
    s_ota_active = 0U;
    if(OtaUart_HasError() || OtaStore_ValidateFinish() != 0U) {
        host_link_send_ota_ack("finish", 0U);
        return;
    }
    /* Finish must be visible to PC/ESP before CommitAndReset never returns. */
    host_link_send_ota_ack("finish", 1U);
    mDelaymS(20);
    (void)OtaStore_CommitAndReset();
}

static void host_link_abort_ota(void)
{
    BSP_UART1_SetOtaBinaryMode(0U);
    s_ota_active = 0U;
    (void)OtaStore_Abort();
    host_link_send_ota_ack("abort", 1U);
}

static void host_link_reset_ota_wire(void)
{
    s_ota_wire_len = 0U;
    s_ota_wire_escape = 0U;
    OtaUart_Reset();
}

static void host_link_handle_ota_wire_frame(void)
{
    uint8_t checksum = 0U;
    uint8_t frame_ok;
    uint16_t i;

    if(s_ota_wire_len < 3U) {
        host_link_send_ota_ack("frame", 0U);
        host_link_reset_ota_wire();
        return;
    }
    for(i = 0U; i + 1U < s_ota_wire_len; i++) {
        checksum ^= s_ota_wire_buf[i];
    }
    if(checksum != s_ota_wire_buf[s_ota_wire_len - 1U]) {
        PRINT("OTA wire checksum reject\r\n");
        host_link_send_ota_ack("frame", 0U);
        host_link_reset_ota_wire();
        return;
    }

    OtaUart_Reset();
    for(i = 0U; i + 1U < s_ota_wire_len; i++) {
        OtaUart_FeedByte(s_ota_wire_buf[i]);
    }
    if(!OtaUart_TakeFrameResult(&frame_ok)) {
        PRINT("OTA wire payload reject\r\n");
        host_link_send_ota_ack("frame", 0U);
        host_link_reset_ota_wire();
        return;
    }

    host_link_send_ota_ack("frame", frame_ok);
    if(!frame_ok) {
        host_link_reset_ota_wire();
        return;
    }
    if(OtaUart_HasAbort()) {
        host_link_abort_ota();
    } else if(OtaUart_HasEnd()) {
        host_link_finish_ota();
    }
    host_link_reset_ota_wire();
}

static void host_link_handle_ota_cmd(const char *json)
{
    char cmd[24];
    uint32_t size = 0U;
    uint32_t crc = 0U;
    uint32_t version = 1U;

    if(!host_link_json_find_cmd(json, cmd, sizeof(cmd))) {
        return;
    }

    if(strcmp(cmd, "ota_begin") == 0) {
        if(!host_link_json_find_uint(json, "size", &size) ||
           !host_link_json_find_uint(json, "crc", &crc) ||
           size == 0U || size > FLASH_PART_OTA_IMAGE_MAX) {
            host_link_send_ota_ack("begin", 0U);
            return;
        }
        if(s_ota_active) {
            /* Gateway retries begin when its first ACK was lost. */
            host_link_send_ota_ack("begin", OtaStore_IsActive() ? 1U : 0U);
            return;
        }
        (void)host_link_json_find_uint(json, "ver", &version);
        if(OtaStore_Begin(size, crc, version) != 0U) {
            host_link_send_ota_ack("begin", 0U);
            return;
        }
        host_link_reset_ota_wire();
        /* PC UART validation uses line-mode hex blocks for reliable framing. */
        BSP_UART1_SetOtaBinaryMode(0U);
        s_ota_active = 1U;
        s_ota_last_tick = TMOS_GetSystemClock();
        s_ota_frame_last_tick = s_ota_last_tick;
        s_ota_trace_lines = 0U;
        host_link_send_ota_ack("begin", 1U);
        return;
    }

    if(strcmp(cmd, "ota_finish") == 0) {
        host_link_finish_ota();
        return;
    }

    if(strcmp(cmd, "ota_data") == 0) {
        host_link_handle_ota_data(json);
        return;
    }

    if(strcmp(cmd, "ota_abort") == 0) {
        host_link_abort_ota();
    }
}

static void host_link_poll_ota_binary(void)
{
    uint8_t b;
    uint16_t handled = 0U;

    if(!s_ota_active || !BSP_UART1_IsOtaBinaryMode()) {
        return;
    }
    while(handled < HOST_LINK_MAX_OTA_BYTES_PER_TICK &&
          BSP_UART1_PollByte(&b)) {
        handled++;
        WWDG_SetCounter(0);
        s_ota_last_tick = TMOS_GetSystemClock();
        s_ota_frame_last_tick = s_ota_last_tick;

        if(b == OTA_WIRE_DELIM) {
            if(s_ota_wire_len != 0U) {
                host_link_handle_ota_wire_frame();
                if(!s_ota_active) {
                    break;
                }
            }
            s_ota_wire_len = 0U;
            s_ota_wire_escape = 0U;
            continue;
        }
        if(b == OTA_WIRE_ESCAPE) {
            s_ota_wire_escape = 1U;
            continue;
        }
        if(s_ota_wire_escape) {
            b ^= OTA_WIRE_ESCAPE_XOR;
            s_ota_wire_escape = 0U;
        }
        if(s_ota_wire_len >= sizeof(s_ota_wire_buf)) {
            PRINT("OTA wire overflow\r\n");
            host_link_send_ota_ack("frame", 0U);
            host_link_reset_ota_wire();
            continue;
        }
        s_ota_wire_buf[s_ota_wire_len++] = b;
    }
    if(s_ota_active && (s_ota_wire_len != 0U || s_ota_wire_escape) &&
       (TMOS_GetSystemClock() - s_ota_frame_last_tick) >
       MS1_TO_SYSTEM_TIME(OTA_FRAME_TIMEOUT_MS)) {
        PRINT("OTA frame timeout\r\n");
        host_link_reset_ota_wire();
        host_link_send_ota_ack("frame", 0U);
        s_ota_frame_last_tick = TMOS_GetSystemClock();
    }
    if((TMOS_GetSystemClock() - s_ota_last_tick) >
       MS1_TO_SYSTEM_TIME(OTA_BINARY_TIMEOUT_MS)) {
        BSP_UART1_SetOtaBinaryMode(0U);
        s_ota_active = 0U;
        (void)OtaStore_Abort();
        host_link_send_ota_ack("timeout", 0U);
    }
}

static uint8_t host_link_is_wifi_status(const char *line)
{
    if(line == NULL) {
        return 0U;
    }
    return (strstr(line, "\"event\":\"wifi_status\"") != NULL) ? 1U : 0U;
}

static uint8_t host_link_store_wifi_status(const char *line)
{
    if(line == NULL) {
        return 0U;
    }
    strncpy(s_wifi_status_buf, line, HOST_LINK_WIFI_STATUS_MAX - 1U);
    s_wifi_status_buf[HOST_LINK_WIFI_STATUS_MAX - 1U] = '\0';
    s_wifi_status_pending = 1U;
    return 1U;
}

static uint8_t host_link_is_wifi_list(const char *line)
{
    size_t n;

    if(line == NULL) {
        return 0U;
    }
    if(strstr(line, "\"event\":\"wifi_list\"") != NULL) {
        return 1U;
    }
    if(strstr(line, "\"aps\":[") != NULL) {
        return 1U;
    }
    n = strlen(line);
    if(n > 180U &&
       strstr(line, "\"ssid\":\"") != NULL &&
       strstr(line, "\"rssi\":") != NULL &&
       strstr(line, "\"event\":\"wifi_status\"") == NULL &&
       strstr(line, "\"event\":\"wifi_ack\"") == NULL) {
        return 1U;
    }
    return 0U;
}

static uint8_t host_link_store_wifi_list(const char *line)
{
    if(line == NULL) {
        return 0U;
    }
    if(strcmp(s_wifi_list_buf, line) == 0) {
        return 0U;
    }
    strncpy(s_wifi_list_buf, line, HOST_LINK_WIFI_LIST_MAX - 1U);
    s_wifi_list_buf[HOST_LINK_WIFI_LIST_MAX - 1U] = '\0';
    s_wifi_list_pending = 1U;
    return 1U;
}

static uint8_t host_link_enqueue(const char *line)
{
    if(line == NULL) {
        return 0U;
    }
    if(s_q_count >= HOST_LINK_Q_DEPTH) {
        s_q_head = (uint8_t)((s_q_head + 1U) % HOST_LINK_Q_DEPTH);
        s_q_count--;
    }
    strncpy(s_q_lines[s_q_tail], line, HOST_LINK_Q_LINE_MAX - 1U);
    s_q_lines[s_q_tail][HOST_LINK_Q_LINE_MAX - 1U] = '\0';
    s_q_tail = (uint8_t)((s_q_tail + 1U) % HOST_LINK_Q_DEPTH);
    s_q_count++;
    return 1U;
}

static uint8_t host_link_dequeue(char *line, uint16_t line_len)
{
    if(line == NULL || line_len == 0U || s_q_count == 0U) {
        return 0U;
    }
    strncpy(line, s_q_lines[s_q_head], (size_t)line_len - 1U);
    line[line_len - 1U] = '\0';
    s_q_head = (uint8_t)((s_q_head + 1U) % HOST_LINK_Q_DEPTH);
    s_q_count--;
    return 1U;
}

void HostLink_Init(void)
{
    s_event_fn = NULL;
    s_q_head = 0U;
    s_q_tail = 0U;
    s_q_count = 0U;
    s_rx_line_count = 0U;
    s_rx_wifi_list_count = 0U;
    s_rx_wifi_status_count = 0U;
    s_wifi_list_pending = 0U;
    s_wifi_list_buf[0] = '\0';
    s_wifi_status_pending = 0U;
    s_wifi_status_buf[0] = '\0';
    s_ota_active = 0U;
    s_ota_last_tick = 0U;
    s_ota_frame_last_tick = 0U;
    host_link_reset_ota_wire();
    BSP_UART1_SetOtaBinaryMode(0U);
    s_task_id = 0xFFU;
}

void HostLink_SetEventFn(HostLinkEventFn fn)
{
    s_event_fn = fn;
}

void HostLink_Poll(void)
{
    char    *line = s_poll_line_buf;
    uint8_t  n = 0U;
    char    *json;

    host_link_poll_ota_binary();
    if(s_ota_active && BSP_UART1_IsOtaBinaryMode()) {
        return;
    }

    while(n < HOST_LINK_MAX_POLL_PER_TICK &&
          BSP_UART1_PollLine(line, sizeof(s_poll_line_buf))) {
        n++;
        s_rx_line_count++;
        json = strchr(line, '{');
        if(json == NULL) {
            continue;
        }
        if(strstr(json, "\"cmd\"") != NULL &&
           strstr(json, "\"ota_") != NULL) {
            if(s_ota_trace_lines < 4U) {
                PRINT("OTA RX line=%u len=%u drop=%lu\r\n",
                      (unsigned)s_ota_trace_lines,
                      (unsigned)strlen(json),
                      (unsigned long)BSP_UART1_GetRxDropCount());
                s_ota_trace_lines++;
            }
            host_link_handle_ota_cmd(json);
            continue;
        }
        if(host_link_is_wifi_list(json)) {
            if(host_link_store_wifi_list(json)) {
                s_rx_wifi_list_count++;
            }
            continue;
        }
        if(host_link_is_wifi_status(json)) {
            if(host_link_store_wifi_status(json)) {
                s_rx_wifi_status_count++;
            }
            continue;
        }
        (void)host_link_enqueue(json);
    }
}

void HostLink_DispatchPending(void)
{
    char    line[HOST_LINK_Q_LINE_MAX];
    uint8_t n = 0U;

    while(!s_ota_active && n < HOST_LINK_MAX_DISPATCH_PER_TICK &&
          host_link_dequeue(line, sizeof(line))) {
        n++;
        if(s_event_fn != NULL) {
            s_event_fn(line);
        }
    }
}

uint8_t HostLink_IsOtaActive(void)
{
    return s_ota_active;
}

void HostLink_ReportBoot(uint32_t version, uint8_t ota_status)
{
    char line[128];

    snprintf(line, sizeof(line),
             "{\"event\":\"boot_report\",\"ver\":%lu,\"ota_status\":%u}",
             (unsigned long)version, (unsigned)ota_status);
    host_link_send_line(line);
}

uint8_t HostLink_TakeWifiList(char *buf, uint16_t buf_len)
{
    if(buf == NULL || buf_len == 0U || !s_wifi_list_pending) {
        return 0U;
    }
    strncpy(buf, s_wifi_list_buf, (size_t)buf_len - 1U);
    buf[buf_len - 1U] = '\0';
    s_wifi_list_pending = 0U;
    return 1U;
}

uint8_t HostLink_TakeWifiStatus(char *buf, uint16_t buf_len)
{
    if(buf == NULL || buf_len == 0U || !s_wifi_status_pending) {
        return 0U;
    }
    strncpy(buf, s_wifi_status_buf, (size_t)buf_len - 1U);
    buf[buf_len - 1U] = '\0';
    s_wifi_status_pending = 0U;
    return 1U;
}

void HostLink_ApplyCachedWifiList(void)
{
}

void HostLink_TaskStart(void)
{
    if(s_task_id == 0xFFU) {
        s_task_id = TMOS_ProcessEventRegister(host_link_process_event);
    }
    tmos_start_task(s_task_id, HOST_LINK_TASK_EVT,
                    MS1_TO_SYSTEM_TIME(HOST_LINK_TASK_PERIOD_MS));
}

static uint16_t host_link_process_event(uint8_t task_id, uint16_t events)
{
    (void)task_id;

    if(events & HOST_LINK_TASK_EVT) {
        HostLink_Poll();
        tmos_start_task(s_task_id, HOST_LINK_TASK_EVT,
                        MS1_TO_SYSTEM_TIME(HOST_LINK_TASK_PERIOD_MS));
        return (uint16_t)(events ^ HOST_LINK_TASK_EVT);
    }
    return 0U;
}

uint32_t HostLink_GetRxLineCount(void)
{
    return s_rx_line_count;
}

uint32_t HostLink_GetWifiListCount(void)
{
    return s_rx_wifi_list_count;
}

uint32_t HostLink_GetWifiStatusCount(void)
{
    return s_rx_wifi_status_count;
}

uint32_t HostLink_GetRxDropCount(void)
{
    return BSP_UART1_GetRxDropCount();
}

void HostLink_CmdPing(void)
{
    host_link_send_line("{\"cmd\":\"ping\"}");
}

void HostLink_CmdWifiScan(void)
{
    host_link_send_line("{\"cmd\":\"wifi_scan\"}");
}

void HostLink_CmdWifiStatus(void)
{
    host_link_send_line("{\"cmd\":\"wifi_status\"}");
}

void HostLink_CmdWifiForget(void)
{
    host_link_send_line("{\"cmd\":\"wifi_forget\"}");
}

void HostLink_CmdWifiConnect(const char *ssid, const char *pass)
{
    char line[HOST_LINK_TX_LINE_MAX];
    const char *p = pass ? pass : "";

    if(ssid == NULL || ssid[0] == '\0') {
        return;
    }
    snprintf(line, sizeof(line),
             "{\"cmd\":\"wifi_connect\",\"ssid\":\"%s\",\"pass\":\"%s\"}",
             ssid, p);
    host_link_send_line(line);
}
