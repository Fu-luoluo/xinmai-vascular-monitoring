#ifndef UI_STRINGS_H
#define UI_STRINGS_H

/* Home kiosk */
#define UI_STR_HOME_TITLE           "外周血管检测"
#define UI_STR_HOME_MEASURE         "开始测量"
#define UI_STR_HOME_PROFILE         "用户档案"
#define UI_STR_HOME_HISTORY         "历史记录"
#define UI_STR_HOME_WIFI            "WiFi设置"
#define UI_STR_HOME_STATUS_FMT      "%s  腕%s 指%s"
/* LV_SYMBOL_OK：须配合 montserrat 字体显示为对勾，勿用中文字库的根号 √ */
#define UI_STR_LINK_MARK_ON           "\xEF\x80\x8C"
#define UI_STR_LINK_MARK_OFF          "--"
#define UI_STR_HOME_PROFILE_GATE    "请先完善档案"
#define UI_STR_HOME_WIFI_OFF        "WiFi--"
#define UI_STR_HOME_BLE_FMT         "腕%s 指%s"

/* Profile */
#define UI_STR_PROFILE_TITLE        "用户档案"
#define UI_STR_PROFILE_BACK         "返回"
#define UI_STR_PROFILE_SAVE         "保存"
#define UI_STR_PROFILE_AGE          "年龄"
#define UI_STR_PROFILE_HEIGHT       "身高"
#define UI_STR_PROFILE_HAND         "手长"
#define UI_STR_PROFILE_HAND_AUTO    "自动"
#define UI_STR_PROFILE_MALE         "男"
#define UI_STR_PROFILE_FEMALE       "女"
#define UI_STR_PROFILE_INCOMPLETE   "未完善"
#define UI_STR_PROFILE_SUMMARY_FMT  "%s%u岁"
#define UI_STR_PROFILE_REF_FMT      "参考%.1f (%.1f-%.1f m/s)"

/* Bottom nav */
#define UI_STR_NAV_HOME             "首页"
#define UI_STR_NAV_MONITOR          "监测"
#define UI_STR_NAV_HISTORY          "历史"

/* Monitor */
#define UI_STR_MONITOR_TITLE        "rd-PWV"
#define UI_STR_MONITOR_DISCLAIMER   "外周趋势·非诊断"
#define UI_STR_MONITOR_REF_FMT      "参考%.1f-%.1f"
#define UI_STR_MEASURE_START        "开始测量"
#define UI_STR_MEASURE_STOP         "停止测量"
#define UI_STR_MEASURE_RUNNING      "测量中"
#define UI_STR_MEASURE_DONE         "测量完成"
#define UI_STR_STATUS_LABEL         "状态"
#define UI_STR_STATUS_IDLE          "--"

/* Result overlay（仅用字库已有字形，不扩字体） */
#define UI_STR_RESULT_TITLE         "血管趋势"
#define UI_STR_RESULT_PWV_FMT       "PWV %d.%01d m/s"
#define UI_STR_RESULT_REF_FMT       "参考%.1f-%.1f m/s"
#define UI_STR_RESULT_NONE_HINT     "暂无PWV"
#define UI_STR_PREFLIGHT_WRIST      "请佩戴腕部传感器"
#define UI_STR_PREFLIGHT_FINGER_LINK "请佩戴指部传感器"
#define UI_STR_PREFLIGHT_PROFILE    "请先完善用户档案"
#define UI_STR_PREFLIGHT_SPO2       "腕血氧偏低"
#define UI_STR_PREFLIGHT_SPO2_FMT   "腕血氧 %u%% 需>=%u"
#define UI_STR_PREFLIGHT_HR         "腕心率未达标"
#define UI_STR_PREFLIGHT_HR_FMT     "腕心率 %u 需 %u-%u"
#define UI_STR_PREFLIGHT_FINGER     "指血氧偏低"
#define UI_STR_PREFLIGHT_FINGER_FMT "指血氧 %u%% 需>=%u"
#define UI_STR_WRIST                "腕部"
#define UI_STR_FINGER               "指部"
#define UI_STR_VITALS_TITLE         "心率血氧"
#define UI_STR_VITALS_HR_FMT        "心率 %u"
#define UI_STR_VITALS_SPO2_FMT      "血氧 %u%%"
#define UI_STR_VITALS_LINE_FMT      "心率 %u    血氧 %u%%"
#define UI_STR_VITALS_NONE          "心率 --    血氧 --"
#define UI_STR_LINK_FMT             "腕%s 指%s"

/* Monitor — PWV quality row (14px) */
#define UI_STR_QUALITY_FMT          "信号质量 %u%%"
#define UI_STR_QUALITY_IDLE         "信号质量 --"
#define UI_STR_QUALITY_CAL          "信号质量 校准中"

/* Monitor — quality gate hints (20px preflight) */
#define UI_STR_PREFLIGHT_QUALITY    "信号质量不足"
#define UI_STR_PREFLIGHT_PI_WRIST   "腕部灌注偏低"
#define UI_STR_PREFLIGHT_PI_FINGER  "指部灌注偏低"
#define UI_STR_PREFLIGHT_PTT_CV     "PTT波动过大"
#define UI_STR_PREFLIGHT_KEEP_STILL "请保持静止"

/* Signal lost / BLE alarm */
#define UI_STR_SIG_TITLE            "传感器脱落"
#define UI_STR_SIG_HINT             "请重新佩戴传感器\n点击开始测量后重试\n即将返回监测页…"
#define UI_STR_BLE_TITLE            "蓝牙连接断开"
#define UI_STR_BLE_HINT             "请检查腕部与指部传感器\n确认开机后重试\n即将返回监测页…"
#define UI_STR_ALARM_WEAK_HINT      "信号较弱，请调整佩戴位置"

/* History */
#define UI_STR_HIST_TITLE           "历史"
#define UI_STR_HIST_TOTAL_FMT       "共 %lu 条"
#define UI_STR_HIST_EMPTY           "暂无记录"
#define UI_STR_HIST_NORFLASH        "存储未就绪"
#define UI_STR_HIST_PAGE_FMT        "第 %lu/%lu 页"
#define UI_STR_HIST_PAGE_EMPTY      "第 -/- 页"
#define UI_STR_HIST_PREV            "上一页"
#define UI_STR_HIST_NEXT            "下一页"

/* WiFi */
#define UI_STR_WIFI_TITLE           "WiFi 设置"
#define UI_STR_WIFI_BACK            "返回首页"
#define UI_STR_WIFI_HINT            "仅支持 2.4GHz WiFi"
#define UI_STR_WIFI_RESCAN          "重新扫描"
#define UI_STR_WIFI_MANUAL          "手动输入"
#define UI_STR_WIFI_FORGET          "忘记网络"
#define UI_STR_WIFI_BLOCK           "请先停止测量"
#define UI_STR_WIFI_KB_CONNECT      "连接"
#define UI_STR_WIFI_KB_CANCEL       "取消"
#define UI_STR_WIFI_SCANNING        "扫描中…"
#define UI_STR_WIFI_CONNECTING_FMT  "正在连接：%s"
#define UI_STR_WIFI_CONNECTED_FMT   "已连接：%s  %s"
#define UI_STR_WIFI_FAILED_FMT      "连接失败：%s"
#define UI_STR_WIFI_NO_CFG          "未配网"
#define UI_STR_WIFI_NO_NET          "未找到网络"
#define UI_STR_WIFI_NO_24G          "未找到 2.4GHz 网络"
#define UI_STR_WIFI_TAP_HINT        "点击网络，输入密码"
#define UI_STR_WIFI_PASS_TITLE      "手动输入密码"
#define UI_STR_WIFI_PASS_NEED_SSID  "请先输入SSID"
#define UI_STR_WIFI_PASS_PH_PASS    "密码"
#define UI_STR_WIFI_ERR_OOM         "内存不足，请重试"
#define UI_STR_WIFI_CHIP_OFF        "WiFi--"
#define UI_STR_WIFI_CHIP_FMT        "WiFi:%s"

#endif /* UI_STRINGS_H */
