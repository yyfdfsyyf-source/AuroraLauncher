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

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.jackhuang.hmcl.util.logging.Logger.LOG;

/// PCL 版本兼容性修复工具
/// 
/// 修复 PCL 安装的版本在 HMCL 中无法识别的问题。
/// 主要问题是 PCL 的版本 JSON 可能缺少 HMCL 需要的某些字段。
public final class PCLVersionCompatibility {
    private PCLVersionCompatibility() {
    }

    /// 修复 PCL 版本的 JSON 文件，使其兼容 HMCL
    /// 
    /// 主要修复：
    /// 1. 确保 `root` 字段为 true（表示这是根版本）
    /// 2. 确保 `id` 字段与文件夹名称一致
    /// 3. 确保 `jar` 字段正确设置
    /// 
    /// @param jsonFile PCL 版本的 JSON 文件路径
    /// @param versionId 版本 ID（文件夹名称）
    /// @return 如果修复成功返回 true
    public static boolean fixPCLVersionJson(Path jsonFile, String versionId) {
        if (!Files.exists(jsonFile)) {
            LOG.warning("PCL version JSON file not found: " + jsonFile);
            return false;
        }

        try {
            // 读取 JSON 文件
            String jsonText = Files.readString(jsonFile);
            
            // 尝试解析为 Version 对象
            Version version = JsonUtils.fromNonNullJson(jsonText, Version.class);
            
            // 检查是否需要修复
            boolean needsFix = false;
            
            // 修复 1: 确保 root 为 true
            if (!version.isRoot()) {
                LOG.info("Fixing PCL version " + versionId + ": setting root=true");
                version = version.setRoot(true);
                needsFix = true;
            }
            
            // 修复 2: 确保 id 正确
            if (!versionId.equals(version.getId())) {
                LOG.info("Fixing PCL version " + versionId + ": correcting id from " + version.getId() + " to " + versionId);
                version = version.setId(versionId);
                needsFix = true;
            }
            
            // 修复 3: 确保 jar 正确（如果没有设置，使用 id）
            if (version.getJar() == null || version.getJar().isEmpty()) {
                LOG.info("Fixing PCL version " + versionId + ": setting jar=" + versionId);
                version = version.setJar(versionId);
                needsFix = true;
            }
            
            // 如果需要修复，写回文件
            if (needsFix) {
                String fixedJson = JsonUtils.GSON.toJson(version);
                Files.writeString(jsonFile, fixedJson);
                LOG.info("Successfully fixed PCL version JSON: " + jsonFile);
                return true;
            } else {
                LOG.info("PCL version " + versionId + " is already compatible with HMCL");
                return true;
            }
            
        } catch (Exception e) {
            LOG.warning("Failed to fix PCL version JSON: " + jsonFile, e);
            return false;
        }
    }

    /// 检查 PCL 版本是否需要修复
    /// 
    /// @param jsonFile PCL 版本的 JSON 文件
    /// @param versionId 版本 ID
    /// @return 如果需要修复返回 true
    public static boolean needsFix(Path jsonFile, String versionId) {
        if (!Files.exists(jsonFile)) {
            return true;
        }

        try {
            Version version = JsonUtils.fromNonNullJson(Files.readString(jsonFile), Version.class);
            
            // 检查 root 是否为 true
            if (!version.isRoot()) {
                return true;
            }
            
            // 检查 id 是否匹配
            if (!versionId.equals(version.getId())) {
                return true;
            }
            
            // 检查 jar 是否设置
            if (version.getJar() == null || version.getJar().isEmpty()) {
                return true;
            }
            
            return false;
            
        } catch (Exception e) {
            LOG.warning("Failed to check if PCL version needs fix: " + jsonFile, e);
            return true; // 如果解析失败，认为需要修复
        }
    }
}
