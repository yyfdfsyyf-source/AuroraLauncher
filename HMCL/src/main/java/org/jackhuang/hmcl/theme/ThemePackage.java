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
import javafx.scene.paint.Color;
import org.jackhuang.hmcl.util.io.IOUtils;

import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.*;

public final class ThemePackage {
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private static final Map<String, ThemePackage> PACKAGES = new LinkedHashMap<>();

    static {
        loadBuiltinPackages();
    }

    @SerializedName("id")
    private String id;

    @SerializedName("name")
    private String name;

    @SerializedName("version")
    private String version;

    @SerializedName("author")
    private String author;

    @SerializedName("description")
    private String description;

    @SerializedName("primaryColor")
    private String primaryColor;

    @SerializedName("layout")
    private LayoutConfig layout;

    @SerializedName("css")
    private String css;

    public static final class LayoutConfig {
        @SerializedName("cornerRadius")
        private double cornerRadius = 8.0;

        @SerializedName("buttonRadius")
        private double buttonRadius = 20.0;

        @SerializedName("cardRadius")
        private double cardRadius = 12.0;

        @SerializedName("inputRadius")
        private double inputRadius = 8.0;

        @SerializedName("borderWidth")
        private double borderWidth = 0.0;

        @SerializedName("borderColor")
        private String borderColor = null;

        // 字体配置
        @SerializedName("fontFamily")
        private String fontFamily = null;

        @SerializedName("fontSize")
        private Double fontSize = null;

        // 图标配置
        @SerializedName("iconSize")
        private Double iconSize = null;

        // 动画配置
        @SerializedName("animationEnabled")
        private Boolean animationEnabled = null;

        @SerializedName("animationDuration")
        private Double animationDuration = null;

        // UI 位置配置
        @SerializedName("sidebarPosition")
        private String sidebarPosition = null; // left, right

        @SerializedName("titlebarHeight")
        private Double titlebarHeight = null;

        @SerializedName("contentPadding")
        private Double contentPadding = null;

        public double getCornerRadius() { return cornerRadius; }
        public double getButtonRadius() { return buttonRadius; }
        public double getCardRadius() { return cardRadius; }
        public double getInputRadius() { return inputRadius; }
        public double getBorderWidth() { return borderWidth; }
        public String getBorderColor() { return borderColor; }
        public String getFontFamily() { return fontFamily; }
        public Double getFontSize() { return fontSize; }
        public Double getIconSize() { return iconSize; }
        public Boolean getAnimationEnabled() { return animationEnabled; }
        public Double getAnimationDuration() { return animationDuration; }
        public String getSidebarPosition() { return sidebarPosition; }
        public Double getTitlebarHeight() { return titlebarHeight; }
        public Double getContentPadding() { return contentPadding; }

        public String toCss() {
            StringBuilder css = new StringBuilder();
            
            css.append(String.format("""
                .jfx-button,
                .button-rippler {
                    -fx-background-radius: %.1f;
                }
                
                .card,
                .jfx-list-view,
                .jfx-scroll-pane {
                    -fx-background-radius: %.1f;
                }
                
                .jfx-text-field,
                .jfx-combo-box,
                .jfx-text-area {
                    -fx-background-radius: %.1f;
                }
                """,
                buttonRadius,
                cardRadius,
                inputRadius
            ));

            if (borderWidth > 0 && borderColor != null && !borderColor.isEmpty()) {
                css.append(String.format("""
                    
                    .jfx-button {
                        -fx-border-radius: %.1f;
                        -fx-border-width: %.1f;
                        -fx-border-color: %s;
                    }
                    
                    .card {
                        -fx-border-radius: %.1f;
                        -fx-border-width: %.1f;
                        -fx-border-color: %s;
                    }
                    """,
                    buttonRadius, borderWidth, borderColor,
                    cardRadius, borderWidth, borderColor
                ));
            }

            // 字体配置
            if (fontFamily != null || fontSize != null) {
                css.append("\n.root {\n");
                if (fontFamily != null) {
                    css.append(String.format("    -fx-font-family: %s;\n", fontFamily));
                }
                if (fontSize != null) {
                    css.append(String.format("    -fx-font-size: %.1fpx;\n", fontSize));
                }
                css.append("}\n");
            }

            // 图标大小配置
            if (iconSize != null) {
                css.append(String.format("""
                    
                    .svg-icon {
                        -fx-fit-width: %.1f;
                        -fx-fit-height: %.1f;
                    }
                    """,
                    iconSize, iconSize
                ));
            }

            // 内容间距配置
            if (contentPadding != null) {
                css.append(String.format("""
                    
                    .content-pane {
                        -fx-padding: %.1f;
                    }
                    """,
                    contentPadding
                ));
            }

            return css.toString();
        }
    }

    public ThemePackage() {
    }

    public String getId() {
        return id;
    }

    public void setId(String id) {
        this.id = id;
    }

    public String getName() {
        return name != null ? name : id;
    }

    public String getVersion() {
        return version;
    }

    public String getAuthor() {
        return author;
    }

    public String getDescription() {
        return description;
    }

    public Color getPrimaryColor() {
        if (primaryColor != null && !primaryColor.isEmpty()) {
            try {
                return Color.web(primaryColor);
            } catch (IllegalArgumentException e) {
                return ThemeColor.DEFAULT.color();
            }
        }
        return ThemeColor.DEFAULT.color();
    }

    public LayoutConfig getLayout() {
        return layout;
    }

    public String getLayoutCss() {
        return layout != null ? layout.toCss() : "";
    }

    public String getCss() {
        return css != null ? css : "";
    }

    public static Collection<ThemePackage> getPackages() {
        return PACKAGES.values();
    }

    public static ThemePackage getPackage(String id) {
        return PACKAGES.get(id);
    }

    public static ThemePackage getDefault() {
        return PACKAGES.get("default");
    }

    private static void loadBuiltinPackages() {
        String[] builtinIds = {"default", "sakura", "cyberpunk"};
        for (String id : builtinIds) {
            loadPackageFromResource(id);
        }
    }

    private static void loadPackageFromResource(String id) {
        try {
            InputStream stream = ThemePackage.class.getResourceAsStream("/assets/themes/" + id + ".hmclt");
            if (stream != null) {
                String json = IOUtils.readFullyAsString(stream, StandardCharsets.UTF_8);
                ThemePackage pkg = GSON.fromJson(json, ThemePackage.class);
                if (pkg != null && pkg.id != null) {
                    PACKAGES.put(pkg.id, pkg);
                    return;
                }
            }
        } catch (Exception e) {
        }
    }

    public String toJson() {
        return GSON.toJson(this);
    }

    public static ThemePackage fromJson(String json) {
        return GSON.fromJson(json, ThemePackage.class);
    }

    public static String getPackageJsonTemplate(String id, String name, String author, String description, String primaryColor, LayoutConfig layout, String css) {
        ThemePackage template = new ThemePackage();
        template.id = id;
        template.name = name;
        template.version = "1.0";
        template.author = author;
        template.description = description;
        template.primaryColor = primaryColor;
        template.layout = layout;
        template.css = css;
        return GSON.toJson(template);
    }

    @Override
    public String toString() {
        return name != null ? name : id;
    }
}
