# AuroraLauncher

AuroraLauncher - Minecraft 启动器，基于 Hello Minecraft! Launcher (HMCL) 的现代化主题系统。

## 特性

- 🎨 **主题包系统** - 支持 .hmclt 单文件主题包
- 🌈 **CSS 布局配置** - 自定义字体、图标大小、动画、UI 位置
- 🎯 **MonetFX 主题色** - 动态主题色生成
- 📦 **自动依赖下载** - 下载模组时自动下载所需依赖
- 🌙 **深浅主题切换** - 平滑的主题切换体验

## 快速开始

### 方式一：从源码构建

```bash
# 克隆仓库
git clone https://gitee.com/YYFDFS/AuroraLauncher.git
cd AuroraLauncher

# 构建 (需要 Java 17+)
./gradlew :HMCL:shadowJar

# 运行开发版本
./gradlew :HMCL:run
```

### 方式二：使用已构建版本

从 [Releases](https://gitee.com/YYFDFS/AuroraLauncher/releases) 下载最新的 JAR 文件直接运行。

## 系统要求

- Java 17 或更高版本
- Windows / Linux / macOS

## 项目结构

```
AuroraLauncher/
├── HMCL/              # 主启动器模块
├── HMCLCore/          # 核心功能模块
├── HMCLBoot/          # 启动引导模块
├── buildSrc/          # Gradle 构建脚本
├── gradle/            # Gradle wrapper
└── config/            # 配置文件
```

## 鸣谢

- **Hello Minecraft! Launcher (HMCL)** - 本项目基于 HMCL 开发
- **huangyuhui** - HMCL 原作者
- **Glavo** - HMCL 核心维护者
- 所有 HMCL 贡献者

## 开源协议

本项目基于 [GPL v3](https://www.gnu.org/licenses/gpl-3.0.html) 开源协议。

> Copyright © 2026 YYFDFS and contributors.
> Based on Hello Minecraft! Launcher by huangyuhui.

## 联系方式

- Gitee: https://gitee.com/YYFDFS/AuroraLauncher
- Issues: https://gitee.com/YYFDFS/AuroraLauncher/issues
