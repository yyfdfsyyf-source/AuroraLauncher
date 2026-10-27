# AuroraLauncher C++ Core

这是 AuroraLauncher 的 C++ 重写工程，当前是 **Windows-first、无 GUI 的启动核心**，不是 HMCL/Java 版的完整功能等价实现。核心目标是把版本管理、认证、加载器和运行时安装做成可复用模块，后续 GUI 直接调用。

原 Java 工程和未提交的 JFoenix 移除/UI 迁移工作已完整归档，未直接删除：

- 归档目录：`D:\AuroraLauncher-archive\2026-10-26\`
- 完整快照：`AuroraLauncher-full-2026-10-26.zip`
  - SHA-256：`C3FE3143C0076117BBF98A5D7259342AA1807E46FFAB7C70875040C55579129E`
- Git 历史：`HMCL-main.git.bundle`
  - SHA-256：`39783AF633172DF5DDF4453CF7BF0778D3EDBAF707D3B3ACD260DA9FF3362FC9`
- 移出工作区的源码：`D:\AuroraLauncher-archive\2026-10-26\HMCL-main`

归档校验结果：ZIP 条目数与源文件数一致（40,415），所有条目的未压缩大小一致，关键文件已做 SHA-256 对比。

## 验证状态

已执行：

- Debug 构建通过，`ctest` 7 个测试目标全部通过（核心、HTTP、加载器、版本清单、认证、Java 安装、Forge/NeoForge 解析与命令构建）。
- 现场联网验证：Mojang 版本清单 `version latest`、Forge promotions `forge list forge 1.21.1`。
- 用本地临时 HTTP 服务验证过客户端 JAR、库、资源索引、native ZIP 下载、SHA-1 校验和 `.dll` 解压；中文路径/用户名也已通过 CLI 验证。

尚未现场验证：

- 真实启动 Minecraft 游戏进程。
- Microsoft 登录全链路（需要项目自己的 Azure 应用 client ID 和真实账号）。
- Fabric/Quilt 元数据 API（本机代理对 `meta.fabricmc.net` 连接不稳定/被拦截）。
- Forge/NeoForge 安装器完整执行（代码和解析已测试，安装器会下载大量第三方文件）。
- 完整 Adoptium JDK 下载（本机网络速度不足以在合理时间内验证）。

## 当前能力

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 版本 JSON 读取 | 已实现 | 支持 `versions/<id>/<id>.json` 和同目录其他 JSON 文件名 |
| `inheritsFrom` 继承 | 已实现 | 递归合并主类、资源索引、库、JVM/游戏参数、Java 版本 |
| 版本清单与版本下载 | 已实现 | `version latest` / `version fetch`，Mojang `version_manifest_v2.json` |
| 规则判断 | 已实现 | OS、架构、版本正则和 features 规则 |
| Maven 坐标 | 已实现 | group/artifact/version/classifier/extension |
| Java 检测 | 已实现 | 注册表、`JAVA_HOME`、PATH、常见 JDK 目录、托管运行时 |
| 托管 Java 安装 | 已实现 | Adoptium API、SHA-256、ZIP 路径穿越防护、原子替换 |
| 离线账号 UUID | 已实现 | `OfflinePlayer:<name>` 的 MD5 UUID |
| Microsoft 登录 | 已实现（需自有 client ID） | 设备码 → Microsoft token → Xbox Live → XSTS → Minecraft；DPAPI 保存 |
| 文件下载 | 已实现 | WinHTTP、重试退避、镜像、断点续传、SHA-1、系统代理 |
| 客户端/库/资源下载 | 已实现 | 资源对象、libraries、natives、版本日志配置、旧版 virtual/map_to_resources |
| 原生库解压 | 已实现（Windows） | miniz 解压 `.dll` 到版本 natives 目录 |
| 启动命令生成 | 已实现 | 类路径、变量替换、JVM/游戏参数、离线/正版参数 |
| Fabric / Quilt | 已实现 | 元数据查询、profile JSON 安装到 `versions/` |
| Forge / NeoForge | 已实现（installer bridge） | 官方安装器下载、SHA-1 sidecar、headless 执行、产物定位 |
| 启动进程 | 部分实现 | `CreateProcessW`，可等待或分离；真实游戏启动待验证 |
| 配置文件 | 已实现（基础） | 游戏目录、Java、内存、窗口、用户名、client ID、下载、额外参数 |
| 自签开发签名 | 已实现 | 脚本 + CMake Windows 资源/清单；公开发布仍需可信证书 |
| GUI | 未接入 | 当前只有 CLI |
| 整合包/模组/资源包管理 | 未开始 | 未迁移 |
| 主题包/UI/服务端模块 | 未开始 | 未迁移 |

## 构建

环境：Windows 10/11、Visual Studio 2022（MSVC）、CMake 3.28+。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

生成的可执行文件：

```text
build\Debug\aurora.exe
```

Release 构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

## CLI 示例

```powershell
# 查看/安装 Java（托管运行时放在 <game-dir>\runtime\java）
.\build\Debug\aurora.exe java list
.\build\Debug\aurora.exe java install 21 --game-dir "D:\path\to\.minecraft"

# 拉取并查看版本
.\build\Debug\aurora.exe version latest
.\build\Debug\aurora.exe version fetch 1.21.1 --game-dir "D:\path\to\.minecraft"
.\build\Debug\aurora.exe version show 1.21.1 --game-dir "D:\path\to\.minecraft"

# 下载客户端、依赖库、资源并解压 natives
.\build\Debug\aurora.exe install 1.21.1 --game-dir "D:\path\to\.minecraft"

# Fabric / Quilt
.\build\Debug\aurora.exe loader list fabric 1.21.1
.\build\Debug\aurora.exe loader install fabric 1.21.1 --game-dir "D:\path\to\.minecraft"

# Forge / NeoForge
.\build\Debug\aurora.exe forge list forge 1.21.1
.\build\Debug\aurora.exe forge install forge 1.21.1 --game-dir "D:\path\to\.minecraft"

# Microsoft 登录（先配置自己的 Azure client ID）
.\build\Debug\aurora.exe config set microsoftClientId <your-client-id>
.\build\Debug\aurora.exe auth login
.\build\Debug\aurora.exe auth status

# 离线启动 / 只生成命令
.\build\Debug\aurora.exe launch 1.21.1 --game-dir "D:\path\to\.minecraft" --username Steve
.\build\Debug\aurora.exe launch 1.21.1 --game-dir "D:\path\to\.minecraft" --username Steve --dry-run
```

`--json` 可用于 `version list/show/latest/fetch`、`java list/install`、`loader list/install`、`forge list/install`、`auth` 和 `launch --dry-run`，方便后续 GUI 或脚本调用。

## 网络与代理

- 启动器自身使用 WinHTTP。优先读取 `HTTPS_PROXY` / `HTTP_PROXY` / `ALL_PROXY`（以及小写形式），否则读取当前用户的 WinINET 设置（含 PAC）。
- `NO_PROXY` 会转换为 WinHTTP 绕过列表；WinINET 的 `ProxyOverride` 也会保留。
- Forge/NeoForge 官方安装器由 Java 执行，会自动带上 `-Djava.net.useSystemProxies=true`，让 JVM 使用同一套系统代理。
- 下载默认带重试、指数退避、镜像回退和 `.part` 断点续传；可通过 `--retry`、`--mirror` 和配置项覆盖。

## 设计约束

- 当前核心面向 Windows；非 Windows 分支只保留编译骨架，WinHTTP/BCrypt/注册表/进程启动尚未移植。
- Microsoft 登录必须使用 AuroraLauncher 自己的 Azure 应用 client ID；不会冒用 HMCL 或其他启动器的应用身份。
- 发布二进制应使用项目自己的代码签名证书。仓库只提供自签开发流程，不包含可信证书私钥；见 `SIGNING.md`。
- 资源索引支持 `objects` 以及旧版 `virtual`/`map_to_resources` 布局；`game_assets` 会指向实际资源目录。
- 启动前要求客户端 JAR、资源索引和所有带 artifact 的 allow 库已经存在；`install` 会尝试补齐。
- 没有 GUI、整合包安装、模组依赖解析、服务器管理、主题包；这些属于后续阶段。
- Forge/NeoForge 支持以官方 installer 为准；本项目不重新实现其安装逻辑，也不在启动时隐式联网。

## 目录

```text
src/
  cli.cpp                  CLI 命令解析和输出
  core/
    auth.cpp               Microsoft 设备码/Xbox/Minecraft 登录与 DPAPI 存储
    config.cpp             JSON 配置
    forge.cpp              Forge/NeoForge 元数据、installer 下载与执行
    hash.cpp               SHA-1/SHA-256/MD5（Windows BCrypt）
    http.cpp               WinHTTP 请求、重试、断点续传、镜像
    http_post.cpp          WinHTTP POST（认证）
    install.cpp            版本文件安装流水线
    java.cpp               Java 检测
    java_install.cpp       Adoptium 托管 Java 安装
    launch.cpp             启动计划生成
    loader.cpp             Fabric/Quilt 元数据与 profile 安装
    manifest.cpp           Mojang 版本清单
    maven.cpp              Maven 坐标
    paths.cpp              路径和文件工具
    process.cpp            CreateProcessW 封装
    proxy.cpp              系统代理/PAC 解析
    repository.cpp         版本目录和继承解析
    rules.cpp              OS/features 规则
    strings.cpp            UTF-8/UTF-16、参数引用
    version.cpp            版本 JSON 模型
    zip.cpp                miniz 解压
tests/                     核心单元测试
third_party/               nlohmann/json、miniz（保留许可证）
ARCHITECTURE.md            当前 C++ 架构、数据流和风险
MIGRATION.md               Java 功能迁移矩阵和阶段计划
SIGNING.md                 产品身份、OAuth 应用和代码签名说明
```

## 许可

原工程为 GPL-3.0，本重写工程继续使用 GPL-3.0，见 `LICENSE`。第三方库许可证见 `third_party/*/LICENSE*`。
