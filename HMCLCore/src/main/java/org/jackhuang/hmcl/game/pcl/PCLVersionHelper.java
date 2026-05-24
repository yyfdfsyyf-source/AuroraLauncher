/*
 * Hello Minecraft! Launcher
 * Copyright (C) 2024  huangyuhui <huanghongxun2008@126.com> and contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
package org.jackhuang.hmcl.game.pcl;

import org.jackhuang.hmcl.game.Version;
import org.jackhuang.hmcl.util.gson.JsonUtils;
import org.jackhuang.hmcl.util.io.FileUtils;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Stream;

import static org.jackhuang.hmcl.util.logging.Logger.LOG;

/// PCL (Plain Craft Launcher) 版本格式支持工具类
/// 
/// PCL 使用标准的 .minecraft/versions 目录结构，与 HMCL 兼容。
/// 此类提供扫描、验证和导入 PCL 版本的功能。
public final class PCLVersionHelper {
    private PCLVersionHelper() {
    }

    /// 扫描 PCL 版本目录并返回所有有效版本
    /// 
    /// @param pclVersionsDir PCL 的 versions 目录路径
    /// @return 有效版本列表
    public static List<PCLVersionInfo> scanPCLVersions(Path pclVersionsDir) {
        List<PCLVersionInfo> versions = new ArrayList<>();

        if (!Files.isDirectory(pclVersionsDir)) {
            LOG.warning("PCL versions directory does not exist: " + pclVersionsDir);
            return versions;
        }

        try (Stream<Path> stream = Files.list(pclVersionsDir)) {
            stream.filter(Files::isDirectory).forEach(dir -> {
                String id = FileUtils.getName(dir);
                PCLVersionInfo versionInfo = analyzePCLVersion(dir, id);
                if (versionInfo != null && versionInfo.isValid()) {
                    versions.add(versionInfo);
                }
            });
        } catch (IOException e) {
            LOG.warning("Failed to scan PCL versions directory: " + pclVersionsDir, e);
        }

        return versions;
    }

    /// 分析单个 PCL 版本目录
    /// 
    /// @param versionDir 版本目录
    /// @param id 版本 ID
    /// @return 版本信息对象，如果无效则返回 null
    public static PCLVersionInfo analyzePCLVersion(Path versionDir, String id) {
        Path jsonFile = versionDir.resolve(id + ".json");
        Path jarFile = versionDir.resolve(id + ".jar");

        boolean hasJson = Files.exists(jsonFile);
        boolean hasJar = Files.exists(jarFile);

        // 检查是否有自定义 JSON 文件（任何 .json 文件）
        if (!hasJson) {
            try (Stream<Path> files = Files.list(versionDir)) {
                List<Path> jsonFiles = files.filter(p -> p.toString().endsWith(".json")).toList();
                if (jsonFiles.size() == 1) {
                    jsonFile = jsonFiles.get(0);
                    hasJson = true;
                    LOG.info("Found alternative JSON file for PCL version " + id + ": " + jsonFile);
                }
            } catch (IOException e) {
                LOG.warning("Failed to list files in PCL version directory: " + versionDir, e);
            }
        }

        Version version = null;
        if (hasJson) {
            try {
                version = JsonUtils.fromNonNullJson(Files.readString(jsonFile), Version.class);
            } catch (Exception e) {
                LOG.warning("Failed to parse PCL version JSON: " + jsonFile, e);
            }
        }

        return new PCLVersionInfo(id, versionDir, jsonFile, jarFile, hasJson, hasJar, version);
    }

    /// 检查 PCL 版本是否可以导入到 HMCL
    /// 
    /// @param versionInfo PCL 版本信息
    /// @return 如果可以导入则返回 true
    public static boolean canImport(PCLVersionInfo versionInfo) {
        return versionInfo.isValid() && versionInfo.hasJson();
    }

    /// 获取 PCL 版本目录中的所有版本 ID
    /// 
    /// @param pclVersionsDir PCL 的 versions 目录
    /// @return 版本 ID 列表
    public static List<String> listVersionIds(Path pclVersionsDir) {
        return scanPCLVersions(pclVersionsDir).stream()
                .map(PCLVersionInfo::id)
                .toList();
    }
}
