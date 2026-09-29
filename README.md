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
make

./build/server            # 终端 1
./build/client            # 终端 2（SERVER_IP 改为 127.0.0.1）
```

> 手动编译记得加 `-lpthread`：`gcc -Wall -Wextra server.c -o build/server -lpthread`

### 交叉编译到 i.MX6ULL

```bash
arm-linux-gnueabihf-gcc server.c -o server_arm -lpthread
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

## 并发模型

服务端采用「主线程 accept + 每客户端一线程 + 一个常驻后台线程」的结构：

```text
main 线程 ── accept 循环 ──┬──> client_handler 线程 A ──> 服务客户端 A
                          ├──> client_handler 线程 B ──> 服务客户端 B
                          └──> client_handler 线程 C ──> 服务客户端 C

cpu_monitor 线程（常驻）：每 1s 采样 ──写──>┐
                                          g_cpu_usage（受 cpu_mutex 保护）
所有 client_handler 线程：GET_CPU ──读──────┘
```

带来的三个收益：

| 改造前 | 改造后 |
| --- | --- |
| 一次只能服务一个客户端 | 多客户端并发，互不干扰 |
| 客户端断开后服务器随即退出 | 服务器长期驻留 |
| `GET_CPU` 采样期间阻塞所有人 1 秒 | 后台线程持续采样，命令**秒回** |

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

### 多线程的三个易错点

**1. `client_fd` 必须用 `malloc` 传递，不能传局部变量地址**

```c
pthread_create(&tid, NULL, client_handler, &client_fd);   /* 错 */
```

主线程马上会回到循环顶部覆盖 `client_fd`，新线程读到的可能是别人的 fd，导致**客户端串线**。正确做法是每次 `malloc` 一块内存，线程里取完值立刻 `free`。

同时 `buf` / `send_buf` 必须声明在线程函数内部（栈上），保证每个线程各有一份。

**2. 只要「一个写者 + 至少一个读者」，读写两端都要加同一把锁**

`double` 是 8 字节，Cortex-A7 是 32 位，一次读写需要两条指令。写线程刚写完前 4 字节时被读线程打断，读到的会是「半个新值 + 半个旧值」——偶发出现离谱数据，且难以复现。`volatile` 解决不了这个问题，只能靠锁。

另外采用**方案 A（锁加在被调函数内部）**：锁的范围越小越好，默认不把整段流程锁住，只在真正出现「读-改-写」组合（如 `LED_TOGGLE`）时才外移到调用处。

**3. 后台线程的异常路径必须 `sleep`，且绝不能退出**

失败后若直接 `continue` 会跳过中间的 `usleep`，形成每秒十几万次的忙循环——**一个监控 CPU 的线程自己把 CPU 跑满**。同时后台线程一旦 `return`，`g_cpu_usage` 就永远停在最后一个值，表现为「程序不崩、没有报错，但数据死了」。此外失败时**不覆盖**全局变量：旧数据远比假的零值安全。

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

- [x] 多线程服务器（`pthread`）：accept 循环 + 每客户端一线程
- [x] 互斥锁保护 LED（多客户端同时操作硬件的资源竞争场景）
- [x] 后台常驻采样线程：消除 `GET_CPU` 的 1 秒阻塞
- [ ] 自定义应用层数据包（帧头 + 长度 + 校验）
- [ ] Qt 图形化上位机
- [ ] ESP32-CAM 视频流接入（GStreamer）

## 已知待改进

- 客户端无法感知服务器断开（阻塞在 `fgets`，需引入 `select` / `poll`）
- 纯文本协议没有帧边界，理论上存在**粘包**风险（由自定义数据包协议解决）
- `listen()` / `send()` 等部分系统调用的返回值尚未检查
- `GET_MEM` 回复内含 `\n`，多行数据不适合机器解析（自定义协议时统一处理）
