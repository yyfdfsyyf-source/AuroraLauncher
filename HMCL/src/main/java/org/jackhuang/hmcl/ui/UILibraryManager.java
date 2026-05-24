/*
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
package org.jackhuang.hmcl.ui;

import atlantafx.base.theme.PrimerDark;
import atlantafx.base.theme.PrimerLight;
import javafx.application.Application;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.SimpleObjectProperty;
import javafx.scene.Scene;
import javafx.scene.control.Skin;
import org.jackhuang.hmcl.setting.UILibrary;

import java.io.File;
import java.nio.file.Path;

import static org.jackhuang.hmcl.setting.ConfigHolder.config;

public final class UILibraryManager {

    private static final ObjectProperty<UILibrary> currentLibrary = new SimpleObjectProperty<>();
    private static Scene scene;

    static {
        currentLibrary.set(config().getUiLibrary());
        config().uiLibraryProperty().addListener((observable, oldValue, newValue) -> {
            if (newValue != null) {
                currentLibrary.set(newValue);
                applyTheme();
            }
        });
        config().customCssPathProperty().addListener((observable, oldValue, newValue) -> {
            applyCustomCss();
        });
    }

    private UILibraryManager() {
    }

    public static void setScene(Scene scene) {
        UILibraryManager.scene = scene;
        applyTheme();
        applyUIPolish();
        applyCustomCss();
    }

    public static UILibrary getCurrentLibrary() {
        return currentLibrary.get();
    }

    public static ObjectProperty<UILibrary> currentLibraryProperty() {
        return currentLibrary;
    }

    public static boolean isJFoenix() {
        // JFoenix 不再作为主要 UI 库，仅保留特殊组件
        return false;
    }

    public static boolean isAtlantaFX() {
        return currentLibrary.get() == UILibrary.ATLANTAFX;
    }

    public static void setLibrary(UILibrary library) {
        config().setUiLibrary(library);
    }

    public static void applyTheme() {
        if (isAtlantaFX()) {
            String brightness = config().getThemeBrightness();
            boolean isDark = "dark".equalsIgnoreCase(brightness);
            if ("auto".equalsIgnoreCase(brightness) && FXUtils.DARK_MODE != null) {
                isDark = FXUtils.DARK_MODE.get();
            }
            if (isDark) {
                Application.setUserAgentStylesheet(new PrimerDark().getUserAgentStylesheet());
            } else {
                Application.setUserAgentStylesheet(new PrimerLight().getUserAgentStylesheet());
            }
            applyAtlantaFXFixes(true);
        } else {
            Application.setUserAgentStylesheet(Application.STYLESHEET_MODENA);
            applyAtlantaFXFixes(false);
        }
    }

    private static void applyAtlantaFXFixes(boolean enable) {
        if (scene == null) return;

        String fixesCss = "/assets/css/atlantafx-fixes.css";
        boolean hasFixes = scene.getStylesheets().contains(fixesCss);

        if (enable && !hasFixes) {
            scene.getStylesheets().add(fixesCss);
        } else if (!enable && hasFixes) {
            scene.getStylesheets().remove(fixesCss);
        }
    }

    private static void applyUIPolish() {
        if (scene == null) return;

        String polishCss = "/assets/css/ui-polish.css";
        if (!scene.getStylesheets().contains(polishCss)) {
            scene.getStylesheets().add(polishCss);
        }
    }

    private static void applyCustomCss() {
        if (scene == null) return;

        String cssPath = config().getCustomCssPath();
        String currentCustomCss = (String) scene.getProperties().get("hmcl.custom.css");

        if (currentCustomCss != null && !currentCustomCss.isEmpty()) {
            scene.getStylesheets().remove(currentCustomCss);
            scene.getProperties().remove("hmcl.custom.css");
        }

        if (cssPath != null && !cssPath.isEmpty()) {
            File cssFile = new File(cssPath);
            if (cssFile.exists() && cssFile.isFile()) {
                String url = cssFile.toURI().toString();
                scene.getStylesheets().add(url);
                scene.getProperties().put("hmcl.custom.css", url);
            }
        }
    }

    /// 在AtlantaFX模式下返回null（使用父类默认Skin），JFoenix模式下返回指定Skin
    public static Skin<?> selectSkin(Skin<?> jfxSkin) {
        return isJFoenix() ? jfxSkin : null;
    }

    /// 通过反射创建JFoenix Skin，避免编译时依赖
    /// AtlantaFX 模式下返回对应的兼容 Skin
    @SuppressWarnings("unchecked")
    public static Skin<?> createSkin(String skinClassName, javafx.scene.control.Skinnable control) {
        if (isAtlantaFX()) {
            return createAtlantaFXSkin(skinClassName, control);
        }
        try {
            Class<?> skinClass = Class.forName(skinClassName);
            return (Skin<?>) skinClass.getConstructor(control.getClass()).newInstance(control);
        } catch (Exception e) {
            return null;
        }
    }

    /// 为 AtlantaFX 模式创建兼容 Skin
    @SuppressWarnings("unchecked")
    private static Skin<?> createAtlantaFXSkin(String skinClassName, javafx.scene.control.Skinnable control) {
        try {
            if (skinClassName.contains("JFXPopupSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXPopupSkin");
                return (Skin<?>) skinClass.getConstructor(com.jfoenix.controls.JFXPopup.class).newInstance(control);
            } else if (skinClassName.contains("JFXComboBoxListViewSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXComboBoxSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.ComboBox.class).newInstance(control);
            } else if (skinClassName.contains("JFXListViewSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXListViewSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.ListView.class).newInstance(control);
            } else if (skinClassName.contains("JFXToggleButtonSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXToggleButtonSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.ToggleButton.class).newInstance(control);
            } else if (skinClassName.contains("JFXCheckBoxSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXCheckBoxSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.CheckBox.class).newInstance(control);
            } else if (skinClassName.contains("JFXTextFieldSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXTextFieldSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.TextField.class).newInstance(control);
            } else if (skinClassName.contains("JFXSpinnerSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXSpinnerSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.ProgressIndicator.class).newInstance(control);
            } else if (skinClassName.contains("JFXButtonSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXButtonSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.Button.class).newInstance(control);
            } else if (skinClassName.contains("JFXProgressBarSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXProgressBarSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.ProgressBar.class).newInstance(control);
            } else if (skinClassName.contains("JFXSliderSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXSliderSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.Slider.class).newInstance(control);
            } else if (skinClassName.contains("JFXTextAreaSkin")) {
                Class<?> skinClass = Class.forName("com.jfoenix.skins.compat.AtlantaFXTextAreaSkin");
                return (Skin<?>) skinClass.getConstructor(javafx.scene.control.TextArea.class).newInstance(control);
            }
        } catch (Exception e) {
            // ignore
        }
        return null;
    }
}
