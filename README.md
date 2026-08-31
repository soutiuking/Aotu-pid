# DOME PID — 双机 PID 控制系统（ESP32-S3 + STM32F103）

ESP32-S3 作为上位机（Wi-Fi + Web 配置界面），STM32F103C8T6 作为下位机（实时 PID 控制回路），两者通过 UART 自定义帧协议通信。手机/电脑连上 Wi-Fi 打开网页即可：实时查看回路曲线、在线整定 PID 参数、下发控制命令，参数掉电保存。

## 系统架构

```
┌─────────────────────────────┐        UART 115200        ┌──────────────────────────────┐
│  ESP32-S3 (上位机)          │  TX17/RX18, 自定义帧协议   │  STM32F103C8T6 (下位机)      │
│                             │ ────────────────────────► │                              │
│  • Wi-Fi STA / AP 配网      │   0xAA55 帧头 + CRC16     │  • 3 路 PID 回路 (温/速/位)   │
│  • HTTP + WebSocket 服务    │ ◄──────────────────────── │  • 微分先行/抗饱和/死区/滤波  │
│  • Web 前端 (内嵌单页)      │                           │  • 参数 Flash 存储           │
│  • NVS 参数持久化           │                           │  • OLED 显示 (自动轮显)      │
│  • 通信看门狗 / 请求重试     │                           │  • 被控对象仿真 (一期)       │
└─────────────────────────────┘                           └──────────────────────────────┘
```

## 目录结构（仓库只管理核心代码）

```
aotu pid esp32/          ESP32-S3 上位机 (ESP-IDF 工程)
├── main/
│   ├── app_main.cpp         入口与任务划分 (协议/轮询/推送任务)
│   ├── wifi_manager.*       Wi-Fi STA 优先, 失败回退 AP 配网
│   ├── web_server.*         HTTP + WebSocket 服务
│   ├── web/index.html       Web 前端单页 (编译时嵌入固件)
│   ├── protocol_client.*    请求匹配/超时/重试状态机
│   ├── uart_manager.*       UART 驱动封装
│   ├── pid_manager.*        PID 参数管理 (Web 请求 → 协议命令)
│   ├── autotune_manager.*   自整定生命周期接口 (一期占位, 无算法)
│   ├── device_status.*      设备状态快照 (WebSocket 节流推送)
│   ├── protocol/            协议解析/组帧/CRC (与 STM32 端同源副本)
│   └── storage/             NVS 配置持久化
├── sdkconfig.defaults       默认配置 (sdkconfig 由它生成, 不入库)
└── CMakeLists.txt

aotu pid stm32/          STM32F103 下位机 (STM32CubeMX + Keil MDK 工程)
├── App/                     应用层
│   ├── pid_controller.c     位置式 PID 核心 (微分先行/积分限幅/抗饱和/死区/一阶滤波/无扰切换)
│   ├── control_task.c       回路状态机 + 10ms 节拍调度 + 通信看门狗 (10s 无有效帧进安全态)
│   ├── pid_parameter.c      参数集管理 (协议同步/默认值/掉电保存)
│   ├── plant_sim.c          一阶被控对象仿真 (K=1.0, T=0.8s, 含测量噪声)
│   └── app_config.h         集中配置 (节拍/看门狗/安全范围/仿真开关)
├── Protocol/                帧协议: 解析器/处理器/CRC16 (与 ESP32 端同源副本)
├── Storage/                 参数 Flash 存储 (原子写入/擦写保护)
├── Drivers/bsp/             板级驱动: UART/定时器/Flash/OLED
├── Core/Src/main.c          ⚠ CubeMX 生成但含 USER CODE 集成代码, 必须保留
├── Core/Src/stm32f1xx_it.c  ⚠ 同上 (USART1 中断转发到 BSP)
├── Core/ 的 oled.c/h、oledfont.c/h、bmp.h   用户自编 OLED 驱动
└── aotu pid stm32.ioc       CubeMX 工程定义 (恢复生成文件全靠它)

以下内容不入库, 由工具链重新生成 (方法见下文):
  • Drivers/CMSIS 与 Drivers/STM32F1xx_HAL_Driver   ST 官方库, 约 68MB
  • Core/Src 的 dma/gpio/i2c/usart/msp/system 等    CubeMX 生成, USER CODE 区为空
  • Core/Inc 的 main/dma/gpio/i2c/usart/it/hal_conf 头文件
  • MDK-ARM 整个目录 (uvprojx/uvoptx/startup 等)     CubeMX 生成的 Keil 工程
  • ESP32 端: build/、sdkconfig、managed_components   ESP-IDF 构建产物
  • 所有编译产物 (*.hex/*.map/*.axf/*.log/Keil 输出目录)
```

## 环境准备

| 目标板 | 需要安装 | 版本要求 |
|--------|---------|---------|
| ESP32-S3 | [ESP-IDF](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/) | 本项目基于 **v5.5.4** 开发 |
| STM32F103 | [STM32CubeMX](https://www.st.com/en/development-tools/stm32cubemx.html) + [Keil MDK-ARM](https://www.keil.com/mdk5) | Keil 需含 STM32F1 器件支持包 |

## 如何构建

### 1. STM32F103 下位机

**第一步：CubeMX 重新生成工程**（同时补齐官方库、生成文件和 Keil 工程）：

1. 用 STM32CubeMX 打开 `aotu pid stm32/aotu pid stm32.ioc`；
2. 确认 Toolchain 选 **MDK-ARM V5**；
3. 点 GENERATE CODE。

生成会自动恢复：`Drivers/CMSIS`、`Drivers/STM32F1xx_HAL_Driver`、`Core/Src` 与 `Core/Inc` 中被排除的生成文件、`MDK-ARM/` 整个 Keil 工程（含 startup 文件、宏 `USE_HAL_DRIVER,STM32F103xB`、C99 模式）。

> ⚠️ `Core/Src/main.c` 和 `stm32f1xx_it.c` 已在仓库中（含 USER CODE 集成代码），CubeMX 重新生成时会保留其 USER CODE 区内容，不要删除这两个文件后重新生成，否则会丢失启动集成代码（模块初始化与主循环轮询、USART1 中断转发）。

**第二步：把自有源码加回 Keil 工程**（CubeMX 生成的工程不含它们）→ 见下文「如何添加文件」。

**第三步**：打开 `MDK-ARM/aotu pid stm32.uvprojx` → F7 编译 → 烧录。

### 2. ESP32-S3 上位机

```bash
cd "aotu pid esp32"
idf.py set-target esp32s3
idf.py build flash monitor
```

首次编译自动由 `sdkconfig.defaults` 生成 `sdkconfig`，无需手动配置。

### 3. 双机接线

```
ESP32 GPIO17 (TX) ──── STM32 USART RX
ESP32 GPIO18 (RX) ──── STM32 USART TX
GND ───────────────── GND (必须共地)
```

串口参数 115200-8N1。

## 如何添加文件

### STM32 端（Keil）

CubeMX 重新生成后，工程里只有官方库和 Core 生成文件，需手动加回自有源码：

1. **添加源文件**：Project 窗口右键 Target → Add Group，建立 4 个组并添加对应文件：

   | 组名 | 添加的文件 |
   |------|-----------|
   | App | `App/pid_controller.c`、`pid_parameter.c`、`control_task.c`、`device_state.c`、`plant_sim.c` |
   | Protocol | `Protocol/protocol_parser.c`、`protocol_handler.c`、`protocol_crc.c` |
   | Storage | `Storage/parameter_storage.c` |
   | BSP | `Drivers/bsp/bsp_uart.c`、`bsp_flash.c`、`bsp_display.c`、`bsp_timer.c` |

   再在 CubeMX 生成的 `Application/User/Core` 组里补上用户自编文件：`Core/Src/oled.c`、`Core/Src/oledfont.c`（`main.c`、`stm32f1xx_it.c` 已在组内，勿动）。

2. **添加包含路径**：Options for Target → C/C++ → Include Paths，在 CubeMX 默认路径后追加：
   `../App;../Protocol;../Storage;../Drivers/bsp`

3. **新增自己的文件**时同理：源文件加进对应组、头文件所在目录加进 Include Paths。头文件建议放对应模块目录（如 `App/`），不要放 `Core/Inc`（会被 CubeMX 管理混淆）。

### ESP32 端（ESP-IDF）

编辑 `main/CMakeLists.txt` 的 `idf_component_register(...)`：

- 新源文件 → 加到 `SRCS` 列表；
- 新头文件目录 → 加到 `INCLUDE_DIRS`；
- 新嵌入资源（如网页）→ 加到 `EMBED_FILES`。

改完 `idf.py build` 即生效。

## 如何使用（典型操作流程）

1. **连接**：手机/电脑连接 ESP32 的 Wi-Fi。
   - STA 模式：连家里路由器，访问 ESP32 分到的 IP（路由器后台可查）；
   - AP 配网模式：热点 SSID 前缀 `DOME-PID`，默认密码 `12345678`，连上后浏览器访问 `192.168.4.1`。
2. **确认在线**：页面顶部「设备连接」卡片显示 STM32 在线、协议版本、最后通信时间。可在页面上直接填入家里 Wi-Fi 的 SSID/密码让 ESP32 联网。
3. **整定参数**：「PID 参数」区选择回路（0 温度 / 1 速度 / 2 位置）→ 读取参数 → 修改 Kp/Ki/Kd、目标值、输出限幅、死区、滤波系数、控制方向 → 写入参数。
4. **运行**：模式切到「自动」，启动 PID；「实时曲线」区以 WebSocket 节流推送（200ms）绘制目标值/反馈/输出曲线。
5. **保存**：确认效果后「保存到 Flash」，下电不丢；也可「从 Flash 加载」「恢复默认值」「校验参数」。
6. **安全**：任何时候可用「紧急停止 / 停止 PID / 关闭输出」；STM32 侧通信看门狗 10 秒收不到有效帧会自动进入安全状态。整定页有开始/暂停/停止/应用结果等入口（一期暂无实际算法，见局限性）。

## 通信协议（简述）

帧格式（多字节数据小端）：

```
AA 55 | VER | CMD | SEQ | LEN_L LEN_H | DATA[N] | CRC16_L CRC16_H | 0D 0A
```

- CRC16：Modbus 多项式 0xA001，初值 0xFFFF，覆盖 VER 至 DATA 末尾；
- 应答 CMD = 请求 CMD | 0x80，应答数据区前 2 字节固定为 STATUS + ERROR_CODE；
- 完整命令表见 `aotu pid stm32/Protocol/protocol_def.h`。

> ⚠️ 协议文件在两端各存一份（`stm32/Protocol/` 与 `esp32/main/protocol/`），必须字节级一致，修改时双端同步。

## 现有局限性

1. **被控对象是软件仿真，不是真实硬件。** `App/app_config.h` 中 `CFG_PLANT_SIMULATION = 1`，当前控制对象是一阶惯性环节仿真（增益 1.0、时间常数 0.8s、含测量噪声）。接真实执行机构/传感器需将该开关置 0 并实现 `plant_sim.c` 中的钩子，目前未实现。
2. **自整定是一期占位，没有实际算法。** `autotune_manager` 只定义了整定生命周期接口（启动/状态/取消）和 Web 交互，`Phase 1 intentionally contains no tuning algorithm`，点击整定不会产生任何参数。
3. **克隆后不能直接编译，必须先过一遍工具链生成。** STM32 端需按「如何构建」用 CubeMX 重新生成官方库、生成文件和 Keil 工程，并手动把自有源码加回工程；ESP32 端开箱即编，无此问题。
4. **协议定义双端副本，无一致性校验。** `protocol_def.h` 等协议文件在两端各存一份，仅靠注释约定字节级一致；改协议漏改一端不会有构建期报错，只能靠联调发现。后续可抽成共享目录或子模块。
5. **Wi-Fi 默认凭据硬编码。** AP 密码 `12345678` 写死在 `esp32/main/app_config.h`，无 Web 修改入口；STA 的 SSID/密码存 NVS（可在页面配置），但 AP 密码不可改。
6. **STM32 仅支持 Keil MDK 构建。** 没有 GCC/Makefile/CMake 工程，Linux/macOS 无法编译下位机。
7. **`main.c` 与 `stm32f1xx_it.c` 是生成文件但必须入库。** 二者的 USER CODE 区含系统集成代码（模块初始化/主循环、USART1 中断转发），CubeMX 只有在文件存在时才保留 USER CODE 区——若删除后重新生成，代码会静默丢失且编译仍通过，故障很难排查。
8. **Web 前端是单个内嵌 index.html，无构建流程。** 500+ 行单页直接嵌入固件，无压缩、无框架、无自动化测试；页面继续膨胀会拖慢固件编译。
9. **源码换行符 CRLF/LF 混用、个别文件带 UTF-8 BOM。** 已加 `.gitattributes` 约束二进制与文本属性，但存量文件混用仍在，diff 偶有噪声。
10. **安全保护按仿真场景标定。** 反馈安全范围 ±150、输出限幅 ±100 等默认值针对仿真对象设定，接真实硬件前必须按实际执行器重新标定。

## License

[The Unlicense](LICENSE) — 贡献至公共领域，可任意复制、修改、商用、再分发，无任何限制与署名要求。
