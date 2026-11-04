# WiFi 核心门户与真实功率桥接

当前工作区采用 schema v13。静态资源来自 `main/web/index.html`、`app.css`、`app.js`，经 `scripts/inline_static.py` 压缩内联，设备不加载外部字体、图片或脚本。仅保留明亮主题；内联总量上限 110 KiB。模板管理和 LED 配置留在电脑端工具。

## 生命周期

deadline/closing/done/restart 由 `s_lock` 保护；done 锁外调用，inflight 防重入，
generation 隔离旧会话回写，拒收时下一节拍重试。重启请求锁内取清。

WiFi 启停统一经过 `radio_ctrl`，普通配网保持 BLE peripheral 在线。默认 300 秒，仅用户点击续期调用 `/api/renew` 重置截止时间；读状态、保存、扫描和测试都不延长。

保存并关闭在事务成功后进入 5 秒收尾窗口，done 回调投递 `APP_EVT_WIFI_DONE`，主循环关闭 HTTP、DNS 和 SoftAP。普通共存模式不重启；异常初始化降级遵循 radio_ctrl 原有恢复流程。

## HTTP 接口

13 个业务路由、10 个 Captive Portal 探测路由和 404 重定向兜底。

| 方法 | 路径 | 内容 |
|---|---|---|
| GET | `/` | 压缩内联页面 |
| GET | `/favicon.ico` | 204 |
| GET | `/.well-known/captive-portal` | RFC 8910 JSON |
| GET | `/api/state` | config、有效 identity、revision、预设、固件详情与桥接状态 |
| GET | `/api/runtime` | 实际输出、桥接状态、保存事务结果、跑步累计 |
| GET | `/api/time` | 剩余秒数纯文本 |
| POST | `/api/renew` | 显式续期，返回剩余秒数纯文本 |
| GET | `/api/mem_stats` | 当前和历史最小空闲堆 |
| POST | `/api/config` | 提交配置事务 |
| POST | `/api/close-wifi` | 提交保存及关闭请求 |
| GET | `/api/bridge/scan` | 本轮 CPS 候选与扫描状态 |
| POST | `/api/bridge/scan` | 启动或停止扫描 |
| POST | `/api/bridge/control` | test、stop、resume |

Captive 探测返回 `303 See Other`、`Location: /` 和短文本 body。DHCP Option 114 指向 `/.well-known/captive-portal`。

## 配置事务

HTTP 读取受保护的配置快照，提交包含 revision 的草稿到应用层。主循环复核 revision、先持久化 NVS，成功后才应用运行配置及有效 DIS 覆盖。请求线程不直接操作引擎，不等待 BLE 连接或 NVS 写入。

POST 成功仅代表已接收，返回 transaction；前端轮询 runtime 的 transaction、pending、result、revision 确认。失败保留原运行状态与浏览器草稿。BLE 与 HTTP 共用配置修订号，避免并发写入覆盖。

schema v13 追加桥接来源、绑定地址及类型、校正和名称。NVS 使用显式小端 bridge blob，保留 v11→v12→v13 迁移；旧配置默认模拟且无绑定。BLE 配置保留原五组布局，Bridge 作为第六组；电脑工具对旧固件跳过缺少的组，对新固件保存时保留桥接原始配置。

## 桥接流程

默认模拟 → 用户选择真实功率计 → 主动扫描 CPS → 临时连接与测试 → 两份有效通知后展开配置 → 保存后切换实际输出。

扫描允许无名 CPS 设备；绑定使用地址和地址类型，候选编号包含扫描代次。临时测试最长 30 秒，不改变当前输出或 NVS；关闭门户会结束临时扫描和测试，已经保存运行的桥接可继续工作。

`ble_power_bridge` 维护静态会话，读取 GAP 名称和 DIS 六字段并确认 CPS CCCD 订阅成功。回调只解析或更新小快照，主循环 tick 负责超时和重连。3 秒数据过期归零，10 秒无有效通知主动断开，60 秒窗口按 1、2、4、8 秒退避重连。

只转发真实功率及同源曲柄踏频；无曲柄时不补模拟踏频。固定偏移 ±200W，比例 50.0–150.0%；0W 始终为 0W，先校正再夹紧到 0–2000W。桥接拒绝 ERG 目标。首次测试默认关闭模拟心率，用户可自行打开。

复制设备信息只覆盖读取成功且非空的 DIS 字段。名称可使用功率计名称、原开发板名称或自定义名称，最多 23 UTF-8 字节。

## 前端与验证

HTTP 服务任务独占 8192 字节 JSON 共享区；state/runtime/scan 命名容量分别为
8192/4096/3072。配置序列化复用同一区。容量不足返回合法错误 JSON。
生产 cJSON 容量与超时回归入口为 `python3 tests/test_portal_runtime.py`。

草稿、已应用配置和实际输出分别管理。轮询不抢输入焦点；按钮居中、输入字号适配手机、弹窗支持键盘焦点和 Escape，动画遵循 reduced-motion。

`tests/mock_server.py` 提供同构数值配置和事务 API。`tests/portal_bridge_browser.py` 用生产压缩页面验证 320/390/1440 宽度、保存失败、桥接校正、0W、过期、DIS 复制、心率、跑步和退出。

桥接领域测试位于 `test/test_power_bridge.c`；`tests/test_bridge_session.py` 以 ASan/UBSan 执行生产上游会话和扫描代码。真机测试及未覆盖项见本轮实施报告，离线替身不能代替真实功率计共存验收。
