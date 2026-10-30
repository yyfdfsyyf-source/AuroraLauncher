# Java → C++ 迁移矩阵

基准是归档中的 Java 工程：

```text
D:\AuroraLauncher-archive\2026-10-26\HMCL-main
```

状态：

- **完成**：C++ 中已有端到端实现并经过基础测试/CLI 验证。
- **部分**：主路径存在，但缺少边界、平台或完整产品能力。
- **未开始**：C++ 中没有实现，或只有文档/接口占位。
- **不迁移**：明确放弃的旧实现。

## 1. 启动核心

| 原 Java 能力 | C++ 状态 | 说明 |
| --- | --- | --- |
| 读取 `versions/<id>/<id>.json` | 完成 | 支持同目录其他 `.json` 文件名 |
| `inheritsFrom` 继承 | 完成 | 递归合并、循环检测 |
| 库规则（OS/arch/features） | 完成 | 规则顺序按 Mojang 语义 |
| Maven 坐标/路径 | 完成 | classifier、`@ext` |
| 下载客户端 JAR | 完成 | URL + SHA-1 |
| 下载 libraries | 完成 | 仅 artifact；串行 |
| 下载 natives | 完成 | classifier + SHA-1 |
| 解压 natives | 完成（Windows） | 仅 `.dll`；已应用 `extract.exclude` |
| 下载资源索引 | 完成 | SHA-1 |
| 下载 assets objects | 完成 | 8 线程；已用 1.19.2（3390 个）与 1.19.4（3579 个）真实资源验证，已有文件按 SHA-1 复核 |
| 旧版 virtual assets | 完成 | `virtual`/`map_to_resources` 复制与 `game_assets` |
| 启动参数生成 | 完成 | `arguments` + 旧 `minecraftArguments` |
| 下载/注入 logging.client | 完成 | 下载并注入 `-Dlog4j.configurationFile` |
| 启动进程 | 完成 | `CreateProcessW`；原版 1.19.2 与 Fabric 1.19.4 隔离实例已实跑到主界面（离线账号） |
| 启动前自动修复文件 | 部分 | 需显式 `install` 或 `--install` |
| 启动失败诊断 | 部分 | `launch --log FILE` 捕获子进程 stdout/stderr（含 JVM 自身输出）；崩溃报告解析与自动归因未做 |
| 版本清单拉取 | 完成 | `version latest` / `version fetch` |
| 下载源选择 | 完成 | 内置 BMCLAPI 优先，失败回退官方；认证/Adoptium 不镜像 |
| 自动选择 Java | 完成 | 按 major 扫描系统 JDK 和托管运行时 |
| 托管 Java 下载 | 完成 | Adoptium + SHA-256；大包现场下载待验证 |
| 内存/窗口配置 | 部分 | CLI/配置文件 |
| 游戏参数高级选项 | 部分 | 未迁移全部 HMCL 选项 |

## 2. 账号与认证

| 原能力 | C++ 状态 | 说明 |
| --- | --- | --- |
| 离线账号 | 完成 | 离线 UUID、access token `0` |
| Microsoft 登录 | 完成（需自有 client ID） | 设备码流程；现场登录待验证 |
| Xbox Live / XSTS | 完成 | 令牌交换和错误处理 |
| 正版皮肤/档案 | 部分 | 已获取 Minecraft profile；皮肤/披风未迁移 |
| 刷新令牌持久化 | 完成 | Windows DPAPI 加密的 `auth.dat` |
| Authlib Injector / 外置登录 | 未开始 | 无 |
| 多账号管理 | 未开始 | 单会话 |

## 3. 模组与整合包

| 原能力 | C++ 状态 | 说明 |
| --- | --- | --- |
| Fabric/Quilt 安装 | 完成 | 官方元数据 + profile JSON |
| Forge/NeoForge 安装 | 完成（installer bridge） | 官方 installer 下载/校验/执行；现场安装待验证 |
| Modrinth 模组搜索/安装 | 完成 | 搜索、版本选择、必需依赖递归、SHA-1；MCIM API 镜像 |
| CurseForge 模组 | 完成（需用户 API key） | 搜索、文件选择、必需依赖递归、SHA-1；有效 key 现场验证待做 |
| 模组依赖解析 | 部分 | Modrinth/CurseForge required 依赖已实现；整合包内依赖由包 manifest 自身提供 |
| CurseForge 整合包 | 完成 | manifest 解析、必需文件、overrides、自动加载器、可启动实例、升级/备份 |
| Modrinth 整合包 | 完成 | `.mrpack`、SHA-1、overrides、自动加载器、可启动实例、升级/备份 |
| 资源包/光影包 | 完成（基础） | 资源包扫描、pack.mcmeta 兼容性、新旧 options 格式与启停；光影包未单独管理 |
| PCL 实例兼容 | 完成 | 读取 `PCL/Setup.ini` 与 `PCL/config.v1.yml`：版本隔离、内存档位、JVM/游戏参数、指定 Java |
| 模组更新/禁用 | 完成 | `mod list/enable/disable`、jar 元数据；Modrinth SHA-1 更新检查与 `--apply`；CurseForge fingerprint 更新检查与 `--apply` |

## 4. UI 与主题

| 原能力 | C++ 状态 | 说明 |
| --- | --- | --- |
| JavaFX 主窗口 | 部分 | `aurora_gui` 实现无边框窗口（自绘标题栏、拖动/缩放/最大化）与莫奈色块主页（左栏大版本号与启动按钮、最近游玩、热力图）+ 全页实例选择 + 全页实例设置（概览/设置/日志、文件夹快捷方式）+ 启动器设置（通用/外观），其余功能仍为 CLI |
| 页面导航/向导 | 部分 | 顶栏「启动/实例」页签、实例选择页、带返回箭头的实例设置页、启动器设置页与实例/启动器两级设置页签；下载/安装向导未做 |
| JFoenix 组件 | 不迁移 | 原 Java 工作树正在移除 JFoenix |
| CSS 主题包 | 未开始 | 界面主色收在蓝绿家族，实例色板取自加载器图标（原版草绿、Fabric 米色、Quilt 紫、Forge 炭灰、NeoForge 橙）；内置「色块 / 柔和边界」两种卡片风格与浅色/深色主题（本机保存），未做外部主题包 |
| 背景/取色 | 已实现 | 本机图片缩为长边 1600 的 JPEG 存 localStorage，取 4 主色着色窗口与卡片；卡片可切换 22px 模糊磨砂 / 取色纯色 |
| 深色/浅色模式 | 已实现 | 跟随系统/浅色/深色；切换时同步原生窗口边与 WebView 底色 |
| 国际化 | 未开始 | CLI 英文，GUI 文案中文硬编码 |
| 下载进度 UI | 未开始 | 安装/下载仍在 CLI，GUI 不触发 |

## 5. 服务端与其他

| 原能力 | C++ 状态 | 说明 |
| --- | --- | --- |
| 服务端安装/启动 | 未开始 | 原 Java 服务端页面也未接入主入口 |
| 联机/房间 | 未开始 | 无 |
| 启动器自更新 | 未开始 | 无 |
| 崩溃分析 | 未开始 | 无 |
| 日志上传 | 未开始 | 无 |
| 多语言下载镜像 | 部分 | 支持自定义镜像 URL 列表，未内置预设 |
| 代理设置 | 完成 | 环境变量 + WinINET/PAC；无单独 UI |
| 便携模式 | 未开始 | 配置固定在 `%APPDATA%` |

## 6. 建议迁移顺序

```mermaid
flowchart LR
    A[核心稳定化] --> B[认证]
    B --> C[模组加载器]
    C --> D[整合包]
    D --> E[GUI]
    E --> F[主题/服务端/更新]
```

### 阶段 1：核心稳定化

- 版本清单拉取和版本安装。**已完成**
- 下载重试、镜像、断点续传、系统代理。**已完成**
- 版本日志配置。**已完成**
- virtual assets / `map_to_resources`。**已完成**
- 更完整的 Java 选择（位数、厂商、内存兼容性）。**部分**
- 结构化日志和退出码。**部分**

### 阶段 2：认证

- Microsoft OAuth 设备码。**已完成**
- Xbox Live/XSTS/Minecraft 令牌链。**已完成**
- DPAPI 保存 refresh token。**已完成**
- 正版/离线账号抽象统一。**部分**（尚未做多账号）
- 现场登录和刷新验证。**待验证**

### 阶段 3：模组加载器与模组

- Fabric/Quilt JSON 安装。**已完成**
- Forge/NeoForge 官方 installer bridge。**已完成（现场安装待验证）**
- 启动参数注入和库合并。**沿用 `inheritsFrom`，已完成**
- Modrinth 模组搜索/安装、必需依赖递归。**已完成（现场验证）**
- 模组扫描、启用/禁用、Modrinth 更新检查与替换。**已完成（现场验证）**
- CurseForge 模组搜索/安装、必需依赖、fingerprint 更新检查。**已完成（需用户 API key；有效 key 现场验证待做）**

### 阶段 4：整合包

- CurseForge/Modrinth 整合包解析、必需文件下载、overrides。**已完成**
- 加载器自动安装、原版文件下载、可启动实例 JSON、实例隔离运行目录。**已完成（Fabric 空包实测；完整游戏下载待现场验证）**
- 整合包升级/差异更新与升级前备份。**已完成（测试覆盖）**
- 资源包扫描、pack.mcmeta 兼容性与新旧 options 格式。**已完成（测试覆盖）**

### 阶段 5：GUI

- 在核心 API 稳定后选择 UI 框架。
- 先做版本列表/启动/下载/设置，再迁移高级页面。
- 主题包和 UI 动画最后处理。

## 7. 不应承诺的能力

在当前 C++ 代码状态下，不能声称：

- 可以替代 HMCL 的全部功能。
- 可以保证整合包的完整端到端流程；自动安装加载器和生成实例已完成，Fabric 空包实测通过，真实游戏启动已用原版 1.19.2 与 Fabric 1.19.4 隔离实例（离线账号）验证，但“整合包安装 → 真启动”这一步还没串起来跑过。模组依赖只支持 Modrinth/CurseForge 的 required 依赖。
- 已有 GUI、主题、服务端管理。
- 已通过 Microsoft 登录、Forge/NeoForge 完整安装和 UI 测试。
- 已具备可信发布代码签名（仓库只有自签开发流程）。

这份矩阵会随着实现推进更新；状态以源码和测试为准，不以计划为准。
