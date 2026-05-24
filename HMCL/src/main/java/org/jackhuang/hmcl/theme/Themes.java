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

import com.sun.jna.Pointer;
import javafx.animation.Interpolator;
import javafx.animation.KeyFrame;
import javafx.animation.KeyValue;
import javafx.animation.Timeline;
import javafx.beans.Observable;
import javafx.beans.binding.Bindings;
import javafx.beans.binding.BooleanBinding;
import javafx.beans.binding.ObjectBinding;
import javafx.beans.binding.ObjectExpression;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.SimpleObjectProperty;
import javafx.beans.value.ChangeListener;
import javafx.beans.value.ObservableValue;
import javafx.event.EventHandler;
import javafx.scene.paint.Color;
import javafx.stage.Stage;
import javafx.stage.WindowEvent;
import javafx.util.Duration;
import org.glavo.monetfx.Brightness;
import org.glavo.monetfx.ColorScheme;
import org.glavo.monetfx.Contrast;
import org.glavo.monetfx.beans.property.ColorSchemeProperty;
import org.glavo.monetfx.beans.property.ReadOnlyColorSchemeProperty;
import org.glavo.monetfx.beans.property.SimpleColorSchemeProperty;
import org.jackhuang.hmcl.ui.FXUtils;
import org.jackhuang.hmcl.ui.MacOSNativeUtils;
import org.jackhuang.hmcl.ui.WindowsNativeUtils;
import org.jackhuang.hmcl.util.io.FileUtils;
import org.jackhuang.hmcl.util.platform.NativeUtils;
import org.jackhuang.hmcl.util.platform.OSVersion;
import org.jackhuang.hmcl.util.platform.OperatingSystem;
import org.jackhuang.hmcl.util.platform.SystemUtils;
import org.jackhuang.hmcl.util.platform.windows.Dwmapi;
import org.jackhuang.hmcl.util.platform.windows.WinConstants;
import org.jackhuang.hmcl.util.platform.windows.WinReg;
import org.jackhuang.hmcl.util.platform.windows.WinTypes;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Objects;

import static org.jackhuang.hmcl.setting.ConfigHolder.config;
import static org.jackhuang.hmcl.util.logging.Logger.LOG;

/// @author Glavo
public final class Themes {

    /// 主题切换动画持续时间（毫秒）
    private static final Duration THEME_TRANSITION_DURATION = Duration.millis(300);
    
    /// 当前活动的主题过渡动画
    private static Timeline activeThemeTransition;
    
    /// 用于平滑过渡的当前主题色属性
    private static final ObjectProperty<Color> currentAnimatedColor = new SimpleObjectProperty<>(ThemeColor.DEFAULT.color());

    private static final ObjectExpression<Theme> theme = new ObjectBinding<>() {
        {
            List<Observable> observables = new ArrayList<>();

            observables.add(config().themeBrightnessProperty());
            observables.add(config().themeColorProperty());
            observables.add(config().themeLightnessProperty());
            if (FXUtils.DARK_MODE != null) {
                observables.add(FXUtils.DARK_MODE);
            }
            bind(observables.toArray(new Observable[0]));
        }

        private Brightness getBrightness() {
            String themeBrightness = config().getThemeBrightness();
            if (themeBrightness == null)
                return Brightness.DEFAULT;

            return switch (themeBrightness.toLowerCase(Locale.ROOT).trim()) {
                case "auto" -> {
                    if (FXUtils.DARK_MODE != null) {
                        yield FXUtils.DARK_MODE.get() ? Brightness.DARK : Brightness.LIGHT;
                    } else {
                        yield getDefaultBrightness();
                    }
                }
                case "dark" -> Brightness.DARK;
                case "light" -> Brightness.LIGHT;
                default -> Brightness.DEFAULT;
            };
        }

        @Override
        protected Theme computeValue() {
            ThemeColor themeColor = Objects.requireNonNullElse(config().getThemeColor(), ThemeColor.DEFAULT);
            Brightness brightness = getBrightness();
            
            Contrast contrast = brightness == Brightness.DARK ? Contrast.HIGH : Contrast.DEFAULT;

            Color originalColor = themeColor.color();
            double lightnessMultiplier = config().getThemeLightness();
            
            double saturation = 1.0;
            if (brightness == Brightness.DARK) {
                lightnessMultiplier *= 1.15;
                saturation = 0.9;
            } else {
                saturation = 0.85;
            }
            
            Color adjustedColor = originalColor.deriveColor(0, saturation, lightnessMultiplier, 1);
            themeColor = new ThemeColor(themeColor.name(), adjustedColor);

            return new Theme(themeColor, brightness, Theme.DEFAULT.colorStyle(), contrast);
        }
    };
    private static final ColorSchemeProperty colorScheme = new SimpleColorSchemeProperty();
    private static final BooleanBinding darkMode = Bindings.createBooleanBinding(
            () -> colorScheme.get().getBrightness() == Brightness.DARK,
            colorScheme
    );

    static {
        ChangeListener<Theme> listener = (observable, oldValue, newValue) -> {
            if (!Objects.equals(oldValue, newValue)) {
                // 停止之前的动画
                if (activeThemeTransition != null) {
                    activeThemeTransition.stop();
                }
                
                // 获取旧颜色和新颜色
                Color oldColor = oldValue != null ? oldValue.primaryColorSeed().color() : ThemeColor.DEFAULT.color();
                Color newColor = newValue != null ? newValue.primaryColorSeed().color() : ThemeColor.DEFAULT.color();
                
                // 更新当前动画颜色
                currentAnimatedColor.set(oldColor);
                
                // 立即设置颜色方案（避免 null）
                colorScheme.set(newValue != null ? newValue.toColorScheme() : Theme.DEFAULT.toColorScheme());
                
                // 创建颜色过渡动画
                activeThemeTransition = new Timeline();
                KeyValue keyValue = new KeyValue(currentAnimatedColor, newColor, Interpolator.EASE_BOTH);
                KeyFrame keyFrame = new KeyFrame(THEME_TRANSITION_DURATION, keyValue);
                activeThemeTransition.getKeyFrames().add(keyFrame);
                
                // 动画完成后应用 AtlantaFX 主题
                activeThemeTransition.setOnFinished(event -> {
                    applyAtlantaFXTheme();
                });
                
                // 启动动画
                activeThemeTransition.play();
            }
        };
        listener.changed(theme, null, theme.get());
        theme.addListener(listener);
    }

    private static void applyAtlantaFXTheme() {
        if (config().getUiLibrary() == org.jackhuang.hmcl.setting.UILibrary.ATLANTAFX) {
            String brightness = config().getThemeBrightness();
            if ("dark".equalsIgnoreCase(brightness) || ("auto".equalsIgnoreCase(brightness) && FXUtils.DARK_MODE != null && FXUtils.DARK_MODE.get())) {
                javafx.application.Application.setUserAgentStylesheet(new atlantafx.base.theme.PrimerDark().getUserAgentStylesheet());
            } else {
                javafx.application.Application.setUserAgentStylesheet(new atlantafx.base.theme.PrimerLight().getUserAgentStylesheet());
            }
        }
    }

    private static Brightness defaultBrightness;

    private static Brightness getDefaultBrightness() {
        if (defaultBrightness != null)
            return defaultBrightness;

        LOG.info("Detecting system theme brightness");
        Brightness brightness = Brightness.DEFAULT;
        if (OperatingSystem.CURRENT_OS == OperatingSystem.WINDOWS) {
            WinReg reg = WinReg.INSTANCE;
            if (reg != null) {
                Object appsUseLightTheme = reg.queryValue(WinReg.HKEY.HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "AppsUseLightTheme");
                if (appsUseLightTheme instanceof Integer value) {
                    brightness = value == 0 ? Brightness.DARK : Brightness.LIGHT;
                }
            }
        } else if (OperatingSystem.CURRENT_OS == OperatingSystem.MACOS) {
            try {
                String result = SystemUtils.run("/usr/bin/defaults", "read", "-g", "AppleInterfaceStyle").trim();
                brightness = "Dark".equalsIgnoreCase(result) ? Brightness.DARK : Brightness.LIGHT;
            } catch (Exception e) {
                // If the key does not exist, it means Light mode is used
                brightness = Brightness.LIGHT;
            }
        } else if (OperatingSystem.CURRENT_OS == OperatingSystem.LINUX) {
            Path dbusSend = SystemUtils.which("dbus-send");
            if (dbusSend != null) {
                try {
                    String[] result = SystemUtils.run(List.of(
                            FileUtils.getAbsolutePath(dbusSend),
                            "--session",
                            "--print-reply=literal",
                            "--reply-timeout=1000",
                            "--dest=org.freedesktop.portal.Desktop",
                            "/org/freedesktop/portal/desktop",
                            "org.freedesktop.portal.Settings.Read",
                            "string:org.freedesktop.appearance",
                            "string:color-scheme"
                    ), java.time.Duration.ofSeconds(2)).trim().split(" ");

                    if (result.length > 0) {
                        String value = result[result.length - 1];
                        // 1: prefer dark
                        // 2: prefer light
                        if ("1".equals(value)) {
                            brightness = Brightness.DARK;
                        } else if ("2".equals(value)) {
                            brightness = Brightness.LIGHT;
                        }
                    }
                } catch (Exception e) {
                    LOG.warning("Failed to get system theme from D-Bus", e);
                }
            }
        }
        LOG.info("Detected system theme brightness: " + brightness);

        return defaultBrightness = brightness;
    }

    public static ObjectExpression<Theme> themeProperty() {
        return theme;
    }

    public static Theme getTheme() {
        return themeProperty().get();
    }
    
    /// 获取当前动画过渡中的主题色
    /// @return 当前正在显示的颜色（可能在旧颜色和新颜色之间）
    public static ObjectProperty<Color> currentAnimatedColorProperty() {
        return currentAnimatedColor;
    }
    
    /// 获取当前动画过渡中的主题色
    public static Color getCurrentAnimatedColor() {
        return currentAnimatedColor.get();
    }

    public static ReadOnlyColorSchemeProperty colorSchemeProperty() {
        return colorScheme;
    }

    public static ColorScheme getColorScheme() {
        return colorScheme.get();
    }

    private static final ObjectBinding<Color> titleFill = Bindings.createObjectBinding(
            () -> config().isTitleTransparent()
                    ? getColorScheme().getOnSurface()
                    : getColorScheme().getOnPrimaryContainer(),
            colorSchemeProperty(),
            config().titleTransparentProperty()
    );

    public static ObservableValue<Color> titleFillProperty() {
        return titleFill;
    }

    public static BooleanBinding darkModeProperty() {
        return darkMode;
    }

    public static void applyNativeDarkMode(Stage stage) {
        if (OperatingSystem.SYSTEM_VERSION.isAtLeast(OSVersion.WINDOWS_11) && NativeUtils.USE_JNA && Dwmapi.INSTANCE != null) {
            ChangeListener<Boolean> listener = FXUtils.onWeakChange(Themes.darkModeProperty(), darkMode -> {
                if (stage.isShowing()) {
                    WindowsNativeUtils.getWindowHandle(stage).ifPresent(handle -> {
                        if (handle == WinTypes.HANDLE.INVALID_VALUE)
                            return;

                        Dwmapi.INSTANCE.DwmSetWindowAttribute(
                                new WinTypes.HANDLE(Pointer.createConstant(handle)),
                                WinConstants.DWMWA_USE_IMMERSIVE_DARK_MODE,
                                new WinTypes.BOOLByReference(new WinTypes.BOOL(darkMode)),
                                WinTypes.BOOL.SIZE
                        );
                    });
                }
            });
            stage.getProperties().put("Themes.applyNativeDarkMode.listener", listener);

            if (stage.isShowing()) {
                listener.changed(null, false, Themes.darkModeProperty().get());
            } else {
                stage.addEventFilter(WindowEvent.WINDOW_SHOWN, new EventHandler<>() {
                    @Override
                    public void handle(WindowEvent event) {
                        stage.removeEventFilter(WindowEvent.WINDOW_SHOWN, this);
                        listener.changed(null, false, Themes.darkModeProperty().get());
                    }
                });
            }
        } else if (OperatingSystem.CURRENT_OS == OperatingSystem.MACOS && MacOSNativeUtils.isSupported()) {
            MacOSNativeUtils.setAppearance(darkModeProperty().get());

            ChangeListener<Boolean> listener = FXUtils.onWeakChange(Themes.darkModeProperty(), MacOSNativeUtils::setAppearance);
            stage.getProperties().put("Themes.applyNativeDarkMode.listener", listener);
        }
    }

    private Themes() {
    }
}
