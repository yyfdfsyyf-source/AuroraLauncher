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
package org.jackhuang.hmcl.setting;

import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import org.jackhuang.hmcl.mod.ModFavorite;
import org.jackhuang.hmcl.mod.RemoteMod;
import org.jackhuang.hmcl.mod.RemoteModRepository;

/// 模组收藏管理器
/// 管理用户收藏的模组列表，提供添加、删除、查询功能
public final class ModFavorites {

    private static final ObservableList<ModFavorite> favorites = FXCollections.observableArrayList();

    static {
        loadFavorites();
    }

    private ModFavorites() {
    }

    public static ObservableList<ModFavorite> getFavorites() {
        return favorites;
    }

    public static boolean isFavorited(RemoteMod mod) {
        return favorites.stream().anyMatch(f ->
                f.getSlug().equals(mod.getSlug()) &&
                        f.getRepoType().equals(mod.getRepositoryType().name()));
    }

    public static boolean isFavorited(String slug, String repoType) {
        return favorites.stream().anyMatch(f ->
                f.getSlug().equals(slug) && f.getRepoType().equals(repoType));
    }

    public static void addFavorite(RemoteMod mod) {
        ModFavorite favorite = ModFavorite.fromRemoteMod(mod);
        if (!favorites.contains(favorite)) {
            favorites.add(favorite);
            saveFavorites();
        }
    }

    public static void removeFavorite(RemoteMod mod) {
        favorites.removeIf(f ->
                f.getSlug().equals(mod.getSlug()) &&
                        f.getRepoType().equals(mod.getRepositoryType().name()));
        saveFavorites();
    }

    public static void removeFavorite(String slug, String repoType) {
        favorites.removeIf(f -> f.getSlug().equals(slug) && f.getRepoType().equals(repoType));
        saveFavorites();
    }

    public static void toggleFavorite(RemoteMod mod) {
        if (isFavorited(mod)) {
            removeFavorite(mod);
        } else {
            addFavorite(mod);
        }
    }

    private static void loadFavorites() {
        Config config = ConfigHolder.config();
        if (config.getModFavorites() != null) {
            favorites.setAll(config.getModFavorites());
        }
    }

    private static void saveFavorites() {
        ConfigHolder.config().setModFavorites(FXCollections.observableArrayList(favorites));
    }
}
