# AuroraLauncher C++ Core

这是 AuroraLauncher 的 C++ 重写工程，当前是 **Windows-first 的启动核心**，不是 HMCL/Java 版的完整功能等价实现。CLI 覆盖全部功能面，GUI 提供浅色玻璃主页（左栏大版本号与启动胶囊、主卡最近游玩行内启动、底部统计与年度日历）以及全页式实例选择、实例设置（概览/设置/日志，含实例/存档/模组/日志文件夹快捷方式），两者调用同一套核心模块。实例选择与实例设置沿用 PCL2 CE 的信息结构；主页按用户提供的设计稿重排（左栏版本 + 主卡 + 底卡），视觉沿用 LauncherX 与美西螈启动器的浅色卡片语言。窗口为无边框自绘，保留 4px 缩放入口并去掉了 Win11 的白色 DWM 边框。

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

- Debug 和 Release 构建通过，`ctest` 14 个测试目标全部通过（核心、HTTP、加载器、版本清单、认证、Java 安装、Forge/NeoForge、Modrinth、模组管理、CurseForge、资源包、整合包、PCL、活动记录）。
- 现场联网验证：Mojang 版本清单 `version latest`、Fabric loader 列表（经 BMCLAPI 镜像）、Forge promotions 和 NeoForge 版本列表（`forge list forge|neoforge 1.21.1`，经 BMCLAPI 元数据镜像，约 0.6 秒）。
- Modrinth 现场验证：`mod search sodium` 经 MCIM API 镜像返回结果；`mod install sodium` 下载并校验 SHA-1；`mod install sodium-extra` 自动解析并识别已安装的 Sodium 依赖。
- 模组管理现场验证：`mod list`、`mod disable/enable` 往返成功；`mod update` 对旧版 Sodium 报告 0.8.13 可用，`--apply` 下载、校验并替换旧 JAR，再查为 up to date。
- CurseForge：API 请求、`x-api-key` 请求头和 403 映射已实测；MurmurHash2 fingerprint（去 0x09/0x0A/0x0D/0x20，seed 1）和 `/v1/fingerprints/432` 批量匹配已用固定向量测试，`curseforge update` 的匹配/替换流程已用注入响应测试。真实搜索/安装需要用户自己的 CurseForge API key，尚未用有效 key 现场跑通。
- 资源包现场验证：`resourcepack list/enable/disable` 在临时目录往返成功，`options.txt` 的其他选项与 CRLF 行尾保持不变；`pack.mcmeta`（含 `supported_formats`/`min_format`/`max_format`）、`pack.png`、1.13 前后 options 格式和 `incompatibleResourcePacks` 同步已有测试。
- 整合包现场验证：自建 `.mrpack`（真实 Sodium 文件 URL + SHA-1）安装成功，客户端 overrides 覆盖普通 overrides，重复安装刷新未修改文件；另用空包实测 Fabric 加载器自动安装并生成 `versions/<id>/<id>.json`（`--no-game`，未下载完整游戏）。升级保留用户修改、保留用户删除、删除新包移除的 overrides、失败时从备份恢复，以及 CurseForge 删除新 manifest 中不存在的旧文件，均已用测试验证。
- PCL 实例兼容：用合成 `PCL/Setup.ini` 和 `PCL/config.v1.yml` 实测隔离目录、内存档位（tick 25 → 8192 MB、tick 20 → 5632 MB、未写档位 → 3072 MB）、`VersionAdvanceJvm/Game` 参数（含 `${library_directory}` 替换）和 `VersionArgumentJavaSelect`；Java 偏好同时支持旧版裸路径与 PCL CE 的 `{"kind":"exist",...}` JSON，路径失效时回退到自动检测。`launch --dry-run --json` 的工作目录、`-Xmx`、额外参数与 java 均按 PCL 设置生成；解析器和 `run_dir` 行为有独立测试。
- 镜像优先/官方回退：用 `install 1.0 --no-assets` 实测客户端和库经 BMCLAPI 下载，并在镜像慢节点出现后回退官方；客户端和 7 个适用库的 SHA-1 全部匹配。
- 真实游戏启动（离线账号）：本机 `D:\PCL\.minecraft` 复制出的独立冒烟目录（1.5 GB，不写回真实游戏目录）实跑两次。① 原版 1.19.2：`install` 校验 3390 个资源后 `launch` 起来，日志出现 `Backend library: LWJGL version 3.3.1 build 7`、逐张 atlas 拼接、`OpenAL initialized on device ... 扬声器 (Realtek(R) Audio)`、`Sound engine started`，无 crash-report、无 `hs_err`，退出为干净的 `Stopping!`。② Fabric 1.19.4（真实 PCL CE 实例，带 BOM 的 `config.v1.yml`、`VersionArgumentIndieV2: true`）：工作目录切到 `versions/<id>`，日志 `Loading Minecraft 1.19.4 with Fabric Loader 0.18.4`、`Loading 9 mods`、Mixin `Service=Knot/Fabric Env=CLIENT`；再用 `mod install sodium` 装进实例自己的 `mods`，重启后模组列表出现 `sodium 0.4.10+build.24` 及其 fabric-* 传递依赖并再次到达 `Sound engine started`（进程 RSS 1.13 GB，确认存活后由我们主动结束）。离线启动下 `Failed to verify authentication Status: 401` 属预期（伪造 token 无法通过 authlib 校验，与其他启动器离线模式一致）。
- `--log FILE` 真机复核：同一次 1.19.2 启动下文件收到 50 条游戏 log4j 事件和 JVM 自身的 stderr（例如 `NOTE: Picked up JDK_JAVA_OPTIONS: ...`，这行只在子进程输出里可见），头部只保留时间戳与 java 可执行路径；同一文件里 `--accessToken` 与 `-cp` 命中均为 0 次。第一版曾把整条命令行写进日志，联网登录时会泄露 access token，已修正并加了回归测试。
- 桌面 GUI 真机验证：`aurora_gui` 在 1180×760 居中窗口下真实点击走完「主页 → 实例选择 → 实例设置（概览/设置/日志）→ 启动 → 停止」。无边框窗口自绘浅色标题栏、状态胶囊与最小化/最大化/关闭；最小尺寸钳制为 980×620（请求 800×500 无效），最大化到工作区 1920×1032（任务栏保留）后可还原 1180×760。窗口边缘逐像素抽样：左/右/下为 #EEF1F6 的 4px 底边、上为 #FAFBFD 的标题栏色，没有纯白 DWM 边框。启动后主胶囊与顶栏状态胶囊同时变绿「停止游戏 / 运行中 · 1.19.4-Fabric_0.18.4」，最近游玩的同行按钮变「停止」、其余行禁用；日志实时滚动到 LWJGL/资源加载行；停止后状态为「已手动停止」，`activity.json` 写入会话，统计立即变为「4 分钟 / 10 次（今天 5 次）」。实例设置概览的「存档文件夹」真实拉起资源管理器到 `versions/<id>/saves`（`ShellExecuteW`，验证后关闭该窗口）。页面切换为 190ms 位移淡入（CDP 在 70ms 截到内容整体右移约 17px 的中间帧），设置内页签为 170ms 上移淡入，均由真实点击触发。实例图标（草方块）与玩家头像在运行时从用户自己的客户端 JAR 读取纹理、用 canvas 合成，仓库不附带 Mojang 素材；背景图为本项目生成。热力图按卡片宽度自适应格子尺寸（980×620 最小窗口到 1920 最大化均对齐），悬浮提示另用 148 天临时活动数据核对后还原。
- 截图与页面自检走 WebView2 远程调试（CDP）：`Page.captureScreenshot` 取 WebView 真实像素，DOM/计算样式核对页签、星期标签与热力图格子坐标，避免窗口被遮挡时截到其他程序。
- 用本地临时 HTTP 服务验证过客户端 JAR、库、资源索引、native ZIP 下载、SHA-1 校验和 `.dll` 解压；中文路径/用户名也已通过 CLI 验证。

尚未现场验证：

- Microsoft 登录全链路（需要项目自己的 Azure 应用 client ID 和真实账号）。
- Quilt 元数据 API（本机到 `meta.quiltmc.org` 超时；Fabric 已通过镜像实测）。
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
| 文件下载 | 已实现 | WinHTTP、镜像优先/官方回退、重试退避、断点续传、SHA-1、系统代理 |
| 客户端/库/资源下载 | 已实现 | 资源对象、libraries、natives、版本日志配置、旧版 virtual/map_to_resources |
| 原生库解压 | 已实现（Windows） | miniz 解压 `.dll` 到版本 natives 目录 |
| 启动命令生成 | 已实现 | 类路径、变量替换、JVM/游戏参数、离线/正版参数 |
| Fabric / Quilt | 已实现 | 元数据查询、profile JSON 安装到 `versions/` |
| Forge / NeoForge | 已实现（installer bridge） | 元数据镜像优先（BMCL schema 适配）/官方回退、安装器下载、SHA-1 sidecar、headless 执行、产物定位 |
| 启动进程 | 已实现（真启动验证） | `CreateProcessW`，可等待或分离；原版 1.19.2 与 Fabric 1.19.4 实例实跑到主界面 |
| 配置文件 | 已实现（基础） | 游戏目录、Java、内存、窗口、用户名、client ID、下载、额外参数 |
| 自签开发签名 | 已实现 | 脚本 + CMake Windows 资源/清单；公开发布仍需可信证书 |
| Modrinth 模组 | 已实现 | 搜索、版本选择、必需依赖递归安装、SHA-1；MCIM API 镜像 |
| 本地模组管理 | 已实现 | `mod list`、`mod enable/disable`、更新检查与 `mod update --apply` |
| CurseForge 模组 | 已实现（需用户自有 API key） | 搜索、文件选择、必需依赖、SHA-1；无下载授权的文件直接拒绝 |
| 资源包管理 | 已实现 | `resourcepack list/enable/disable`，读写 `options.txt` 的 `resourcePacks`；pack.mcmeta 兼容性、pack.png 检测、新旧 options 格式 |
| 整合包 | 已实现 | Modrinth `.mrpack` / CurseForge manifest、SHA-1、overrides、自动安装加载器、安装原版文件、生成可启动实例、升级/备份 |
| PCL 实例兼容 | 已实现 | 读取 `PCL/Setup.ini` 与 `PCL/config.v1.yml`：版本隔离（含 PCL 自动隔离规则）、内存档位、JVM/游戏参数、指定 Java；`launch` 自动采用 |
| GUI | 主页与实例页已实现 | `aurora_gui`：WebView2 宿主 + 无边框浅色窗口 + 主页（左栏大版本号与启动胶囊、最近游玩行内启动、统计与年度热力图）+ 全页实例选择 + 全页实例设置（概览/设置/日志，文件夹快捷方式）；安装与下载仍在 CLI |
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

桌面界面（可选，默认不构建）需要 WebView2 SDK 与运行时的支持：

```powershell
# 1) 取 SDK（无需 Qt、无需前端构建链；静态 loader，发布时不额外带 DLL）
python -m pip install --user aqtinstall           # 或任意能解包的 NuGet 客户端
# 解包 Microsoft.Web.WebView2 到某个目录，例如 D:\tools\webview2
# 2) 配置并构建
cmake -S . -B build -DAURORA_BUILD_GUI=ON -DAURORA_WEBVIEW2_ROOT=D:/tools/webview2
cmake --build build --config Release --target aurora_gui
# 3) 运行（界面直接读源码目录的 ui/index.html，改样式只需刷新窗口）
.\build\Release\aurora_gui.exe --game-dir "D:\path\to\.minecraft"
```

Windows 11 自带 WebView2 运行时；旧版 Windows 10 缺少时程序会弹窗提示安装地址，不会静默失败。

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

# Modrinth 模组（Fabric 示例）
.\build\Debug\aurora.exe mod search sodium --loader fabric --game-version 1.21.1
.\build\Debug\aurora.exe mod install sodium --game-dir "D:\path\to\.minecraft" --loader fabric --game-version 1.21.1
.\build\Debug\aurora.exe mod list --game-dir "D:\path\to\.minecraft"
.\build\Debug\aurora.exe mod disable sodium-fabric-0.8.13+mc1.21.1.jar --game-dir "D:\path\to\.minecraft"
.\build\Debug\aurora.exe mod update --game-dir "D:\path\to\.minecraft" --loader fabric --game-version 1.21.1 --apply

# CurseForge（使用你自己的 API key）
.\build\Debug\aurora.exe config set curseForgeApiKey <your-curseforge-key>
.\build\Debug\aurora.exe curseforge search jei --loader fabric --game-version 1.21.1
.\build\Debug\aurora.exe curseforge install 238222 --game-dir "D:\path\to\.minecraft" --loader fabric --game-version 1.21.1

# 资源包 / 整合包
.\build\Debug\aurora.exe resourcepack list --game-dir "D:\path\to\.minecraft" --instance "MyPack"
.\build\Debug\aurora.exe resourcepack enable "MyPack.zip" --game-dir "D:\path\to\.minecraft" --instance "MyPack" --game-version 1.21.1
.\build\Debug\aurora.exe modpack info "D:\packs\MyPack.mrpack"
# 安装为可启动实例：自动安装加载器、下载游戏文件、生成 versions/<id>/<id>.json
.\build\Debug\aurora.exe modpack install "D:\packs\MyPack.mrpack" --game-dir "D:\instances\MyPack" --java-home "D:\Java\jdk-21"
.\build\Debug\aurora.exe launch "MyPack" --game-dir "D:\instances\MyPack"
# 模组、资源包和 options 都位于 D:\instances\MyPack\versions\MyPack 下
.\build\Debug\aurora.exe mod list --game-dir "D:\instances\MyPack" --instance "MyPack"
# 整合包升级：重复 install 同一实例即可，升级前备份到 <game-dir>\backup
.\build\Debug\aurora.exe modpack install "D:\packs\MyPack-2.0.mrpack" --game-dir "D:\instances\MyPack"
# CurseForge：按本地 JAR 指纹识别并检查更新
.\build\Debug\aurora.exe curseforge update --game-dir "D:\instances\MyPack" --instance "MyPack" --loader fabric --game-version 1.21.1 --apply

# PCL 实例：自动读取 versions/<id>/PCL/Setup.ini 或 PCL/config.v1.yml 的隔离、内存、参数和 Java
.\build\Debug\aurora.exe launch "1.21.1-Forge" --game-dir "D:\path\to\.minecraft" --dry-run --json

# Microsoft 登录（先配置自己的 Azure client ID）
.\build\Debug\aurora.exe config set microsoftClientId <your-client-id>
.\build\Debug\aurora.exe auth login
.\build\Debug\aurora.exe auth status

# 离线启动 / 只生成命令
.\build\Debug\aurora.exe launch 1.21.1 --game-dir "D:\path\to\.minecraft" --username Steve
.\build\Debug\aurora.exe launch 1.21.1 --game-dir "D:\path\to\.minecraft" --username Steve --dry-run
```

`--json` 可用于 `version list/show/latest/fetch`、`java list/install`、`loader list/install`、`forge list/install`、`mod search/install/list/enable/disable/update`、`curseforge search/install/update`、`resourcepack list/enable/disable`、`modpack info/install`、`auth` 和 `launch --dry-run`，方便后续 GUI 或脚本调用。

## 网络与代理

- 启动器自身使用 WinHTTP。优先读取 `HTTPS_PROXY` / `HTTP_PROXY` / `ALL_PROXY`（以及小写形式），否则读取当前用户的 WinINET 设置（含 PAC）。
- `NO_PROXY` 会转换为 WinHTTP 绕过列表；WinINET 的 `ProxyOverride` 也会保留。
- Forge/NeoForge 官方安装器由 Java 执行，会自动带上 `-Djava.net.useSystemProxies=true`，让 JVM 使用同一套系统代理。
- 下载默认优先内置镜像；镜像失败或响应过慢时，在本次运行内回退官方源。`--mirror` / `downloadMirrors` 提供的额外 URL 位于内置镜像之后、官方之前。
- Mojang、Fabric、Forge、NeoForge 使用 BMCLAPI；Modrinth API 使用 MCIM（`mod.mcimirror.top/modrinth/`），Modrinth 文件 CDN 走 MCIM 的 `/data/` 备用路由（该路由会重定向到官方 CDN，不是独立缓存）。
- Microsoft/Xbox/Minecraft 认证接口、entitlements 和 Adoptium API 不走镜像。
- Modrinth 模组只安装 SHA-1 可验证的 `.jar`；`--no-deps` 可关闭必需依赖递归安装。
- CurseForge 使用用户自己的 API key（`--api-key`、`CURSEFORGE_API_KEY` 或 `curseForgeApiKey`），不会写入日志；`config show` 只显示 `<set>`。CurseForge API 与文件 CDN 不走镜像，文件无 `downloadUrl` 时直接拒绝，不擅自构造 CDN 地址。
- 整合包默认自动安装加载器（Fabric/Quilt 直接写 profile；Forge/NeoForge 下载并执行官方安装器）、下载客户端/库/资源，并把包内容放进 `versions/<id>`；`launch <id>` 会自动把该目录作为运行目录。`--no-loader` / `--no-game` 可用于只解包或离线准备。Modrinth 包文件校验 SHA-1，CurseForge 包需要用户 API key 解析下载地址。升级前默认复制实例到 `<game-dir>/backup/<id>-<时间戳>`，失败时恢复。
- 下载仍带重试、指数退避和 `.part` 断点续传；跨源切换时会丢弃上一源的半成品，避免拼接不同来源的数据。

## 设计约束

- 当前核心面向 Windows；非 Windows 分支只保留编译骨架，WinHTTP/BCrypt/注册表/进程启动尚未移植。
- Microsoft 登录必须使用 AuroraLauncher 自己的 Azure 应用 client ID；不会冒用 HMCL 或其他启动器的应用身份。
- 发布二进制应使用项目自己的代码签名证书。仓库只提供自签开发流程，不包含可信证书私钥；见 `SIGNING.md`。
- 资源索引支持 `objects` 以及旧版 `virtual`/`map_to_resources` 布局；`game_assets` 会指向实际资源目录。
- 启动前要求客户端 JAR、资源索引和所有带 artifact 的 allow 库已经存在；`install` 会尝试补齐。
- GUI 覆盖主页与实例页，但没有资源包下载源、服务器管理、主题包；Modrinth 和 CurseForge 的必需依赖已支持递归安装，模组启停、Modrinth/CurseForge 更新检查、整合包安装/升级已接入。
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
    http.cpp               WinHTTP 请求、重试、断点续传、镜像回退
    mirrors.cpp            BMCLAPI URL 映射、镜像健康状态和候选顺序
    http_post.cpp          WinHTTP GET/POST 带自定义头（认证、CurseForge 指纹）
    install.cpp            版本文件安装流水线
    java.cpp               Java 检测
    java_install.cpp       Adoptium 托管 Java 安装
    launch.cpp             启动计划生成
    loader.cpp             Fabric/Quilt 元数据与 profile 安装
    manifest.cpp           Mojang 版本清单
    maven.cpp              Maven 坐标
    modrinth.cpp           Modrinth 搜索、版本/依赖解析、模组安装与更新
    mods.cpp               本地模组扫描、启停和 jar 元数据
    curseforge.cpp         CurseForge 搜索/安装、指纹匹配与更新
    modpack.cpp            整合包解析、加载器/游戏安装、实例与升级
    resourcepacks.cpp      资源包扫描、pack.mcmeta 兼容性与 options.txt
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
