/**
 * Hello Minecraft! Launcher
 * Copyright (C) 2025  huangyuhui <huanghongxun2008@126.com> and contributors
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
package org.jackhuang.hmcl.mod;

import java.util.List;

/// 收藏的模组信息
/// 用于序列化存储用户收藏的模组
public final class ModFavorite {

    private final String slug;
    private final String author;
    private final String title;
    private final String description;
    private final List<String> categories;
    private final String pageUrl;
    private final String iconUrl;
    private final String repoType;
    private final long favoritedAt;

    public ModFavorite(String slug, String author, String title, String description,
                       List<String> categories, String pageUrl, String iconUrl,
                       String repoType, long favoritedAt) {
        this.slug = slug;
        this.author = author;
        this.title = title;
        this.description = description;
        this.categories = categories;
        this.pageUrl = pageUrl;
        this.iconUrl = iconUrl;
        this.repoType = repoType;
        this.favoritedAt = favoritedAt;
    }

    public static ModFavorite fromRemoteMod(RemoteMod mod) {
        return new ModFavorite(
                mod.getSlug(),
                mod.getAuthor(),
                mod.getTitle(),
                mod.getDescription(),
                mod.getCategories(),
                mod.getPageUrl(),
                mod.getIconUrl(),
                mod.getRepositoryType().name(),
                System.currentTimeMillis()
        );
    }

    public String getSlug() {
        return slug;
    }

    public String getAuthor() {
        return author;
    }

    public String getTitle() {
        return title;
    }

    public String getDescription() {
        return description;
    }

    public List<String> getCategories() {
        return categories;
    }

    public String getPageUrl() {
        return pageUrl;
    }

    public String getIconUrl() {
        return iconUrl;
    }

    public String getRepoType() {
        return repoType;
    }

    public long getFavoritedAt() {
        return favoritedAt;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        ModFavorite that = (ModFavorite) o;
        return slug.equals(that.slug) && repoType.equals(that.repoType);
    }

    @Override
    public int hashCode() {
        return 31 * slug.hashCode() + repoType.hashCode();
    }
}
