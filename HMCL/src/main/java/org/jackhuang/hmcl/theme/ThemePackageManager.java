/*
 * Hello Minecraft! Launcher
 * Copyright (C) 2025 huangyuhui <huanghongxun2008@126.com> and contributors
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
package org.jackhuang.hmcl.theme;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.annotations.SerializedName;
import org.jackhuang.hmcl.util.io.IOUtils;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public final class ThemePackageManager {
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private static final Path USER_THEMES_DIR;
    private static final Path USER_PACKAGES_FILE;
    private static final Map<String, ThemePackage> USER_PACKAGES = new LinkedHashMap<>();

    static {
        Path configDir = org.jackhuang.hmcl.setting.ConfigHolder.configLocation().getParent();
        USER_THEMES_DIR = configDir.resolve("themes");
        USER_PACKAGES_FILE = configDir.resolve("theme_packages.json");
        
        try {
            Files.createDirectories(USER_THEMES_DIR);
        } catch (IOException e) {
            e.printStackTrace();
        }
        
        loadUserPackages();
    }

    public static Collection<ThemePackage> getAllPackages() {
        Collection<ThemePackage> builtin = ThemePackage.getPackages();
        Collection<ThemePackage> user = USER_PACKAGES.values();
        
        List<ThemePackage> all = new ArrayList<>(builtin.size() + user.size());
        all.addAll(builtin);
        all.addAll(user);
        return all;
    }

    public static String[] getAllPackageIds() {
        Collection<ThemePackage> all = getAllPackages();
        return all.stream()
                .map(ThemePackage::getId)
                .toArray(String[]::new);
    }

    public static ThemePackage getPackage(String id) {
        ThemePackage pkg = ThemePackage.getPackage(id);
        if (pkg != null) {
            return pkg;
        }
        return USER_PACKAGES.get(id);
    }

    public static ThemePackage importPackage(Path filePath) throws IOException {
        String json = Files.readString(filePath, StandardCharsets.UTF_8);
        ThemePackage pkg = ThemePackage.fromJson(json);
        
        if (pkg == null || pkg.getId() == null || pkg.getId().isEmpty()) {
            throw new IOException("Invalid theme package: missing id");
        }

        String id = pkg.getId();
        if (ThemePackage.getPackage(id) != null || USER_PACKAGES.containsKey(id)) {
            id = id + "_user_" + System.currentTimeMillis();
            pkg = ThemePackage.fromJson(json);
            pkg.setId(id);
        }

        Path targetPath = USER_THEMES_DIR.resolve(id + ".hmclt");
        Files.writeString(targetPath, json, StandardCharsets.UTF_8);
        
        USER_PACKAGES.put(pkg.getId(), pkg);
        saveUserPackages();
        
        return pkg;
    }

    public static boolean removePackage(String id) {
        if (ThemePackage.getPackage(id) != null) {
            return false;
        }
        
        ThemePackage pkg = USER_PACKAGES.remove(id);
        if (pkg != null) {
            Path filePath = USER_THEMES_DIR.resolve(id + ".hmclt");
            try {
                Files.deleteIfExists(filePath);
            } catch (IOException e) {
                e.printStackTrace();
            }
            saveUserPackages();
            return true;
        }
        return false;
    }

    public static boolean isUserPackage(String id) {
        return USER_PACKAGES.containsKey(id);
    }

    private static void loadUserPackages() {
        USER_PACKAGES.clear();
        
        if (!Files.exists(USER_PACKAGES_FILE)) {
            loadFromDirectory();
            return;
        }
        
        try {
            String json = Files.readString(USER_PACKAGES_FILE, StandardCharsets.UTF_8);
            UserPackagesData data = GSON.fromJson(json, UserPackagesData.class);
            
            if (data != null && data.packages != null) {
                for (UserPackageInfo info : data.packages) {
                    Path filePath = USER_THEMES_DIR.resolve(info.id + ".hmclt");
                    if (Files.exists(filePath)) {
                        try {
                            String packageJson = Files.readString(filePath, StandardCharsets.UTF_8);
                            ThemePackage pkg = ThemePackage.fromJson(packageJson);
                            if (pkg != null && pkg.getId() != null) {
                                USER_PACKAGES.put(pkg.getId(), pkg);
                            }
                        } catch (Exception e) {
                            e.printStackTrace();
                        }
                    }
                }
            }
        } catch (IOException e) {
            e.printStackTrace();
            loadFromDirectory();
        }
    }

    private static void loadFromDirectory() {
        try {
            try (var stream = Files.list(USER_THEMES_DIR)) {
                stream.filter(p -> p.toString().endsWith(".hmclt"))
                      .forEach(filePath -> {
                          try {
                              String json = Files.readString(filePath, StandardCharsets.UTF_8);
                              ThemePackage pkg = ThemePackage.fromJson(json);
                              if (pkg != null && pkg.getId() != null) {
                                  USER_PACKAGES.put(pkg.getId(), pkg);
                              }
                          } catch (Exception e) {
                              e.printStackTrace();
                          }
                      });
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private static void saveUserPackages() {
        try {
            UserPackagesData data = new UserPackagesData();
            data.packages = new ArrayList<>();
            for (ThemePackage pkg : USER_PACKAGES.values()) {
                UserPackageInfo info = new UserPackageInfo();
                info.id = pkg.getId();
                data.packages.add(info);
            }
            String json = GSON.toJson(data);
            Files.writeString(USER_PACKAGES_FILE, json, StandardCharsets.UTF_8);
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private static class UserPackagesData {
        @SerializedName("packages")
        List<UserPackageInfo> packages;
    }

    private static class UserPackageInfo {
        @SerializedName("id")
        String id;
    }
}
