# AuroraLauncher C++ Core

这是 AuroraLauncher 的 C++ 重写工程。目前完成的是**无 GUI 的启动核心 MVP**，不是 HMCL/AuroraLauncher Java 版的完整功能等价实现。

原 Java 工程和未提交的 JFoenix 移除/UI 迁移工作已完整归档，未直接删除：

- 归档目录：`D:\AuroraLauncher-archive\2026-10-26\`
- 完整快照：`AuroraLauncher-full-2026-10-26.zip`
  - SHA-256：`C3FE3143C0076117BBF98A5D7259342AA1807E46FFAB7C70875040C55579129E`
- Git 历史：`HMCL-main.git.bundle`
  - SHA-256：`39783AF633172DF5DDF4453CF7BF0778D3EDBAF707D3B3ACD260DA9FF3362FC9`
- 移出工作区的源码：`D:\AuroraLauncher-archive\2026-10-26\HMCL-main`

归档校验结果：ZIP 条目数与源文件数一致（40,415），所有条目的未压缩大小一致，关键文件已做 SHA-256 对比。

当前 C++ 核心已通过 Debug/Release 编译和单元测试；另外用本地临时 HTTP 服务验证了客户端 JAR、库、资源索引和 native ZIP 下载，以及 SHA-1 校验和 `.dll` 解压。中文路径/用户名也已通过 CLI 验证。真实 Mojang 下载、真实游戏启动和 GUI 尚未验证。

## 当前能力

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 版本 JSON 读取 | 已实现 | 读取 `versions/<id>/<id>.json`，支持同目录下其他 `.json` 文件名 |
| `inheritsFrom` 继承 | 已实现 | 递归合并主类、资源索引、库、JVM/游戏参数、Java 版本 |
| 规则判断 | 已实现 | 支持 OS、架构、版本正则和 features 规则 |
| Maven 坐标 | 已实现 | group/artifact/version/classifier/extension |
| Java 检测 | 部分实现（Windows） | 注册表、`JAVA_HOME`、`PATH`、常见 JDK 目录、`release` 文件；按 major 选最低可用版本 |
| 离线账号 UUID | 已实现 | `OfflinePlayer:<name>` 的 MD5 UUID |
| 文件下载 | 部分实现（Windows） | WinHTTP、重定向、SHA-1 校验、`.part` 临时文件；无重试/镜像/限速 |
| 客户端/库/资源下载 | 部分实现 | 资源对象、libraries、natives；不含镜像和版本清单拉取 |
| 原生库解压 | 已实现（Windows） | miniz 解压 `.dll` 到版本 natives 目录 |
| 启动命令生成 | 已实现 | 类路径、变量替换、JVM/游戏参数、离线账号参数 |
| 启动进程 | 部分实现 | `CreateProcessW`，可等待或分离；未做真实游戏启动验证 |
| 配置文件 | 已实现（基础） | 游戏目录、Java、内存、窗口、用户名、额外参数 |
| GUI | 未接入 | 当前只有 CLI |
| Microsoft 登录 | 未开始 | 没有 OAuth/设备码/Xbox 链路 |
| 版本清单/自动安装版本 | 未开始 | 只处理本地已有版本 JSON |
| Forge/Fabric/Quilt/NeoForge | 未开始 | 没有安装器或 mod loader 启动链 |
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

## CLI 示例

```powershell
# 查看 Java
.\build\Debug\aurora.exe java list

# 查看本地版本
.\build\Debug\aurora.exe version list --game-dir "D:\path\to\.minecraft"

# 查看解析后的版本信息
.\build\Debug\aurora.exe version show 1.20.1 --game-dir "D:\path\to\.minecraft"

# 下载客户端、依赖库、资源并解压 natives
.\build\Debug\aurora.exe install 1.20.1 --game-dir "D:\path\to\.minecraft"

# 只生成启动命令，不实际启动
.\build\Debug\aurora.exe launch 1.20.1 --game-dir "D:\path\to\.minecraft" --username Steve --dry-run

# 使用离线账号启动
.\build\Debug\aurora.exe launch 1.20.1 --game-dir "D:\path\to\.minecraft" --username Steve
```

`--json` 可用于 `version list`、`version show`、`java list` 和 `launch --dry-run`，方便后续 GUI 或脚本调用。

## 设计约束

- 当前核心面向 Windows；非 Windows 分支只保留编译骨架，WinHTTP/BCrypt/注册表/进程启动尚未移植，相关单元测试也不能在非 Windows 上通过。
- Windows 下通过 `CommandLineToArgvW` 读取宽字符命令行并转换为 UTF-8，中文路径、用户名和版本 id 已做 CLI 验证。
- 只支持离线账号。没有 Microsoft 登录、正版验证、Authlib Injector。
- 不会自动从 Mojang 下载版本清单，也不会创建新版本目录；需要先用原启动器或手工准备 `versions/<id>/<id>.json`。
- 资源索引只处理 `objects`；未实现旧版 `virtual`/`map_to_resources` 资源布局。
- 未下载/注入版本 JSON 的 `logging.client` 日志配置文件；部分版本仍可启动，但日志配置可能缺失。
- 启动前要求客户端 JAR、资源索引和所有带 artifact 的 allow 库已经存在；缺少非 artifact 的普通 classpath 库时跳过，native 库仍会校验；`install` 会尝试补齐。
- 没有模组加载器注入、模组依赖解析、整合包安装、服务器管理、主题包和 GUI。

## 目录

```text
src/
  cli.cpp                  CLI 命令解析和输出
  core/
    config.cpp             JSON 配置
    hash.cpp               SHA-1/MD5（Windows BCrypt）
    http.cpp               WinHTTP 下载
    install.cpp            版本文件安装流水线
    java.cpp               Java 检测
    launch.cpp             启动计划生成
    maven.cpp              Maven 坐标
    paths.cpp              路径和文件工具
    process.cpp            CreateProcessW 封装
    repository.cpp          版本目录和继承解析
    rules.cpp              OS/features 规则
    strings.cpp            UTF-8/UTF-16、参数引用
    version.cpp            版本 JSON 模型
    zip.cpp                miniz 解压
tests/                     核心单元测试
third_party/               nlohmann/json、miniz（保留许可证）
ARCHITECTURE.md            当前 C++ 架构、数据流和风险
MIGRATION.md               Java 功能迁移矩阵和阶段计划
```

## 许可

原工程为 GPL-3.0，本重写工程继续使用 GPL-3.0，见 `LICENSE`。第三方库许可证见 `third_party/*/LICENSE*`。
