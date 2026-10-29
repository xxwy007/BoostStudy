# Boost.Asio RPC 三天实施计划

## 目标

在现有 Boost.Beast 任务管理服务基础上，用三天完成一个可运行、可压测、可用于面试展示的异步 RPC 原型。

最终项目同时保留两个入口：

```text
HTTP 8080：/health、/stats 和现有 REST API
RPC  9090：二进制帧、长连接、任务 CRUD
```

三天版本定位为性能导向的学习原型，不宣称生产级 RPC 框架。

## 范围约束

三天内必须完成：

- Boost.Asio 异步 TCP 服务端和客户端。
- 固定长度二进制帧头。
- 长连接和请求 ID。
- Task CRUD 方法分发。
- 单连接多请求并发。
- 服务端和客户端写队列。
- 请求超时、payload 限制和基础背压。
- 专用异步 RPC 压测客户端。
- QPS、平均延迟、P50、P95、P99 和最大延迟统计。

三天内暂不实现：

- TLS、认证和权限。
- 服务发现和负载均衡。
- Protobuf 代码生成和跨语言 SDK。
- 流式 RPC、压缩和零拷贝。
- 生产级自动重连和连接池。
- 分布式链路追踪。

payload 第一版继续使用 Boost.JSON，以减少新依赖和构建时间。二进制 Frame 与序列化层保持分离，后续可以替换为 Protobuf。

## 协议设计

固定帧头共 24 字节：

| 字段 | 大小 | 说明 |
| --- | ---: | --- |
| `magic` | 4 | 协议标识 |
| `version` | 2 | 协议版本 |
| `flags` | 2 | 请求、响应和错误标志 |
| `request_id` | 8 | 请求与响应关联 ID |
| `method_id` | 4 | RPC 方法编号 |
| `payload_size` | 4 | 正文长度 |

要求：

- 所有整数显式编码为网络字节序。
- 不直接发送 C++ 结构体内存，避免填充和平台布局差异。
- 解码后先验证 magic、version 和 payload 大小，再分配正文缓冲区。
- 默认最大 payload 为 1 MiB。

方法编号：

| Method ID | 方法 |
| ---: | --- |
| 1 | `CreateTask` |
| 2 | `ListTasks` |
| 3 | `FindTask` |
| 4 | `UpdateTask` |
| 5 | `DeleteTask` |

## 计划目录结构

```text
include/rpc/frame.hpp
include/rpc/frame_codec.hpp
include/rpc/listener.hpp
include/rpc/session.hpp
include/rpc/service_registry.hpp
include/rpc/client.hpp
include/rpc/error.hpp
src/rpc/frame_codec.cpp
src/rpc/listener.cpp
src/rpc/session.cpp
src/rpc/service_registry.cpp
src/rpc/client.cpp
src/rpc_bench.cpp
tests/rpc_codec_tests.cpp
tests/rpc_integration.sh
```

## 第一天：协议、服务端和顺序调用

### 上午：Frame Codec

- [ ] 定义 magic、version、flags 和最大 payload。
- [ ] 定义逻辑 `FrameHeader`，但不直接传输其内存布局。
- [ ] 实现 24 字节 `encode_header()`。
- [ ] 实现 `decode_header()` 和完整字段校验。
- [ ] 测试正常帧、错误 magic、错误版本和超大 payload。

### 下午：RPC 服务端

- [ ] 增加 `--rpc-port`，默认使用 9090。
- [ ] 实现 `RpcListener`。
- [ ] 实现 `RpcSession::read_header()`。
- [ ] 根据 `payload_size` 实现 `read_payload()`。
- [ ] 实现 `ServiceRegistry` 和 method ID 分发。
- [ ] 将现有 `TaskRepository` 接入 RPC CRUD。
- [ ] 顺序处理请求并返回相同 `request_id`。

### 第一天验收

- [ ] 服务端能拒绝非法帧和超大 payload。
- [ ] 测试客户端能完成一次 `CreateTask`。
- [ ] 响应的 `request_id` 与请求一致。
- [ ] HTTP `/health` 和现有 API 不受影响。

## 第二天：客户端、多路复用和背压

### 上午：RPC 客户端

- [ ] 实现异步连接和读取循环。
- [ ] 使用原子计数生成 `request_id`。
- [ ] 使用 pending map 保存未完成请求。
- [ ] 根据响应 `request_id` 完成对应调用。
- [ ] 增加请求超时和超时后的 pending 清理。
- [ ] 完成五个 Task RPC 客户端方法。

### 下午：单连接并发

- [ ] 客户端增加写队列，保证同一时刻只有一个 `async_write()`。
- [ ] 服务端增加写队列，避免多个响应帧交叉写入。
- [ ] 允许一条连接存在多个 in-flight 请求。
- [ ] 限制每连接最大 in-flight 数量。
- [ ] 限制写队列总字节数。
- [ ] 过载时返回明确的 Server Busy 错误帧。
- [ ] 测试响应乱序时仍能正确匹配请求。

### 第二天验收

- [ ] 单条 TCP 连接同时发送至少 32 个请求。
- [ ] 所有响应 request ID 匹配正确。
- [ ] 任务 ID 没有重复或丢失。
- [ ] 写队列不会出现并行 `async_write()`。
- [ ] 超时请求能够从 pending map 清除。

## 第三天：压测、异常测试和项目整理

### 上午：专用压测客户端

- [ ] 实现 `rpc_bench`，不通过大量外部进程制造请求。
- [ ] 支持配置连接数、每连接 in-flight 数和测试时长。
- [ ] 预热连接后再开始统计。
- [ ] 统计成功数、失败数和 QPS。
- [ ] 记录平均延迟、P50、P95、P99 和最大延迟。
- [ ] 分别测试 1、2、4 个 I/O 线程。

### 下午：稳定性与文档

- [ ] 测试错误 magic、错误版本和不支持的 method ID。
- [ ] 测试超大 payload、半包、粘包和客户端中途断开。
- [ ] 测试慢客户端导致写队列增长的场景。
- [ ] 检查 Session 关闭后 pending、队列和缓冲区均被释放。
- [ ] 更新 README、协议说明和架构图。
- [ ] 记录测试环境、请求大小、连接数和性能结果。
- [ ] 整理两分钟面试介绍、项目限制和后续改进方向。

### 第三天验收

- [ ] 连续完成至少 10000 次 RPC 调用。
- [ ] 没有 request ID 匹配错误。
- [ ] 没有任务 ID 重复或数据丢失。
- [ ] 输出 QPS、P50、P95、P99 和最大延迟。
- [ ] 错误帧和异常断开不会导致服务退出。

## 性能测试基线

首轮统一使用：

```text
payload：约 128 字节
响应：约 256 字节
连接数：10
每连接 in-flight：32
测试时间：30 秒
I/O 线程：1、2、4
```

每次结果必须记录：

```text
编译类型
CPU 和系统环境
请求及响应大小
连接数
in-flight 数
成功和失败请求数
QPS
平均延迟
P50 / P95 / P99 / 最大延迟
```

## 风险和降级方案

- 如果多路复用在第二天结束前仍不稳定，先限制每连接一个请求，保证完整 RPC 链路可运行，再继续增加 pending map。
- 如果 Boost.JSON 成为明显瓶颈，只记录结果，不在三天内临时引入复杂序列化库。
- 如果业务线程池来不及实现，保持当前内存 TaskService 在 I/O 线程执行，并在文档中明确阻塞任务必须移出 I/O 线程。
- 如果 P99 波动较大，增加预热时间并重复三轮，不能只保留最好结果。

## 最终交付物

- [ ] 可运行的 RPC 服务端。
- [ ] 可复用的异步 RPC 客户端。
- [ ] Frame Codec 和协议错误测试。
- [ ] Task CRUD RPC 集成测试。
- [ ] 单连接多路复用和写队列。
- [ ] 超时、payload 限制和基础背压。
- [ ] 专用 `rpc_bench`。
- [ ] 性能结果表和限制说明。
- [ ] README 构建、运行、测试和架构说明。
