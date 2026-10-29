# Beast Task Server

一个使用 Boost.Asio、Boost.Beast、Boost.JSON 和 Boost.Program_options 编写的内存版任务管理 API。项目提供可运行的学习模板，覆盖异步 HTTP、JSON、CRUD、超时、请求体限制和线程安全。

## 接口

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/health` | 健康检查 |
| GET | `/tasks` | 查询任务列表 |
| POST | `/tasks` | 创建任务 |
| GET | `/tasks/{id}` | 查询任务 |
| PUT | `/tasks/{id}` | 修改任务 |
| DELETE | `/tasks/{id}` | 删除任务 |
| GET | `/stats` | 请求数与活动连接数 |

数据仅保存在内存中，服务退出后会丢失。

## 环境要求

- Visual Studio 2022（安装“使用 C++ 的桌面开发”）
- CMake 3.24+
- Git
- vcpkg（也可以使用 Visual Studio 自带的副本）

当前目录包含 `vcpkg.json`，配置 CMake 时会自动安装需要的 Boost 组件。

### 使用本地 Boost

本机模板默认使用 `E:\boost_1_92_0`。也可以在配置时通过 `BOOST_ROOT`
覆盖这个位置：

```powershell
cmake -S . -B build `
  -DBOOST_ROOT="E:/boost_1_92_0"
cmake --build build --config Debug
```

模板会依次查找 `${BOOST_ROOT}/stage/lib` 和 `${BOOST_ROOT}/lib`。如果库文件
放在其他位置，可以额外指定：

```powershell
cmake -S . -B build `
  -DBOOST_ROOT="E:/boost_1_92_0" `
  -DBOOST_LIBRARY_DIR="E:/boost_1_92_0/custom-lib"
```

`Boost.Beast` 是头文件库，但本项目还使用了 `Boost.JSON`、
`Boost.Program_options`。Boost.System 在现代 Boost 中默认以头文件方式使用。
仅解压 Boost 源码还不够，必须能在库目录中找到前两个组件对应的 `.lib`
文件。若 `stage/lib` 为空，请先在 Boost
根目录运行：

```powershell
.\bootstrap.bat
.\b2.exe --with-json --with-program_options stage
```

## 构建

PowerShell：

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Debug
```

本机 Visual Studio 若自带 vcpkg，也可以把 `VCPKG_ROOT` 指向
`<Visual Studio 安装目录>\VC\vcpkg`。

运行：

```powershell
.\build\Debug\task_server.exe --address 127.0.0.1 --port 8080 --threads 4
```

如果使用 Ninja 等单配置生成器，可执行文件通常位于 `build/task_server.exe`。

查看参数：

```powershell
.\build\Debug\task_server.exe --help
```

成功响应会输出一条访问日志，例如：

```text
[+1250ms] session=8 thread=14260 event=access method=GET target=/tasks status=200 duration_us=318 bytes_written=245
```

连接正常关闭、读取超时、请求体超限、操作取消、对端中途断开和其他网络错误
会使用不同的事件名称，便于区分正常连接生命周期和真正的读写故障。

## 快速验证

另开一个 PowerShell 窗口：

```powershell
Invoke-RestMethod http://127.0.0.1:8080/health

Invoke-RestMethod -Method Post http://127.0.0.1:8080/tasks `
  -ContentType "application/json" `
  -Body '{"title":"学习 Boost.Beast"}'

Invoke-RestMethod http://127.0.0.1:8080/tasks
```

运行完整冒烟测试（Git Bash 或 WSL）：

```bash
bash ./tests/api_smoke.sh
```

也可以指定服务地址：

```bash
bash ./tests/api_smoke.sh http://127.0.0.1:8080
```

错误与边界测试使用 Git Bash 或 WSL 运行：

```bash
bash ./tests/api_error_cases.sh
```

如果服务使用了不同的地址或 `--body-limit`，需要传入相同参数：

```bash
bash ./tests/api_error_cases.sh http://127.0.0.1:9090 32768
```

该脚本覆盖非法 JSON、标题边界、错误字段类型、非法或不存在的任务 ID、
错误 Content-Type、405、未知路由、请求体超限，以及客户端在请求过程中断开。

自动比较 1、2、4 个 I/O 线程的并发行为：

```bash
bash ./tests/thread_scaling_benchmark.sh
```

脚本默认使用 `out/build/x64-Debug/task_server.exe`，每轮并发创建 100 个任务，
并使用端口 `18080`。也可以依次指定可执行文件、请求数量和测试端口：

```bash
bash ./tests/thread_scaling_benchmark.sh \
  ./out/build/x64-Debug/task_server.exe 200 18081
```

输出包括批次耗时、平均请求耗时、最大请求耗时和每秒请求数。该结果适合
观察线程数量变化，不代表严格的生产环境性能基准。

## 代码结构

```text
include/task.hpp                任务数据模型及 JSON 输出
include/task_repository.hpp     线程安全的内存仓库接口
include/router.hpp              HTTP 路由接口
include/session.hpp             单个 HTTP 连接的异步生命周期
include/listener.hpp            TCP 监听与连接接受
src/task_repository.cpp         CRUD 实现
src/router.cpp                  路由、校验、状态码和 JSON 响应
src/session.cpp                 async_read / async_write 调用链
src/listener.cpp                async_accept 调用链
src/main.cpp                    参数解析、线程和服务启动
tests/api_smoke.ps1             基础接口验证
tests/api_error_cases.sh        错误响应和边界行为验证
tests/concurrency_probe.sh      单次并发正确性和耗时验证
tests/thread_scaling_benchmark.sh  自动比较 1、2、4 个 I/O 线程
```

## 架构与并发边界

```text
Listener
  ↓ 接受 TCP 连接
HttpSession
  ↓ 异步读取和发送 HTTP 消息
Router
  ↓ 校验方法、路径和 JSON
TaskRepository
  ↓ mutex 保护共享容器和任务 ID
```

- 多个线程共同调用同一个 `io_context.run()`。
- 每个 Session 使用独立 strand，同一连接的回调不会同时执行，但可以先后由不同线程执行。
- 不同 Session 可以并发执行。
- `shared_from_this()` 保证异步操作完成前 Session 不会被销毁。
- Repository 使用 mutex 保护复合数据操作，统计计数使用 atomic。
- `TaskRepository::list()` 只在复制共享数据时持锁，局部排序在锁外执行。

## 当前限制

- 任务只保存在内存中，服务重启后数据丢失。
- 路由使用代码中的条件分支，接口增多后应改为更明确的路由表或服务层。
- 收到退出信号后直接停止 `io_context`，尚未等待所有 Session 完成，属于基础停机而非完整优雅停机。
- 请求体超过限制时当前会关闭连接，没有生成结构化 `413` 响应。
- Router 和 Repository 操作在 I/O 回调线程中同步执行，不适合直接加入阻塞数据库或耗时计算。
- 没有 TLS、认证、分页、持久化和跨进程部署能力。
- 本地并发脚本包含 `curl` 进程创建成本，结果只适合学习和趋势比较。

## 学习要点

- `io_context` 保存和分发异步完成回调，本身不自动创建工作线程。
- `async_accept`、`async_read` 和 `async_write` 注册操作后立即返回。
- 一个 Session 对应一条 TCP 连接；Keep-Alive 允许一个 Session 处理多个 HTTP 请求。
- strand 提供串行执行保证，不提供固定线程亲和性。
- 异步缓冲区和响应对象必须存活到完成回调执行结束。
- mutex 适合保护容器与自增 ID 等复合状态，atomic 适合独立计数。
- 增加线程不保证吞吐量线性提升，需要结合任务粒度、锁竞争和测试端开销分析。

## 最终验证记录

2026-10-29 使用独立 Debug 构建完成以下验证：

- CMake 配置、Boost 1.92.0 查找和完整编译成功。
- `api_smoke.sh` 通过。
- `api_error_cases.sh` 的 17 个错误与边界用例通过。
- 100 个并发创建请求全部成功，产生 100 个唯一 ID，且任务全部存在于仓库中。
- 直接 HTTP 命令完成创建、查询、修改、列表、删除和删除后 404 验证。
- 四个 I/O 线程均实际执行了回调，服务日志记录了 132 个 Session 和 130 条访问日志。

## 一周学习建议

1. 第一天只阅读并跑通 `/health`，画出 `Listener -> Session -> Router` 调用链。
2. 第二天阅读 `Task`、JSON 转换和 `TaskRepository`。
3. 第三天逐行理解 `POST /tasks` 与 `GET /tasks`。
4. 第四天理解 `/tasks/{id}` 的路径解析以及 PUT、DELETE。
5. 第五天检查请求体限制、30 秒超时、Keep-Alive 和响应生命周期。
6. 第六天运行多线程配置和冒烟测试，给测试增加非法 JSON 与 404 场景。
7. 第七天自行增加持久化、分页或日志中的一个功能，并补充 README。

## 建议自行完成的扩展

- 为所有响应增加请求 ID。
- 给 `GET /tasks` 增加 `completed=true/false` 过滤条件。
- 增加分页参数 `page` 和 `page_size`。
- 增加结构化访问日志。
- 为仓库和路由编写单元测试。
- 实现优雅停止：停止接收新连接，并等待已有请求完成。

