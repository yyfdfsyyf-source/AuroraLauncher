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
import org.jetbrains.annotations.Nullable;

import java.nio.file.Path;

/// PCL 版本信息数据类
/// 
/// 封装 PCL 版本的元数据，包括文件位置和有效性检查。
public record PCLVersionInfo(
        String id,
        Path versionDir,
        Path jsonFile,
        Path jarFile,
        boolean hasJson,
        boolean hasJar,
        @Nullable Version version
) {
    /// 检查版本是否有效（至少有 JSON 文件）
    public boolean isValid() {
        return hasJson && jsonFile != null;
    }

    /// 检查是否有完整的文件（JSON + JAR）
    public boolean isComplete() {
        return hasJson && hasJar;
    }

    /// 获取 Minecraft 游戏版本（如果可用）
    @Nullable
    public String getGameVersion() {
        if (version == null) {
            return null;
        }
        return version.getMinecraftArguments() != null ? "Unknown" : null;
    }

    @Override
    public String toString() {
        return "PCLVersionInfo{" +
                "id='" + id + '\'' +
                ", hasJson=" + hasJson +
                ", hasJar=" + hasJar +
                ", isValid=" + isValid() +
                '}';
    }
}
