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

## 快速验证

另开一个 PowerShell 窗口：

```powershell
Invoke-RestMethod http://127.0.0.1:8080/health

Invoke-RestMethod -Method Post http://127.0.0.1:8080/tasks `
  -ContentType "application/json" `
  -Body '{"title":"学习 Boost.Beast"}'

Invoke-RestMethod http://127.0.0.1:8080/tasks
```

运行完整冒烟测试：

```powershell
.\tests\api_smoke.ps1
```

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
```

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

