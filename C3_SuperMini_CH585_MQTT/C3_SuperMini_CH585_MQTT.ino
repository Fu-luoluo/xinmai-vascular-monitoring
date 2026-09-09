/*
 * ESP32-C3-SuperMini — CH585 Central 云端网关（WiFi + MQTT + 屏上配网）
 *
 * UART1 57600 8N1：CH585 PA9(TX)->GPIO20(RX), PA8(RX)<-GPIO21(TX)
 * 板载 LED=GPIO8(低电平点亮)，外接 LED=GPIO5。
 * 注意：GPIO20/21 为 UART0 默认脚，须在 IDE 开启 "USB CDC On Boot = Enabled"，
 *       使 Serial 走 USB，避免与 Host_UART 抢占 GPIO20/21。
 * JSON 行 \r\n 结尾。CH585 命令含 "cmd"；主机测量数据含 "pulse_id"/"session_end"。
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Preferences.h>

// ==================== 调试开关 ====================
// 1=打开 USB 串口调试打印；0=关闭。需 IDE: USB CDC On Boot = Enabled
#define DBG_ENABLE 1

// ==================== 单独 WiFi 测试（不接 CH585）====================
// 1=仅测 SuperMini 能否连路由器；IDE 烧录后看 USB 串口。测完改回 0 恢复网关模式
#define WIFI_STANDALONE_TEST 0
#if WIFI_STANDALONE_TEST
  #define STANDALONE_WIFI_SSID  "YOUR_WIFI_SSID"
  #define STANDALONE_WIFI_PASS  "YOUR_WIFI_PASSWORD"
#endif
#if DBG_ENABLE
  #define DBG(...)    Serial.print(__VA_ARGS__)
  #define DBGLN(...)  Serial.println(__VA_ARGS__)
  #define DBGF(...)   Serial.printf(__VA_ARGS__)
#else
  #define DBG(...)    do{}while(0)
  #define DBGLN(...)  do{}while(0)
  #define DBGF(...)   do{}while(0)
#endif

/* 单独测试 */
const char* mqtt_server = "YOUR_MQTT_SERVER";
const int mqtt_port = 1883;
const char* mqtt_client_id = "device_ch585_wifi_c3";
const char* mqtt_username = "YOUR_MQTT_USERNAME";
const char* mqtt_password = "YOUR_MQTT_PASSWORD";
const char* topic_health = "/device/ch585/health";
const char* topic_data = "/device/ch585/data";
const char* topic_ota_cmd = "/device/ch585/ota/cmd";
const char* topic_ota_status = "/device/ch585/ota/status";

/* SuperMini 引脚 */
#define HOST_UART_RX  20
#define HOST_UART_TX  21
#define HOST_UART_BAUD 57600
#define LED_D4        5    /* 外接 LED：高电平点亮 */
#define LED_D5        8    /* 板载 LED：低电平点亮 */
#define LED_D5_ACTIVE_LOW  1

#define UART_FRAME_MAX          768
/*
 * 某些 AP 在首次认证超时后由 ESP-IDF 后台重试才能接通。
 * 不得以 30 秒的软件超时打断该重试。
 */
#define WIFI_CONNECT_TIMEOUT_MS 120000
#define WIFI_SCAN_MAX_APS       8
#define WIFI_SCAN_MS_PER_CHAN   150
#define WIFI_SCAN_TIMEOUT_MS    8000
#define WIFI_SCAN_ACTIVE_MIN_MS 100
#define WIFI_SCAN_AUTO_MS       8000
#define MQTT_RETRY_MS           5000
#define OTA_CHUNK_BYTES         32
#define OTA_MAX_IMAGE           (432UL * 1024UL)
#define OTA_BEGIN_ACK_MS        10000UL
#define OTA_DATA_ACK_MS         5000UL
#define OTA_FINISH_ACK_MS       15000UL
#define OTA_HTTP_TIMEOUT_MS     60000UL
#define OTA_BOOT_REPORT_MS       90000UL
/*
 * 当前热点扫描 RSSI 约 -26 dBm；降低发射功率仍有充足链路余量，
 * 同时减少认证帧发送瞬间的 3.3 V 电流脉冲。
 */
#define WIFI_TX_POWER           WIFI_POWER_8_5dBm

HardwareSerial Host_UART(1);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences prefs;

String uartFrame;
unsigned long last_health = 0;
unsigned long last_mqtt_try = 0;
unsigned long wifi_action_deadline = 0;
unsigned long wifi_reconnect_at = 0;
uint32_t wifi_reconnect_delay_ms = 5000;

String saved_ssid;
String saved_pass;
String pending_ssid;
String pending_pass;
static bool pending_save_on_success = false;

enum WifiRunState {
  WIFI_ST_NO_CONFIG = 0,
  WIFI_ST_CONNECTING,
  WIFI_ST_CONNECTED,
  WIFI_ST_FAILED,
  WIFI_ST_SCANNING,
  WIFI_ST_PROVISIONING
};
WifiRunState wifi_run_state = WIFI_ST_NO_CONFIG;
static bool wifi_scan_pending = false;
static bool wifi_scan_active = false;
static bool wifi_boot_connect_pending = false;
static bool wifi_host_cmd_seen = false;
static unsigned long wifi_last_begin_ms = 0;

#define WIFI_AP_CH_CACHE 16
static struct {
  String ssid;
  int ch;
} wifi_ap_ch_cache[WIFI_AP_CH_CACHE];
static int wifi_ap_ch_cache_n = 0;
static String s_last_wifi_list_json;
static unsigned long s_last_wifi_list_ms = 0;
static unsigned long s_last_auto_scan_ms = 0;
static bool g_uart_ota_lock = false;
static String g_ota_rx_assembly;
static String g_ota_ack_line;
static bool g_ota_ack_ready = false;
static bool g_ota_wait_boot = false;
static uint32_t g_ota_expected_ver = 0;
static unsigned long g_ota_boot_deadline = 0;
static uint32_t g_host_firmware_ver = 0;

static struct {
  bool pending;
  String url;
  uint32_t size;
  uint32_t crc;
  uint32_t ver;
} g_ota_job = { false, "", 0, 0, 0 };

static void processHostUart(void);

static bool wifiIsBusyConnect(void)
{
  return (wifi_run_state == WIFI_ST_CONNECTING ||
          wifi_run_state == WIFI_ST_PROVISIONING);
}

static void wifiDebugEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
  if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    DBGLN("[WiFi EVT] STA associated");
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    DBGLN("[WiFi EVT] STA got IP");
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    DBGF("[WiFi EVT] STA disconnected, reason=%u\n",
         (unsigned)info.wifi_sta_disconnected.reason);
  }
}

// ==================== JSON helpers ====================
static bool jsonExtractUInt(const String& s, const char* key, unsigned long& out) {
  String pat = String("\"") + key + "\":";
  int i = s.indexOf(pat);
  if (i < 0) return false;
  i += pat.length();
  out = strtoul(s.c_str() + i, NULL, 10);
  return true;
}

static bool jsonExtractFloat(const String& s, const char* key, float& out) {
  String pat = String("\"") + key + "\":";
  int i = s.indexOf(pat);
  if (i < 0) return false;
  i += pat.length();
  out = strtof(s.c_str() + i, NULL);
  return true;
}

static bool jsonExtractString(const String& s, const char* key, String& out) {
  String pat = String("\"") + key + "\"";
  int i = s.indexOf(pat);
  if (i < 0) return false;
  i += pat.length();
  i = s.indexOf(':', i);
  if (i < 0) return false;
  i++;
  while (i < (int)s.length() &&
         (s.charAt(i) == ' ' || s.charAt(i) == '\t' ||
          s.charAt(i) == '\r' || s.charAt(i) == '\n')) {
    i++;
  }
  if (i >= (int)s.length() || s.charAt(i) != '\"') return false;
  i++;
  int j = i;
  while (j < (int)s.length()) {
    if (s.charAt(j) == '\\' && j + 1 < (int)s.length()) {
      j += 2;
      continue;
    }
    if (s.charAt(j) == '\"') break;
    j++;
  }
  if (j >= (int)s.length()) return false;
  out = s.substring(i, j);
  return true;
}

static String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c == '\\' || c == '\"') o += '\\';
    o += c;
  }
  return o;
}

static void hostSendLine(const String& line) {
  if (g_uart_ota_lock) {
    return;
  }
  Host_UART.print(line);
  Host_UART.print("\r\n");
  Host_UART.flush();
  DBG("[TX->CH585] ");
  DBGLN(line);
}

static void hostSendOtaLine(const String& line) {
  Host_UART.print(line);
  Host_UART.print("\r\n");
  Host_UART.flush();
  DBG("[OTA TX->CH585] ");
  DBGLN(line);
}

static void emitWifiStatus(const char* state, const char* ssid = NULL,
                           const char* ip = NULL, int rssi = 0,
                           const char* reason = NULL) {
  String msg = String("{\"event\":\"wifi_status\",\"state\":\"") + state + "\"";
  if (ssid && ssid[0]) {
    msg += ",\"ssid\":\"" + jsonEscape(String(ssid)) + "\"";
  }
  if (ip && ip[0]) {
    msg += ",\"ip\":\"" + String(ip) + "\"";
  }
  if (rssi != 0) {
    msg += ",\"rssi\":" + String(rssi);
  }
  if (reason && reason[0]) {
    msg += ",\"reason\":\"" + String(reason) + "\"";
  }
  msg += "}";
  hostSendLine(msg);
}

static void emitWifiAck(const char* cmd) {
  hostSendLine(String("{\"event\":\"wifi_ack\",\"cmd\":\"") + cmd + "\"}");
}

// ==================== NVS ====================
static void wifiLoadNvs() {
  prefs.begin("wifi", true);
  saved_ssid = prefs.getString("ssid", "");
  saved_pass = prefs.getString("pass", "");
  prefs.end();
}

static void wifiSaveNvs(const String& ssid, const String& pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  saved_ssid = ssid;
  saved_pass = pass;
}

static void wifiClearNvs() {
  prefs.begin("wifi", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  saved_ssid = "";
  saved_pass = "";
}

static void wifiCacheScanChannels(int n) {
  wifi_ap_ch_cache_n = 0;
  for (int i = 0; i < n && wifi_ap_ch_cache_n < WIFI_AP_CH_CACHE; i++) {
    String ss = WiFi.SSID(i);
    if (ss.length() == 0) continue;
    wifi_ap_ch_cache[wifi_ap_ch_cache_n].ssid = ss;
    wifi_ap_ch_cache[wifi_ap_ch_cache_n].ch = WiFi.channel(i);
    wifi_ap_ch_cache_n++;
  }
}

static int wifiFindCachedChannel(const String& ssid) {
  for (int i = 0; i < wifi_ap_ch_cache_n; i++) {
    if (wifi_ap_ch_cache[i].ssid == ssid) {
      return wifi_ap_ch_cache[i].ch;
    }
  }
  return 0;
}

static int wifiScanChannelForSsid(const String& ssid) {
  int n;
  int i;

  if (ssid.length() == 0) {
    return 0;
  }
  WiFi.scanDelete();
  n = WiFi.scanNetworks(false, false, false, WIFI_SCAN_MS_PER_CHAN);
  if (n <= 0) {
    WiFi.scanDelete();
    return 0;
  }
  wifiCacheScanChannels(n);
  for (i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) {
      int ch = WiFi.channel(i);
      WiFi.scanDelete();
      DBGF("[WiFi] scan resolve ssid=\"%s\" ch=%d rssi=%d\n",
           ssid.c_str(), ch, WiFi.RSSI(i));
      return ch;
    }
  }
  WiFi.scanDelete();
  return 0;
}

static void wifiStartConnect(const String& ssid, const String& pass, bool save_on_success) {
  int ch;
  bool ok;

  if (wifiIsBusyConnect() &&
      ssid == pending_ssid && pass == pending_pass &&
      (millis() - wifi_last_begin_ms) < 3000) {
    DBGLN("[WiFi] skip duplicate begin");
    emitWifiStatus("connecting", pending_ssid.c_str());
    return;
  }

  pending_ssid = ssid;
  pending_pass = pass;
  pending_save_on_success = save_on_success;
  wifi_last_begin_ms = millis();
  wifi_boot_connect_pending = false;

  wifi_run_state = save_on_success ? WIFI_ST_PROVISIONING : WIFI_ST_CONNECTING;
  wifi_action_deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  wifi_scan_pending = false;
  /*
   * 先通知 UI；从 WiFi.begin() 开始直到成功/最终失败，不再因状态轮询
   * 对 Host UART 发送数据，保证认证窗口与 standalone 测试一致。
   */
  emitWifiStatus("connecting", pending_ssid.c_str());
  WiFi.scanDelete();

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.setSleep(WIFI_PS_NONE);
  /* 与已验证通过的 standaloneTryConnect() 保持完全相同的连接配置。 */
  WiFi.persistent(false);
  WiFi.setTxPower(WIFI_TX_POWER);
  DBGF("[WiFi] TX power=%d quarter-dBm\n", (int)WiFi.getTxPower());

  ch = wifiFindCachedChannel(pending_ssid);
  if (ch <= 0) {
    ch = wifiScanChannelForSsid(pending_ssid);
  }
  if (ch > 0) {
    ok = WiFi.begin(pending_ssid.c_str(), pending_pass.c_str(), ch);
    DBGF("[WiFi] begin ssid=\"%s\" pass_len=%u ch=%d ok=%d\n",
         pending_ssid.c_str(), (unsigned)pending_pass.length(), ch, ok ? 1 : 0);
  } else {
    ok = WiFi.begin(pending_ssid.c_str(), pending_pass.c_str());
    DBGF("[WiFi] begin ssid=\"%s\" pass_len=%u ch=auto ok=%d\n",
         pending_ssid.c_str(), (unsigned)pending_pass.length(), ok ? 1 : 0);
  }
  if (!ok) {
    DBGLN("[WiFi] begin returned false");
  }
}

static const char* wifiFailReason(wl_status_t st) {
  switch (st) {
    case WL_NO_SSID_AVAIL: return "not_found";
    case WL_CONNECT_FAILED: return "auth_fail";
    case WL_CONNECTION_LOST: return "lost";
    default: return "timeout";
  }
}

static void wifiOnConnected() {
  wifi_run_state = WIFI_ST_CONNECTED;
  wifi_reconnect_delay_ms = 5000;
  DBGF("[WiFi] connected ip=%s rssi=%d\n",
       WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (pending_save_on_success) {
    wifiSaveNvs(pending_ssid, pending_pass);
    pending_save_on_success = false;
  }
  emitWifiStatus("connected", WiFi.SSID().c_str(),
                 WiFi.localIP().toString().c_str(), WiFi.RSSI());
  mqttClient.disconnect();
  last_mqtt_try = 0;
}

static void wifiOnConnectFailed(const char* reason) {
  pending_save_on_success = false;
  wifi_reconnect_at = millis() + 30000;
  if (saved_ssid.length() > 0) {
    wifi_run_state = WIFI_ST_FAILED;
  } else {
    wifi_run_state = WIFI_ST_NO_CONFIG;
  }
  DBGF("[WiFi] connect failed reason=%s\n", reason ? reason : "?");
  emitWifiStatus("failed", pending_ssid.c_str(), NULL, 0, reason);
}

static void wifiPollConnect() {
  wl_status_t st;

  if (wifi_run_state != WIFI_ST_CONNECTING &&
      wifi_run_state != WIFI_ST_PROVISIONING) {
    return;
  }
  st = WiFi.status();
  if (st == WL_CONNECTED) {
    wifiOnConnected();
    return;
  }
  if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
    wifiOnConnectFailed(wifiFailReason(st));
    WiFi.disconnect(true);
    return;
  }
  if (millis() >= wifi_action_deadline) {
    DBGF("[WiFi] timeout after %lus st=%d\n",
         WIFI_CONNECT_TIMEOUT_MS / 1000UL, (int)st);
    wifiOnConnectFailed(wifiFailReason(st));
    /*
     * 不调用 WiFi.disconnect()。即使 UI 显示失败，ESP-IDF 仍可继续
     * 后台重试；loop() 中的连接状态检查会在获得 IP 后重新上报 connected。
     */
  }
}

static void wifiTryBootConnect() {
  if (saved_ssid.length() == 0) {
    wifi_run_state = WIFI_ST_NO_CONFIG;
    emitWifiStatus("no_config");
    return;
  }
  wifiStartConnect(saved_ssid, saved_pass, false);
}

// ==================== Scan (sync fast, deferred in loop) ====================
static void wifiRestoreAfterScan(void) {
  if (WiFi.status() == WL_CONNECTED) {
    wifi_run_state = WIFI_ST_CONNECTED;
  } else if (wifiIsBusyConnect()) {
    return;
  } else if (saved_ssid.length() > 0) {
    wifi_run_state = WIFI_ST_FAILED;
  } else {
    wifi_run_state = WIFI_ST_NO_CONFIG;
  }
}

static void wifiSendScanResults(int n) {
  if (wifiIsBusyConnect()) {
    WiFi.scanDelete();
    return;
  }

  if (n <= 0) {
    wifi_ap_ch_cache_n = 0;
    s_last_wifi_list_json = "{\"event\":\"wifi_list\",\"count\":0,\"aps\":[]}";
    s_last_wifi_list_ms = millis();
    hostSendLine(s_last_wifi_list_json);
    wifiRestoreAfterScan();
    DBGLN("[WiFi] scan: 0 AP (host UART sent)");
    return;
  }

  int idx[32];
  int cnt = 0;
  int k;
  int count;

  for (int i = 0; i < n && cnt < 32; i++) {
    String ss = WiFi.SSID(i);
    if (ss.length() == 0) continue;
    idx[cnt++] = i;
  }
  for (int a = 0; a < cnt - 1; a++) {
    for (int b = a + 1; b < cnt; b++) {
      if (WiFi.RSSI(idx[b]) > WiFi.RSSI(idx[a])) {
        int t = idx[a];
        idx[a] = idx[b];
        idx[b] = t;
      }
    }
  }
  count = cnt > WIFI_SCAN_MAX_APS ? WIFI_SCAN_MAX_APS : cnt;

  wifiCacheScanChannels(n);

  String msg = "{\"event\":\"wifi_list\",\"count\":" + String(count) + ",\"aps\":[";
  for (k = 0; k < count; k++) {
    int i = idx[k];
    if (k > 0) msg += ",";
    msg += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\"";
    msg += ",\"rssi\":" + String(WiFi.RSSI(i));
    msg += ",\"ch\":" + String(WiFi.channel(i)) + "}";
    DBGF("[WiFi] AP: %s rssi=%d ch=%d\n",
         WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
  }
  msg += "]}";
  s_last_wifi_list_json = msg;
  s_last_wifi_list_ms = millis();
  hostSendLine(msg);
  DBGF("[WiFi] wifi_list sent to host, count=%d len=%u\n", count, (unsigned)msg.length());
  wifiRestoreAfterScan();
}

static void wifiDoScanFast(void) {
  unsigned long t0;
  int n;

  if (wifiIsBusyConnect()) {
    wifi_scan_pending = false;
    return;
  }

  wifi_scan_active = true;
  wifi_run_state = WIFI_ST_SCANNING;
  emitWifiStatus("scanning");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.scanDelete();

  t0 = millis();
  /*
   * 这里不能调用 processHostUart()：
   * 若扫描期间收到 wifi_connect，会递归进入 wifiStartConnect()，
   * 与尚未返回的 scanNetworks()/disconnect() 同时操作 WiFi 驱动，
   * 导致关联过程永久保持 WL_DISCONNECTED。
   * 扫描最长约 2.5 秒，命令保留在 UART FIFO，扫描完成后由主循环处理。
   */
  n = WiFi.scanNetworks(false, false, false, WIFI_SCAN_MS_PER_CHAN);
  DBGF("[WiFi] scan done: n=%d ms=%lu\n", n, millis() - t0);
  wifi_scan_active = false;

  if (n < 0) {
    n = 0;
  }
  wifiSendScanResults(n);
  WiFi.scanDelete();
}

static void wifiRequestScan(void) {
  if (wifiIsBusyConnect()) {
    return;
  }
  if (wifi_scan_pending || wifi_run_state == WIFI_ST_SCANNING) {
    return;
  }
  wifi_scan_pending = true;
}

static void wifiPollScanRequest(void) {
  if (!wifi_scan_pending) {
    return;
  }
  if (wifiIsBusyConnect()) {
    return;
  }
  wifi_scan_pending = false;
  wifiDoScanFast();
}

// ==================== MQTT（屏显对齐：心率平滑 + 有效值平均 + PWV 会话中位）====================
#define DISP_PWV_RING  16

static uint8_t  s_disp_w_hr = 0;
static uint8_t  s_disp_f_hr = 0;
static float    s_disp_pwv_ring[DISP_PWV_RING];
static uint8_t  s_disp_pwv_cnt = 0;
static uint8_t  s_disp_pwv_idx = 0;

static void dispVitalsReset() {
  s_disp_w_hr = 0;
  s_disp_f_hr = 0;
  s_disp_pwv_cnt = 0;
  s_disp_pwv_idx = 0;
}

/* 对齐 Central_LCD ui_hr_display_step() */
static uint8_t hrDisplayStep(uint8_t& display, uint8_t sample) {
  int delta;
  if (sample == 0) return display;
  if (display == 0) {
    display = sample;
    return sample;
  }
  if (sample >= 76) {
    display = (uint8_t)((display * 3U + sample * 7U + 5U) / 10U);
    return display;
  }
  delta = (int)sample - (int)display;
  if (delta >= 10) {
    display = (uint8_t)(display + (delta * 3) / 4);
  } else if (delta <= -8) {
    display = (uint8_t)(display + (delta * 2) / 10);
  } else {
    display = (uint8_t)((display * 5U + sample * 5U + 5U) / 10U);
  }
  return display;
}

/* 对齐屏显：两路都有效 → (a+b+1)/2；仅一路 → 用该路 */
static void mergeDisplayVitals(unsigned long w_hr, unsigned long w_spo2,
                               unsigned long f_hr, unsigned long f_spo2,
                               unsigned long& hr, unsigned long& spo2,
                               bool& vitals_valid) {
  const bool w_ok = (w_hr > 0) || (w_spo2 > 0);
  const bool f_ok = (f_hr > 0) || (f_spo2 > 0);
  hr = 0;
  spo2 = 0;
  vitals_valid = false;
  if (w_ok && f_ok) {
    hr = (w_hr + f_hr + 1UL) / 2UL;
    spo2 = (w_spo2 + f_spo2 + 1UL) / 2UL;
    vitals_valid = true;
  } else if (w_ok) {
    hr = w_hr;
    spo2 = w_spo2;
    vitals_valid = true;
  } else if (f_ok) {
    hr = f_hr;
    spo2 = f_spo2;
    vitals_valid = true;
  }
}

/* 对齐屏显 PWV：会话环中位数（主机 UI_SetPwv 用的是 median，不是单拍 pwv） */
static float pwvDisplayPushMedian(float pwv) {
  float tmp[DISP_PWV_RING];
  uint8_t n, i, j;
  s_disp_pwv_ring[s_disp_pwv_idx] = pwv;
  s_disp_pwv_idx = (uint8_t)((s_disp_pwv_idx + 1U) % DISP_PWV_RING);
  if (s_disp_pwv_cnt < DISP_PWV_RING) s_disp_pwv_cnt++;
  n = s_disp_pwv_cnt;
  if (n == 0) return 0.0f;
  for (i = 0; i < n; i++) tmp[i] = s_disp_pwv_ring[i];
  for (i = 0; i < n; i++) {
    for (j = (uint8_t)(i + 1U); j < n; j++) {
      if (tmp[i] > tmp[j]) {
        float t = tmp[i];
        tmp[i] = tmp[j];
        tmp[j] = t;
      }
    }
  }
  if ((n & 1U) != 0U) return tmp[n / 2U];
  return (tmp[n / 2U - 1U] + tmp[n / 2U]) * 0.5f;
}

static String buildPwvMqttPayload(const String& hostJson) {
  unsigned long pulse_id = 0, ts = 0;
  unsigned long w_hr = 0, w_spo2 = 0, f_hr = 0, f_spo2 = 0, ml_accept = 1;
  unsigned long w_hr_disp = 0, f_hr_disp = 0;
  unsigned long hr = 0, spo2 = 0;
  float pwv_inst = 0.0f, pwv_disp = 0.0f, ml_score = -1.0f;
  bool has_pwv = jsonExtractFloat(hostJson, "pwv", pwv_inst);
  bool has_ml = jsonExtractFloat(hostJson, "ml_score", ml_score);
  bool vitals_valid = false;

  jsonExtractUInt(hostJson, "pulse_id", pulse_id);
  jsonExtractUInt(hostJson, "ts", ts);
  jsonExtractUInt(hostJson, "w_hr", w_hr);
  jsonExtractUInt(hostJson, "w_spo2", w_spo2);
  jsonExtractUInt(hostJson, "f_hr", f_hr);
  jsonExtractUInt(hostJson, "f_spo2", f_spo2);
  jsonExtractUInt(hostJson, "ml_accept", ml_accept);

  /* 先按屏显规则平滑各端心率，再平均 */
  if (w_hr > 0) w_hr_disp = hrDisplayStep(s_disp_w_hr, (uint8_t)w_hr);
  else w_hr_disp = s_disp_w_hr;
  if (f_hr > 0) f_hr_disp = hrDisplayStep(s_disp_f_hr, (uint8_t)f_hr);
  else f_hr_disp = s_disp_f_hr;
  mergeDisplayVitals(w_hr_disp, w_spo2, f_hr_disp, f_spo2, hr, spo2, vitals_valid);

  if (has_pwv) {
    pwv_disp = pwvDisplayPushMedian(pwv_inst);
  }

  /* 小程序请用 hr/spo2/pwv（已与屏对齐）；分路与 pwv_inst 仅调试 */
  String msg = "{\"device\":\"pwv\"";
  msg += ",\"pulse_id\":" + String(pulse_id);
  msg += ",\"ts\":" + String(ts);
  msg += ",\"hr\":" + String(hr);
  msg += ",\"spo2\":" + String(spo2);
  msg += ",\"vitals_valid\":" + String(vitals_valid ? 1 : 0);
  if (has_pwv) {
    msg += ",\"pwv\":" + String(pwv_disp, 1);
    msg += ",\"pwv_inst\":" + String(pwv_inst, 1);
  }
  msg += ",\"w_hr\":" + String(w_hr);
  msg += ",\"w_spo2\":" + String(w_spo2);
  msg += ",\"f_hr\":" + String(f_hr);
  msg += ",\"f_spo2\":" + String(f_spo2);
  if (has_ml) msg += ",\"ml_score\":" + String(ml_score, 2);
  msg += ",\"valid\":" + String(ml_accept);
  msg += "}";
  return msg;
}

static String buildSessionEndMqttPayload(const String& hostJson) {
  unsigned long w_hr = 0, w_spo2 = 0, f_hr = 0, f_spo2 = 0, pwv_valid = 0;
  unsigned long hr = 0, spo2 = 0;
  float pwv = 0.0f;
  bool has_pwv = jsonExtractFloat(hostJson, "pwv", pwv);
  bool vitals_valid = false;
  String reason = "unknown";

  jsonExtractUInt(hostJson, "w_hr", w_hr);
  jsonExtractUInt(hostJson, "w_spo2", w_spo2);
  jsonExtractUInt(hostJson, "f_hr", f_hr);
  jsonExtractUInt(hostJson, "f_spo2", f_spo2);
  jsonExtractUInt(hostJson, "pwv_valid", pwv_valid);
  (void)jsonExtractString(hostJson, "reason", reason);
  /* 结束包主机已是定稿，不再走平滑，直接平均以对齐 ApplySessionResult */
  mergeDisplayVitals(w_hr, w_spo2, f_hr, f_spo2, hr, spo2, vitals_valid);
  dispVitalsReset();

  String msg = "{\"device\":\"pwv\",\"event\":\"session_end\"";
  msg += ",\"reason\":\"" + jsonEscape(reason) + "\"";
  msg += ",\"hr\":" + String(hr);
  msg += ",\"spo2\":" + String(spo2);
  msg += ",\"vitals_valid\":" + String(vitals_valid ? 1 : 0);
  msg += ",\"w_hr\":" + String(w_hr);
  msg += ",\"w_spo2\":" + String(w_spo2);
  msg += ",\"f_hr\":" + String(f_hr);
  msg += ",\"f_spo2\":" + String(f_spo2);
  if (has_pwv) msg += ",\"pwv\":" + String(pwv, 1);
  msg += ",\"pwv_valid\":" + String(pwv_valid);
  msg += "}";
  return msg;
}

static void mqttTryConnect() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient.connected()) return;
  if (millis() - last_mqtt_try < MQTT_RETRY_MS) return;
  last_mqtt_try = millis();
  if (mqttClient.connect(mqtt_client_id, mqtt_username, mqtt_password)) {
    DBGLN("[MQTT] connected");
    mqttClient.subscribe(topic_ota_cmd);
    DBGF("[MQTT] subscribed %s\n", topic_ota_cmd);
  } else {
    DBGF("[MQTT] connect failed state=%d (server=%s:%d)\n",
         mqttClient.state(), mqtt_server, mqtt_port);
  }
}

static void publishHeart() {
  if (WiFi.status() != WL_CONNECTED) return;
  String msg = "{\"online\":true,\"ip\":\"" + WiFi.localIP().toString() +
               "\",\"phase\":2,\"wifi\":\"connected\"}";
  mqttClient.publish(topic_health, msg.c_str());
}

static void sendDataToMQTT(const String& hostJson) {
  if (hostJson.length() == 0 || WiFi.status() != WL_CONNECTED) return;
  String payload = buildPwvMqttPayload(hostJson);
  if (!mqttClient.publish(topic_data, payload.c_str())) {
    mqttClient.disconnect();
    mqttTryConnect();
    mqttClient.publish(topic_data, payload.c_str());
  }
}

// ==================== CH585 JSON OTA bridge ====================
static uint32_t otaCrc32Accum(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
  }
  return crc;
}

static void otaPublishStatus(const char* state, bool ok, const char* detail,
                             uint32_t offset = 0, uint32_t total = 0) {
  if (!mqttClient.connected()) return;

  String msg = String("{\"state\":\"") + state + "\",\"ok\":" + (ok ? "1" : "0");
  if (detail && detail[0]) msg += ",\"detail\":\"" + String(detail) + "\"";
  if (total != 0) {
    msg += ",\"offset\":" + String(offset);
    msg += ",\"total\":" + String(total);
  }
  msg += "}";
  mqttClient.publish(topic_ota_status, msg.c_str());
}

static void otaPollHostUart() {
  while (Host_UART.available() > 0) {
    char c = (char)Host_UART.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (g_ota_rx_assembly.length() > 0) {
        DBG("[OTA RX<-CH585] ");
        DBGLN(g_ota_rx_assembly);
        g_ota_ack_line = g_ota_rx_assembly;
        g_ota_ack_ready = true;
        g_ota_rx_assembly = "";
      }
      continue;
    }
    g_ota_rx_assembly += c;
    if (g_ota_rx_assembly.length() > UART_FRAME_MAX) {
      g_ota_rx_assembly = "";
    }
  }
}

static bool otaWaitAck(const char* phase, bool expected_ok, uint32_t timeout_ms) {
  const unsigned long deadline = millis() + timeout_ms;
  const String phase_match = String("\"phase\":\"") + phase + "\"";

  g_ota_rx_assembly = "";
  g_ota_ack_line = "";
  g_ota_ack_ready = false;
  while ((long)(millis() - deadline) < 0) {
    otaPollHostUart();
    mqttClient.loop();
    if (g_ota_ack_ready) {
      bool phase_ok = g_ota_ack_line.indexOf("\"event\":\"ota_ack\"") >= 0 &&
                      g_ota_ack_line.indexOf(phase_match) >= 0;
      bool ok = g_ota_ack_line.indexOf("\"ok\":1") >= 0;
      bool nack = g_ota_ack_line.indexOf("\"ok\":0") >= 0;
      g_ota_ack_ready = false;
      g_ota_ack_line = "";
      if (phase_ok && ((expected_ok && ok) || (!expected_ok && nack))) {
        return true;
      }
      if (phase_ok && nack) {
        return false;
      }
    }
    delay(1);
  }
  return false;
}

static String otaHexEncode(const uint8_t* data, size_t len) {
  static const char k_hex[] = "0123456789abcdef";
  String out;
  out.reserve(len * 2U);
  for (size_t i = 0; i < len; i++) {
    out += k_hex[data[i] >> 4];
    out += k_hex[data[i] & 0x0F];
  }
  return out;
}

static void otaAbortHost() {
  hostSendOtaLine("{\"cmd\":\"ota_abort\"}");
  (void)otaWaitAck("abort", true, OTA_DATA_ACK_MS);
}

static bool otaSendDataBlock(uint32_t offset, const uint8_t* data, size_t len) {
  String line = String("{\"cmd\":\"ota_data\",\"off\":") + String(offset) +
                ",\"data\":\"" + otaHexEncode(data, len) + "\"}";
  for (uint8_t retry = 0; retry < 3; retry++) {
    hostSendOtaLine(line);
    if (otaWaitAck("data", true, OTA_DATA_ACK_MS)) {
      return true;
    }
    delay(20);
  }
  return false;
}

static void otaRunPendingJob() {
  if (!g_ota_job.pending) return;
  g_ota_job.pending = false;

  if (WiFi.status() != WL_CONNECTED) {
    otaPublishStatus("error", false, "no_wifi");
    return;
  }
  if (g_ota_job.size == 0 || g_ota_job.size > OTA_MAX_IMAGE ||
      g_ota_job.url.length() == 0) {
    otaPublishStatus("error", false, "bad_metadata");
    return;
  }

  HTTPClient http;
  WiFiClient client;
  uint8_t buf[OTA_CHUNK_BYTES];
  uint32_t offset = 0;
  uint32_t crc = 0xFFFFFFFFUL;
  bool host_started = false;

  g_uart_ota_lock = true;
  otaPublishStatus("downloading", true, "");
  http.setTimeout(OTA_HTTP_TIMEOUT_MS);
  if (!http.begin(client, g_ota_job.url) || http.GET() != HTTP_CODE_OK) {
    otaPublishStatus("error", false, "http_get");
    g_uart_ota_lock = false;
    http.end();
    return;
  }
  const int content_len = http.getSize();
  if (content_len < 0 || (uint32_t)content_len != g_ota_job.size) {
    otaPublishStatus("error", false, "http_size");
    g_uart_ota_lock = false;
    http.end();
    return;
  }

  String begin_line = String("{\"cmd\":\"ota_begin\",\"size\":") +
                      String(g_ota_job.size) + ",\"crc\":" +
                      String(g_ota_job.crc) + ",\"ver\":" +
                      String(g_ota_job.ver) + "}";
  bool begin_ok = false;
  for (uint8_t retry = 0; retry < 3; retry++) {
    hostSendOtaLine(begin_line);
    if (otaWaitAck("begin", true, OTA_BEGIN_ACK_MS)) {
      begin_ok = true;
      break;
    }
    delay(50);
  }
  if (!begin_ok) {
    otaPublishStatus("error", false, "begin_ack");
    g_uart_ota_lock = false;
    http.end();
    return;
  }
  host_started = true;
  otaPublishStatus("forwarding", true, "", 0, g_ota_job.size);

  WiFiClient* stream = http.getStreamPtr();
  while (offset < g_ota_job.size) {
    size_t want = min((uint32_t)OTA_CHUNK_BYTES, g_ota_job.size - offset);
    size_t got = stream->readBytes(buf, want);
    if (got != want) {
      otaPublishStatus("error", false, "http_read", offset, g_ota_job.size);
      otaAbortHost();
      g_uart_ota_lock = false;
      http.end();
      return;
    }
    crc = otaCrc32Accum(crc, buf, got);
    if (!otaSendDataBlock(offset, buf, got)) {
      otaPublishStatus("error", false, "data_ack", offset, g_ota_job.size);
      otaAbortHost();
      g_uart_ota_lock = false;
      http.end();
      return;
    }
    offset += (uint32_t)got;
    if ((offset % 4096U) == 0U || offset == g_ota_job.size) {
      otaPublishStatus("forwarding", true, "", offset, g_ota_job.size);
    }
    mqttClient.loop();
    delay(1);
  }
  http.end();

  if ((crc ^ 0xFFFFFFFFUL) != g_ota_job.crc) {
    otaPublishStatus("error", false, "http_crc", offset, g_ota_job.size);
    otaAbortHost();
    g_uart_ota_lock = false;
    return;
  }

  hostSendOtaLine("{\"cmd\":\"ota_finish\"}");
  if (!otaWaitAck("finish", true, OTA_FINISH_ACK_MS)) {
    otaPublishStatus("error", false, "finish_ack", offset, g_ota_job.size);
    if (host_started) otaAbortHost();
    g_uart_ota_lock = false;
    return;
  }
  otaPublishStatus("applying", true, "", offset, g_ota_job.size);
  g_uart_ota_lock = false;
  g_ota_wait_boot = true;
  g_ota_expected_ver = g_ota_job.ver;
  g_ota_boot_deadline = millis() + OTA_BOOT_REPORT_MS;
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, topic_ota_cmd) != 0) return;

  String cmd;
  String url;
  unsigned long size = 0;
  unsigned long crc = 0;
  unsigned long ver = 1;
  bool force;
  for (unsigned int i = 0; i < length; i++) cmd += (char)payload[i];

  if (g_uart_ota_lock || g_ota_job.pending || g_ota_wait_boot) {
    otaPublishStatus("error", false, "busy");
    return;
  }
  if (!jsonExtractString(cmd, "url", url) ||
      !jsonExtractUInt(cmd, "size", size) ||
      !jsonExtractUInt(cmd, "crc", crc) ||
      size == 0 || size > OTA_MAX_IMAGE) {
    otaPublishStatus("error", false, "bad_command");
    return;
  }
  (void)jsonExtractUInt(cmd, "ver", ver);
  force = cmd.indexOf("\"force\":true") >= 0;
  if (!force && g_host_firmware_ver != 0 &&
      (uint32_t)ver <= g_host_firmware_ver) {
    otaPublishStatus("error", false, "version_not_new");
    return;
  }
  g_ota_job.url = url;
  g_ota_job.size = (uint32_t)size;
  g_ota_job.crc = (uint32_t)crc;
  g_ota_job.ver = (uint32_t)ver;
  g_ota_job.pending = true;
  otaPublishStatus("queued", true, "");
}

static void otaHandleBootReport(const String& frame) {
  unsigned long version = 0;
  unsigned long ota_status = 0;

  if (!jsonExtractUInt(frame, "ver", version)) {
    return;
  }
  (void)jsonExtractUInt(frame, "ota_status", ota_status);
  g_host_firmware_ver = (uint32_t)version;
  DBGF("[OTA] boot report ver=%lu ota_status=%lu\n", version, ota_status);

  if (!g_ota_wait_boot) {
    return;
  }
  g_ota_wait_boot = false;
  if ((uint32_t)version == g_ota_expected_ver) {
    otaPublishStatus("done", true, "boot_report");
  } else {
    otaPublishStatus("error", false, "boot_version");
  }
}

static void otaPollBootTimeout() {
  if (g_ota_wait_boot && (long)(millis() - g_ota_boot_deadline) >= 0) {
    g_ota_wait_boot = false;
    otaPublishStatus("error", false, "boot_timeout");
  }
}

// ==================== Host commands ====================
static void handleWifiCmd(const String& frame) {
  String cmd;
  wifi_host_cmd_seen = true;
  wifi_boot_connect_pending = false;

  if (!jsonExtractString(frame, "cmd", cmd)) {
    DBGLN("[CMD] parse cmd failed");
    return;
  }
  DBG("[CMD] cmd=");
  DBGLN(cmd);

  if (cmd == "ping") {
    hostSendLine("{\"event\":\"pong\"}");
    return;
  }
  if (cmd == "wifi_scan") {
    emitWifiAck("wifi_scan");
    wifiRequestScan();
    return;
  }
  if (cmd == "wifi_status") {
    if (WiFi.status() == WL_CONNECTED) {
      emitWifiStatus("connected", WiFi.SSID().c_str(),
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else if (wifiIsBusyConnect()) {
      /*
       * UI 已收到一次 connecting。认证期间不回传每 2 秒一次的
       * connecting，避免 Host UART 发送和 WiFi 认证重试并发。
       */
      return;
    } else if (wifi_run_state == WIFI_ST_SCANNING) {
      emitWifiStatus("scanning");
    } else if (saved_ssid.length() == 0) {
      emitWifiStatus("no_config");
    } else {
      emitWifiStatus("failed", saved_ssid.c_str(), NULL, 0, "disconnected");
    }
    return;
  }
  if (cmd == "wifi_forget") {
    emitWifiAck("wifi_forget");
    wifiClearNvs();
    WiFi.disconnect(true);
    wifi_run_state = WIFI_ST_NO_CONFIG;
    mqttClient.disconnect();
    emitWifiStatus("no_config");
    return;
  }
  if (cmd == "wifi_connect") {
    String ssid, pass;
    if (!jsonExtractString(frame, "ssid", ssid)) return;
    if (!jsonExtractString(frame, "pass", pass)) pass = "";
    emitWifiAck("wifi_connect");
    wifiStartConnect(ssid, pass, true);
    return;
  }
}

static inline void ledD4Set(bool on) {
  digitalWrite(LED_D4, on ? HIGH : LOW);
}

static inline void ledD5Set(bool on) {
#if LED_D5_ACTIVE_LOW
  digitalWrite(LED_D5, on ? LOW : HIGH);
#else
  digitalWrite(LED_D5, on ? HIGH : LOW);
#endif
}

static void handleLedCommand(char cmd) {
  switch (cmd) {
    case '1': ledD4Set(true); break;
    case '2': ledD4Set(false); break;
    case '3': ledD5Set(true); break;
    case '4': ledD5Set(false); break;
    default: break;
  }
}

static void indicateMlAccept(unsigned long ml_accept, bool has_ml_field) {
  if (!has_ml_field || ml_accept != 1) return;
  ledD5Set(true);
  delay(80);
  ledD5Set(false);
}

static void handleHostFrame(const String& frame) {
  if (frame.length() == 0) return;

  DBG("[RX<-CH585] ");
  DBGLN(frame);

  if (frame.length() == 1) {
    handleLedCommand(frame[0]);
    return;
  }

  if (frame.charAt(0) != '{') {
    DBGLN("[RX] non-JSON ignored");
    return;
  }

  if (frame.indexOf("\"event\":\"boot_report\"") >= 0) {
    otaHandleBootReport(frame);
    return;
  }

  if (frame.indexOf("\"cmd\"") >= 0) {
    handleWifiCmd(frame);
    return;
  }

  if (frame.indexOf("\"event\":\"session_end\"") >= 0) {
    if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
      String payload = buildSessionEndMqttPayload(frame);
      mqttClient.publish(topic_data, payload.c_str());
    }
    return;
  }

  if (frame.indexOf("\"pulse_id\"") >= 0) {
    unsigned long ml_accept = 1;
    float ml_score = -1.0f;
    bool has_ml = jsonExtractFloat(frame, "ml_score", ml_score);
    bool has_accept = jsonExtractUInt(frame, "ml_accept", ml_accept);
    indicateMlAccept(ml_accept, has_accept);
    sendDataToMQTT(frame);
  }
}

static void processHostUart() {
  while (Host_UART.available() > 0) {
    char c = (char)Host_UART.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleHostFrame(uartFrame);
      uartFrame = "";
    } else {
      uartFrame += c;
      if (uartFrame.length() > UART_FRAME_MAX) uartFrame = "";
    }
  }
}

static void wifiPollReconnect() {
  if (wifi_run_state == WIFI_ST_SCANNING ||
      wifi_run_state == WIFI_ST_CONNECTING ||
      wifi_run_state == WIFI_ST_PROVISIONING) {
    return;
  }
  if (millis() < wifi_reconnect_at) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (wifi_run_state != WIFI_ST_CONNECTED) {
      wifi_run_state = WIFI_ST_CONNECTED;
    }
    return;
  }
  if (saved_ssid.length() == 0) {
    wifi_run_state = WIFI_ST_NO_CONFIG;
    return;
  }
  wifi_reconnect_at = millis() + wifi_reconnect_delay_ms;
  if (wifi_reconnect_delay_ms < 30000) {
    wifi_reconnect_delay_ms += 5000;
  }
  wifiStartConnect(saved_ssid, saved_pass, false);
}

#if WIFI_STANDALONE_TEST

static const char* standaloneWlName(wl_status_t st) {
  switch (st) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "AUTH_FAIL";
    case WL_CONNECTION_LOST: return "LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "?";
  }
}

static void standaloneLedOk(bool on) {
#if LED_D5_ACTIVE_LOW
  digitalWrite(LED_D5, on ? LOW : HIGH);
#else
  digitalWrite(LED_D5, on ? HIGH : LOW);
#endif
}

static int standaloneScanFind(const char* ssid) {
  int n;
  int ch = 0;
  int i;

  DBGLN("[TEST] scanning 2.4GHz APs ...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.disconnect(true);
  delay(200);
  WiFi.scanDelete();
  n = WiFi.scanNetworks(false, false, false, 150);
  DBGF("[TEST] scan result n=%d\n", n);
  if (n <= 0) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    DBGF("  [%d] ssid=\"%s\" rssi=%d ch=%d\n",
         i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
    if (strcmp(WiFi.SSID(i).c_str(), ssid) == 0) {
      ch = WiFi.channel(i);
    }
  }
  WiFi.scanDelete();
  return ch;
}

static bool standaloneTryConnect(const char* ssid, const char* pass, int ch, const char* tag) {
  unsigned long deadline;
  wl_status_t st;
  bool ok;

  DBGLN();
  DBGF("[TEST] --- %s ---\n", tag);
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.persistent(false);

  if (ch > 0) {
    ok = WiFi.begin(ssid, pass, ch);
    DBGF("[TEST] WiFi.begin(\"%s\", ***, ch=%d) -> %d\n", ssid, ch, ok ? 1 : 0);
  } else {
    ok = WiFi.begin(ssid, pass);
    DBGF("[TEST] WiFi.begin(\"%s\", ***) -> %d\n", ssid, ok ? 1 : 0);
  }

  deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (millis() < deadline) {
    st = WiFi.status();
    if (st == WL_CONNECTED) {
      DBGF("[TEST] OK ip=%s rssi=%d gw=%s\n",
           WiFi.localIP().toString().c_str(), WiFi.RSSI(),
           WiFi.gatewayIP().toString().c_str());
      standaloneLedOk(true);
      return true;
    }
    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
      DBGF("[TEST] FAIL early st=%d (%s)\n", (int)st, standaloneWlName(st));
      return false;
    }
    delay(1000);
    DBGF("[TEST] waiting ... st=%d (%s) t=%lus\n",
         (int)st, standaloneWlName(st), (unsigned long)((deadline - millis()) / 1000));
  }
  st = WiFi.status();
  DBGF("[TEST] TIMEOUT st=%d (%s)\n", (int)st, standaloneWlName(st));
  return false;
}

static void standaloneSetup() {
  int ch;

  pinMode(LED_D5, OUTPUT);
  standaloneLedOk(false);

  Serial.begin(115200);
  delay(800);
  DBGLN();
  DBGLN("========================================");
  DBGLN("  SuperMini STANDALONE WiFi Test");
  DBGLN("  (CH585 / Host UART disabled)");
  DBGLN("========================================");
  DBGF("Chip model : %s\n", ESP.getChipModel());
  DBGF("CPU MHz    : %u\n", ESP.getCpuFreqMHz());
  DBGF("MAC        : %s\n", WiFi.macAddress().c_str());
  DBGF("Target SSID: \"%s\"\n", STANDALONE_WIFI_SSID);
  DBGF("Pass len   : %u\n", (unsigned)strlen(STANDALONE_WIFI_PASS));
  DBGLN("IDE: USB CDC On Boot = Enabled, then press RST after upload");
  DBGLN();

  ch = standaloneScanFind(STANDALONE_WIFI_SSID);
  if (ch > 0) {
    DBGF("[TEST] target AP found on channel %d\n", ch);
  } else {
    DBGLN("[TEST] WARN: target SSID not in scan (hidden / 5G-only / out of range?)");
  }

  if (standaloneTryConnect(STANDALONE_WIFI_SSID, STANDALONE_WIFI_PASS, ch, "connect with scan channel")) {
    DBGLN("[TEST] RESULT: SUCCESS");
    return;
  }

  if (ch > 0 && standaloneTryConnect(STANDALONE_WIFI_SSID, STANDALONE_WIFI_PASS, 0, "retry auto channel")) {
    DBGLN("[TEST] RESULT: SUCCESS (auto channel)");
    return;
  }

  DBGLN("[TEST] RESULT: FAILED — if scan saw AP but connect fails, check password/router.");
  DBGLN("[TEST] Edit STANDALONE_WIFI_SSID/PASS at top of .ino and re-upload.");
}

static void standaloneLoop() {
  static unsigned long last = 0;

  if (WiFi.status() == WL_CONNECTED) {
    if (millis() - last > 5000) {
      last = millis();
      DBGF("[TEST] alive ip=%s rssi=%d\n",
           WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
  }
  delay(100);
}

#endif /* WIFI_STANDALONE_TEST */

void setup() {
#if WIFI_STANDALONE_TEST
  standaloneSetup();
  return;
#endif

  pinMode(LED_D4, OUTPUT);
  pinMode(LED_D5, OUTPUT);
  ledD4Set(false);
  ledD5Set(false);

  Serial.begin(115200);
  delay(500);
  DBGLN();
  DBGLN("=== ESP32-C3 SuperMini CH585 gateway ===");
  DBGF("Host_UART: RX=GPIO%d, TX=GPIO%d, %u 8N1\n",
       HOST_UART_RX, HOST_UART_TX, HOST_UART_BAUD);

  Host_UART.begin(HOST_UART_BAUD, SERIAL_8N1, HOST_UART_RX, HOST_UART_TX);
  uartFrame.reserve(UART_FRAME_MAX);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.persistent(false);
  WiFi.setTxPower(WIFI_TX_POWER);
  WiFi.onEvent(wifiDebugEvent);
  WiFi.setScanTimeout(WIFI_SCAN_TIMEOUT_MS);
  WiFi.setScanActiveMinTime(WIFI_SCAN_ACTIVE_MIN_MS);
  wifiLoadNvs();
  DBGF("[NVS] saved_ssid=\"%s\"\n", saved_ssid.c_str());
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(768);

  hostSendLine("{\"event\":\"pong\"}");
  wifi_run_state = WIFI_ST_NO_CONFIG;
  wifi_boot_connect_pending = (saved_ssid.length() > 0);
  if (!wifi_boot_connect_pending) {
    emitWifiStatus("no_config");
  } else {
    DBGLN("[WiFi] boot connect deferred, waiting for host or 8s");
  }
}

void loop() {
#if WIFI_STANDALONE_TEST
  standaloneLoop();
  return;
#endif

  unsigned long now = millis();

  if (!g_uart_ota_lock) {
    processHostUart();
  }

  /* 驱动在超时状态之后后台重试成功时，仍需通知 CH585 UI。 */
  if (WiFi.status() == WL_CONNECTED &&
      wifi_run_state != WIFI_ST_CONNECTED) {
    wifiOnConnected();
  }

  if (wifi_boot_connect_pending && !wifi_host_cmd_seen && now > 8000) {
    wifi_boot_connect_pending = false;
    DBGLN("[WiFi] boot connect (no host cmd)");
    wifiTryBootConnect();
  }

  /*
   * AP 列表只由 CH585 的 wifi_scan 指令刷新。
   * 自动扫描会在失败状态后立即发送 scanning，覆盖 failed 状态，
   * 并且没有必要为已完成的用户连接再干扰 WiFi 驱动。
   */
  wifiPollConnect();
  if (!wifiIsBusyConnect()) {
    wifiPollScanRequest();
  }
  wifiPollReconnect();

  if (WiFi.status() == WL_CONNECTED) {
    mqttTryConnect();
    mqttClient.loop();
    otaRunPendingJob();
    otaPollBootTimeout();
    if (millis() - last_health > 5000) {
      last_health = millis();
      publishHeart();
    }
  }

  delay(10);
}
