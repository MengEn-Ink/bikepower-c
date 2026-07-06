/**
 * 应用状态机（对齐 doc 11 §8.1）
 *
 * 消费按钮/连接事件，驱动调功、二次确认窗口与 LED。
 * 按钮语义：
 *   短按 <500ms        -> 按当前启用数据项调整主目标 -
 *   长按 >=500ms       -> 每 BTN_AUTO_REPEAT_MS 调整主目标 +（仅 WiFi 确认阈值前）
 *   长按 >=2000ms 释放 -> 回滚本次自动调功，进入确认窗口（LED 按配置切换）
 *   确认窗口内再按      -> 触发 WiFi 配网（阶段5接入，经 wifi_trigger 回调）
 *   确认窗口 3s 超时    -> 取消，恢复 BLE_ACTIVE
 */
#include "app_state.h"
#include "led.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bikepower_config.h"
#include "button_logic.h"

static const char *TAG = "STATE";

static power_engine_t *s_engine;
static app_state_t s_state = ST_BOOTING;
static bool s_connected = false;

static control_target_t s_press_target = CONTROL_TARGET_NONE;
static int s_value_before_press = 0;   // 本次按下前目标值（用于长按回滚）
static bool s_auto_adjusted = false;   // 本次按住是否发生自动调功
static uint32_t s_confirm_start = 0;   // 确认窗口开始时间

static void (*s_wifi_trigger)(void) = NULL;
static void (*s_wifi_exit)(void) = NULL;

/**
 * 获取当前单调时间的毫秒值。
 * 基于 esp_timer 返回应用状态机使用的时间基准。
 * 无参数。
 * 返回值：当前毫秒时间戳。
 * 特殊行为：用于确认窗口和按压流程的相对计时，不依赖系统时钟。
 */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void apply_led_scene(led_scene_t scene)
{
    if (!s_engine) return;
    led_apply_scene(scene, &s_engine->led);
}

/**
 * 根据当前连接状态刷新空闲态 LED 灯效。
 * 未连接/已连接灯效由 led_config 对应场景决定。
 * 无参数。
 * 返回值：无。
 * 特殊行为：只应用于 BLE 活动态，不负责 WiFi 确认和配网中的灯效。
 */
static void update_idle_led(void)
{
    apply_led_scene(s_connected ? LED_SCENE_CONNECTED : LED_SCENE_WAIT_CONN);
}

void app_state_refresh_led(void)
{
    if (s_state == ST_ERROR_RECOVERY) {
        apply_led_scene(LED_SCENE_ERROR);
    } else if (s_state == ST_WIFI_CONFIRM) {
        apply_led_scene(LED_SCENE_WIFI_CONFIRM);
    } else if (s_state == ST_WIFI_CONFIG) {
        apply_led_scene(LED_SCENE_WIFI_CONFIG);
    } else {
        update_idle_led();
    }
}

/**
 * 注册 WiFi 配网触发回调。
 * 保存状态机进入配网态时要调用的外部切换函数。
 * cb：配网触发回调，可为 NULL。
 * 返回值：无。
 * 特殊行为：后续注册会直接覆盖之前的回调。
 */
void app_state_set_wifi_trigger(void (*cb)(void))
{
    s_wifi_trigger = cb;
}

/**
 * 注册 WiFi 配网结束回调。
 * 保存状态机退出配网态时要调用的外部清理函数（关 HTTP + 关 WiFi）。
 * cb：配网结束回调，可为 NULL。
 * 返回值：无。
 * 特殊行为：后续注册会直接覆盖之前的回调；回调在主任务上下文执行。
 */
void app_state_set_wifi_exit(void (*cb)(void))
{
    s_wifi_exit = cb;
}

/**
 * 初始化应用状态机。
 * 绑定领域引擎、清空连接状态并进入 BLE 活动态。
 * engine：应用共享的功率引擎实例。
 * 返回值：无。
 * 特殊行为：初始化完成后会立刻刷新 LED 到空闲灯效。
 */
void app_state_init(power_engine_t *engine)
{
    s_engine = engine;
    s_state = ST_BLE_ACTIVE;
    s_connected = false;
    update_idle_led();
    ESP_LOGI(TAG, "状态机启动 -> ST_BLE_ACTIVE");
}

/**
 * 获取当前状态机状态。
 * 返回当前应用层高层状态。
 * 无参数。
 * 返回值：当前状态枚举值。
 * 特殊行为：仅返回内部状态快照，不触发任何状态迁移。
 */
app_state_t app_state_get(void)
{
    return s_state;
}

/**
 * 进入错误恢复态。
 * BLE 启动失败或其他可恢复错误时调用，LED 切到异常场景。
 * 无参数。
 * 返回值：无。
 * 特殊行为：只改变状态和 LED，不复位或重启。
 */
void app_state_set_error(void)
{
    s_state = ST_ERROR_RECOVERY;
    s_connected = false;
    apply_led_scene(LED_SCENE_ERROR);
    ESP_LOGW(TAG, "进入错误恢复态 ST_ERROR_RECOVERY");
}

/**
 * 从错误恢复态回到 BLE 活动态。
 * BLE 重试成功后调用，恢复正常运行的 LED 和状态。
 * 无参数。
 * 返回值：无。
 * 特殊行为：仅当当前处于 ST_ERROR_RECOVERY 时才会迁移。
 */
void app_state_clear_error(void)
{
    if (s_state != ST_ERROR_RECOVERY) return;
    s_state = ST_BLE_ACTIVE;
    update_idle_led();
    ESP_LOGI(TAG, "错误恢复 -> ST_BLE_ACTIVE");
}

/**
 * 进入 WiFi 二次确认窗口。
 * 长按达到阈值后回滚本次自动调功，并切换到等待二次确认的状态。
 * 无参数。
 * 返回值：无。
 * 特殊行为：会将 LED 切到二次确认场景，并记录确认窗口起始时间。
 */
static void enter_confirm_window(void)
{
    if (s_auto_adjusted) {
        if (power_engine_set_control_value(s_engine, s_press_target, s_value_before_press)) {
            ESP_LOGI(TAG, "长按结束，回滚按钮目标 %d 到 %d", (int)s_press_target, s_value_before_press);
        } else {
            ESP_LOGW(TAG, "长按结束，按钮目标 %d 回滚被拒绝（目标 %d）", (int)s_press_target, s_value_before_press);
        }
    }
    s_state = ST_WIFI_CONFIRM;
    s_confirm_start = now_ms();
    apply_led_scene(LED_SCENE_WIFI_CONFIRM);
    ESP_LOGI(TAG, "长按2秒，LED切到二次确认场景，等待二次确认...");
}

/**
 * 执行二次确认后的 WiFi 配网触发。
 * 若已注册回调则切到 WiFi 配网态并交给射频控制模块执行，否则取消本次配网请求。
 * 无参数。
 * 返回值：无。
 * 特殊行为：未注册回调时会恢复到 BLE 活动态而不是停留在确认态。
 */
static void trigger_wifi(void)
{
    ESP_LOGI(TAG, "二次确认，进入WiFi配网模式");
    if (s_wifi_trigger) {
        s_state = ST_WIFI_CONFIG;
        apply_led_scene(LED_SCENE_WIFI_CONFIG);
        s_wifi_trigger();
    } else {
        ESP_LOGW(TAG, "WiFi 配网未就绪（阶段5接入），取消");
        s_state = ST_BLE_ACTIVE;
        update_idle_led();
    }
}

#ifdef PORTAL_TEST_AUTO_CONFIG
void app_state_enter_wifi_config_for_test(void)
{
    ESP_LOGW(TAG, "[配网测试] 自动触发配网（PORTAL_TEST_AUTO_CONFIG）");
    trigger_wifi();
}
#endif

/**
 * 处理一个应用层事件。
 * 根据按钮和连接事件驱动状态迁移、功率调整与配网确认逻辑。
 * e：待处理事件。
 * 返回值：无。
 * 特殊行为：默认假定由主循环串行调用，因此内部状态更新未做额外并发保护。
 */
void app_state_handle_event(const app_evt_t *e)
{
    switch (e->type) {
    case APP_EVT_CONN_CHANGED:
        s_connected = (e->data != 0);
        if (!s_connected && power_engine_clear_erg_target(s_engine)) {
            ESP_LOGI(TAG, "BLE 断开，退出 ERG 模式并恢复手动调功");
        }
        if (s_state == ST_BLE_ACTIVE) update_idle_led();
        ESP_LOGI(TAG, "连接状态变化 connected=%d", s_connected);
        break;

    case APP_EVT_ERG_TARGET:
        power_engine_set_erg_target(s_engine, e->data);
        ESP_LOGI(TAG, "ERG 目标功率 %dW", (int)e->data);
        break;

    case APP_EVT_ERG_CLEAR:
        if (power_engine_clear_erg_target(s_engine)) {
            ESP_LOGI(TAG, "退出 ERG 模式，恢复手动调功（reason=%d）", (int)e->data);
        }
        break;

    case APP_EVT_WIFI_DONE:
        // 配网结束（保存成功或超时）：先关闭 WiFi 资源（HTTP + SoftAP），再回到 BLE 活动态。
        // BLE 全程未停，无需重激活；LED 直接根据当前连接状态刷新。
        if (s_state == ST_WIFI_CONFIG) {
            if (s_wifi_exit) s_wifi_exit();   // 在主任务上下文串行释放 WiFi 资源
            s_state = ST_BLE_ACTIVE;
            update_idle_led();
            ESP_LOGI(TAG, "WiFi 配网结束 -> ST_BLE_ACTIVE");
        }
        break;

    case APP_EVT_BTN_DOWN:
        if (s_state == ST_WIFI_CONFIRM) {
            trigger_wifi();
            break;
        }
        s_press_target = power_engine_button_target(s_engine);
        s_value_before_press = power_engine_control_value(s_engine, s_press_target);
        s_auto_adjusted = false;
        break;

    case APP_EVT_BTN_REPEAT:
        if (s_state != ST_BLE_ACTIVE) break;
        if (button_repeat_should_adjust((uint32_t)e->data, WIFI_BTN_HOLD_MS)) {
            power_engine_adjust_control_target(s_engine, s_press_target, 1);
            s_auto_adjusted = true;
        }
        break;

    case APP_EVT_BTN_UP:
        if (s_state == ST_WIFI_CONFIG) break;        // 二次确认后的释放，忽略
        if (s_state == ST_WIFI_CONFIRM) break;       // 已在 DOWN 处理
        if (s_state != ST_BLE_ACTIVE) break;
        {
            btn_release_action_t act = button_classify_release((uint32_t)e->data, SHORT_PRESS_MS, WIFI_BTN_HOLD_MS);
            if (act == BTN_RELEASE_SHORT) {
                int v = power_engine_adjust_control_target(s_engine, s_press_target, -1);
                ESP_LOGI(TAG, "短按 调整目标=%d -> %d", (int)s_press_target, v);
            } else if (act == BTN_RELEASE_LONG) {
                enter_confirm_window();
            }
            // BTN_RELEASE_MID：500ms~2000ms 长按调功已在 REPEAT 应用，释放不额外动作
        }
        break;
    }
}

/**
 * 推进状态机的时间相关逻辑。
 * 当前只负责处理 WiFi 二次确认窗口超时。
 * 无参数。
 * 返回值：无。
 * 特殊行为：确认窗口超时后会恢复 BLE 活动态并刷新 LED。
 */
void app_state_tick(void)
{
    if (s_state == ST_WIFI_CONFIRM &&
        button_confirm_window_expired(now_ms() - s_confirm_start, CONFIRM_WINDOW_MS)) {
        s_state = ST_BLE_ACTIVE;
        update_idle_led();
        ESP_LOGI(TAG, "确认窗口超时，取消进入配网模式");
    }
}
