# 基于 i.MX6ULL 的嵌入式 Linux 网络设备管理终端

> 在 i.MX6ULL 开发板上实现一个 TCP 服务器，PC 端通过网络发送文本命令，
> 远程读取设备运行状态并控制硬件。

```text
Ubuntu 上位机（client）  ────TCP 8888────>  i.MX6ULL（server）
                         <────文本应答────
```

## 能力一览

| 客户端发送 | 服务器回复 | 数据来源 | 用到的技术 |
| --- | --- | --- | --- |
| `PING` | `PONG` | — | 协议连通性测试 |
| `GET_UPTIME` | `12804.83 s` | `/proc/uptime` | 文件 IO |
| `GET_MEM` | `MEM: total=494MB / free=328MB / used=33%` | `/proc/meminfo` | 文本解析 |
| `GET_CPU` | `CPU: 2.9%` | `/proc/stat` | **两次采样做差** |
| `GET_IP` | `IP:192.168.137.125 MAC:b8:ae:1d:01:00:00` | 内核网络栈 | `ioctl` |
| `GET_TEMP` | `TEMP:34.043000 C` | `/sys/class/thermal/` | sysfs |
| `LED_ON` | `LED_ON` | `/sys/class/leds/red/` | **写文件控制硬件** |
| `LED_OFF` | `LED_OFF` | 同上 | 同上 |
| `quit` | （客户端本地退出） | — | — |

## 快速开始

### 本机验证（不需要开发板）

```bash
make                      # 或：gcc server.c -o build/server && gcc client.c -o build/client

./build/server            # 终端 1
./build/client            # 终端 2（SERVER_IP 改为 127.0.0.1）
```

### 交叉编译到 i.MX6ULL

```bash
arm-linux-gnueabihf-gcc server.c -o server_arm
scp server_arm root@192.168.137.125:/home/root/
```

板子串口终端：

```bash
chmod +x server_arm && ./server_arm
```

Ubuntu 上运行客户端（`SERVER_IP` 填板子 IP）即可。

## 目录结构

```text
.
├── server.c        # TCP 服务器，命令分发 + 系统信息采集 + LED 控制
├── client.c        # TCP 客户端，交互式命令行
├── Makefile        # 一键编译
├── build/          # 编译产物（已被 .gitignore 忽略）
├── practice/       # 学习期间的练习代码
└── README.md
```

## 通信协议

简单文本协议：**一问一答，长连接**。

```text
客户端                         服务器
PING        --------------->
            <---------------    PONG
GET_CPU     --------------->
            <---------------    CPU: 2.9%
```

- 传输层：TCP，端口 `8888`
- 应用层：纯文本命令，服务端用 `strcmp` 分发
- 客户端输入 `quit` 主动断开

## 三条获取系统信息的路线

这是本项目最核心的知识结构：

| 路线 | 说明 | 本项目命令 |
| --- | --- | --- |
| **procfs** | 内核把系统状态导出成 `/proc` 下的"文件" | `GET_UPTIME` / `GET_MEM` / `GET_CPU` |
| **ioctl** | 向内核网络协议栈发请求拿信息 | `GET_IP` / `GET_MAC` |
| **sysfs** | `/sys` 暴露设备与驱动的属性，可读可写 | `GET_TEMP`（读）/ `LED_ON`（写） |

## 关键实现笔记

### CPU 占用率为什么必须两次采样

`/proc/stat` 给出的是**开机至今的累计时间片**（jiffies），单次读数只能得到"开机以来的平均占用率"。
正确做法是取两个时刻的快照做差：

```text
占用率 = (Δtotal - Δidle) * 100 / Δtotal
```

几个容易搞错的点：

- 分母 `Δtotal` **必须包含 idle**，否则结果恒为 100%
- `guest` / `guest_nice` 已包含在 `user` / `nice` 中，重复累加会虚高
- `CONFIG_HZ=100`（1 jiffy = 10ms），采样窗口越短，台阶效应越明显：
  300ms 窗口只有约 31 个 tick，分辨率为 3.2%；1s 窗口约 103 个 tick，分辨率约 1%

### 写 sysfs 控制 LED 的两个坑

1. LED 若有 `trigger`（如 `heartbeat`、`mmc0`），**写 brightness 无效**，需先写 `none` 到 trigger
2. `fprintf` 只是写进 stdio 缓冲区，**必须 `fclose()`** 才会真正生效，灯才会亮

### 其他

- `GET_IP` 用完的 socket 必须 `close(fd)`，否则每次调用泄漏一个文件描述符
- `recv()` 用 `sizeof(buf)-1` 限制长度并手动补 `\0`；`send()` 用 `strlen()` 只发有效字节
- 用 `snprintf` 代替 `strcat` / `sprintf`，避免缓冲区溢出

## 运行环境

| 项目 | 值 |
| --- | --- |
| 开发板 | 正点原子 i.MX6ULL（Cortex-A7 单核） |
| 根文件系统 | Buildroot |
| 网卡 / IP | `eth0` / `192.168.137.125` |
| MAC | `b8:ae:1d:01:00:00` |
| 温度传感器 | `imx_thermal_zone`（片内） |
| LED | `/sys/class/leds/red` |

## 后续路线

- [ ] 多线程服务器（`pthread`）：支持多客户端并发 + 后台持续采样 CPU
- [ ] 互斥锁保护 LED（多客户端同时操作硬件的资源竞争场景）
- [ ] 自定义应用层数据包（帧头 + 长度 + 校验）
- [ ] Qt 图形化上位机
- [ ] ESP32-CAM 视频流接入（GStreamer）

## 已知待改进

- `set_led()` 末尾缺少 `return 0`，返回值不确定，会导致上层的成功判断不可靠
- 单线程结构：一次只能服务一个客户端，`GET_CPU` 采样期间会阻塞所有请求
- 客户端无法感知服务器断开（阻塞在 `fgets`，需引入 `select` / `poll`）
- 各系统调用返回值未全部检查（`listen`、`send`、`fscanf` 等）
