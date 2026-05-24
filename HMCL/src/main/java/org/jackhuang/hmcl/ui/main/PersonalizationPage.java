/*
 * Hello Minecraft! Launcher
 * Copyright (C) 2021  huangyuhui <huanghongxun2008@126.com> and contributors
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
package org.jackhuang.hmcl.ui.main;

import com.jfoenix.controls.*;
import com.jfoenix.effects.JFXDepthManager;
import javafx.application.Platform;
import javafx.beans.binding.Bindings;
import javafx.beans.binding.StringBinding;
import javafx.beans.binding.When;
import javafx.beans.value.ChangeListener;
import javafx.beans.value.ObservableValue;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.control.Button;
import javafx.scene.control.ColorPicker;
import javafx.scene.control.Label;
import javafx.scene.control.ScrollPane;
import javafx.scene.layout.*;
import javafx.scene.text.Font;
import javafx.scene.text.FontSmoothingType;
import javafx.stage.FileChooser;
import org.jackhuang.hmcl.setting.EnumBackgroundImage;
import org.jackhuang.hmcl.setting.FontManager;
import org.jackhuang.hmcl.setting.UILibrary;
import org.jackhuang.hmcl.theme.ThemeColor;
import org.jackhuang.hmcl.theme.ThemePackage;
import org.jackhuang.hmcl.theme.ThemePackageManager;
import org.jackhuang.hmcl.theme.Themes;
import org.jackhuang.hmcl.ui.FXUtils;
import org.jackhuang.hmcl.ui.SVG;
import org.jackhuang.hmcl.ui.construct.*;
import org.jackhuang.hmcl.util.Lang;
import org.jackhuang.hmcl.util.javafx.SafeStringConverter;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Locale;
import java.util.Optional;

import static org.jackhuang.hmcl.setting.ConfigHolder.config;
import static org.jackhuang.hmcl.setting.ConfigHolder.globalConfig;
import static org.jackhuang.hmcl.util.i18n.I18n.i18n;

public class PersonalizationPage extends StackPane {

    private void showAlert(javafx.scene.control.Alert.AlertType type, String title, String content) {
        javafx.scene.control.Alert alert = new javafx.scene.control.Alert(type);
        alert.setTitle(title);
        alert.setHeaderText(null);
        alert.setContentText(content);
        alert.showAndWait();
    }

    private static int snapOpacity(double val) {
        if (val <= 0) {
            return 0;
        } else if (Double.isNaN(val) || val >= 100.) {
            return 100;
        }

        int prevTick = (int) (val / 5);
        int prevTickValue = prevTick * 5;
        int nextTickValue = (prevTick + 1) * 5;

        return (val - prevTickValue) > (nextTickValue - val) ? nextTickValue : prevTickValue;
    }

    public PersonalizationPage() {
        VBox content = new VBox(10);
        content.setPadding(new Insets(10));
        content.setFillWidth(true);
        ScrollPane scrollPane = new ScrollPane(content);
        FXUtils.smoothScrolling(scrollPane);
        scrollPane.setFitToWidth(true);
        getChildren().setAll(scrollPane);

        ComponentList themeList = new ComponentList();
        {
            var brightnessPane = new LineSelectButton<String>();
            brightnessPane.setTitle(i18n("settings.launcher.brightness"));
            brightnessPane.setNullSafeConverter(name -> i18n("settings.launcher.brightness." + name));
            brightnessPane.setItems("auto", "light", "dark");
            brightnessPane.valueProperty().bindBidirectional(config().themeBrightnessProperty());

            themeList.getContent().add(brightnessPane);
        }

        {
            var uiLibraryPane = new LineSelectButton<UILibrary>();
            uiLibraryPane.setTitle(i18n("settings.launcher.ui_library"));
            uiLibraryPane.setNullSafeConverter(library -> i18n("settings.launcher.ui_library." + library.name().toLowerCase()));
            uiLibraryPane.setItems(UILibrary.values());
            uiLibraryPane.valueProperty().bindBidirectional(config().uiLibraryProperty());
            uiLibraryPane.valueProperty().addListener((observable, oldValue, newValue) -> {
                if (newValue != null) {
                    org.jackhuang.hmcl.ui.UILibraryManager.applyTheme();
                }
            });

            themeList.getContent().add(uiLibraryPane);

            Label warningLabel = new Label(i18n("settings.launcher.ui_library.atlantafx.warning"));
            warningLabel.setWrapText(true);
            warningLabel.getStyleClass().add("warning-label");
            warningLabel.visibleProperty().bind(
                    uiLibraryPane.valueProperty().isEqualTo(UILibrary.ATLANTAFX));
            warningLabel.managedProperty().bind(warningLabel.visibleProperty());
            themeList.getContent().add(warningLabel);
        }

        {
            BorderPane themePane = new BorderPane();
            themeList.getContent().add(themePane);

            Label left = new Label(i18n("settings.launcher.theme"));
            BorderPane.setAlignment(left, Pos.CENTER_LEFT);
            themePane.setLeft(left);

            StackPane themeColorPickerContainer = new StackPane();
            themeColorPickerContainer.setMinHeight(30);
            themePane.setRight(themeColorPickerContainer);

            ColorPicker picker = new JFXColorPicker();
            picker.getCustomColors().setAll(ThemeColor.STANDARD_COLORS.stream().map(ThemeColor::color).toList());
            ThemeColor.bindBidirectional(picker, config().themeColorProperty());
            themeColorPickerContainer.getChildren().setAll(picker);
            Platform.runLater(() -> JFXDepthManager.setDepth(picker, 0));
        }
        {
            HBox lightnessItem = new HBox(8);
            lightnessItem.setAlignment(Pos.CENTER);
            themeList.getContent().add(lightnessItem);

            Label label = new Label("主题明度");

            JFXSlider slider = new JFXSlider(0.3, 2.0, config().getThemeLightness());
            slider.setShowTickMarks(true);
            slider.setMajorTickUnit(0.5);
            slider.setMinorTickCount(4);
            slider.setBlockIncrement(0.1);
            slider.setSnapToTicks(false);
            slider.setPadding(new Insets(9, 0, 0, 0));
            HBox.setHgrow(slider, Priority.ALWAYS);
            
            Label valueLabel = new Label();
            valueLabel.setMinWidth(50);
            valueLabel.textProperty().bind(Bindings.format("%.2f", slider.valueProperty()));
            
            slider.valueProperty().bindBidirectional(config().themeLightnessProperty());

            lightnessItem.getChildren().addAll(label, slider, valueLabel);
        }
        {
            var themePackagePane = new LineSelectButton<String>();
            themePackagePane.setTitle("主题包");
            themePackagePane.setNullSafeConverter(id -> {
                ThemePackage pkg = ThemePackageManager.getPackage(id);
                return pkg != null ? pkg.getName() : id;
            });
            themePackagePane.setItems(ThemePackageManager.getAllPackageIds());
            themePackagePane.valueProperty().bindBidirectional(config().themePackageIdProperty());
            
            themePackagePane.valueProperty().addListener((obs, oldVal, newVal) -> {
                if (newVal != null) {
                    ThemePackage pkg = ThemePackageManager.getPackage(newVal);
                    if (pkg != null && pkg.getPrimaryColor() != null) {
                        config().setThemeColor(ThemeColor.of(pkg.getPrimaryColor()));
                    }
                }
            });

            themeList.getContent().add(themePackagePane);
        }
        {
            HBox importBox = new HBox(8);
            importBox.setAlignment(Pos.CENTER_LEFT);
            themeList.getContent().add(importBox);
            
            Label importLabel = new Label("导入主题包");
            importLabel.getStyleClass().add("subtitle-label");
            
            JFXButton importButton = new JFXButton("从文件导入");
            importButton.setOnAction(e -> {
                FileChooser fileChooser = new FileChooser();
                fileChooser.setTitle("选择主题包文件");
                fileChooser.getExtensionFilters().add(
                    new FileChooser.ExtensionFilter("HMCL 主题包", "*.hmclt")
                );
                File file = fileChooser.showOpenDialog(this.getScene().getWindow());
                if (file != null) {
                    try {
                        ThemePackage pkg = ThemePackageManager.importPackage(file.toPath());
                        config().setThemePackageId(pkg.getId());
                        if (pkg.getPrimaryColor() != null) {
                            config().setThemeColor(ThemeColor.of(pkg.getPrimaryColor()));
                        }
                        showAlert(javafx.scene.control.Alert.AlertType.INFORMATION, "导入成功", "主题包 \"" + pkg.getName() + "\" 导入成功！");
                    } catch (IOException ex) {
                        showAlert(javafx.scene.control.Alert.AlertType.ERROR, "导入失败", "无法导入主题包：" + ex.getMessage());
                    }
                }
            });
            
            importBox.getChildren().addAll(importLabel, importButton);
        }
        {
            LineToggleButton titleTransparentButton = new LineToggleButton();
            themeList.getContent().add(titleTransparentButton);
            titleTransparentButton.selectedProperty().bindBidirectional(config().titleTransparentProperty());
            titleTransparentButton.setTitle(i18n("settings.launcher.title_transparent"));
        }
        {
            LineToggleButton animationButton = new LineToggleButton();
            themeList.getContent().add(animationButton);
            animationButton.selectedProperty().bindBidirectional(config().animationDisabledProperty());
            animationButton.setTitle(i18n("settings.launcher.turn_off_animations"));
            animationButton.setSubtitle(i18n("settings.take_effect_after_restart"));
        }
        content.getChildren().addAll(ComponentList.createComponentListTitle(i18n("settings.launcher.appearance")), themeList);

        {
            ComponentList componentList = new ComponentList();

            MultiFileItem<EnumBackgroundImage> backgroundItem = new MultiFileItem<>();
            ComponentSublist backgroundSublist = new ComponentSublist();
            backgroundSublist.getContent().add(backgroundItem);
            backgroundSublist.setTitle(i18n("launcher.background"));
            backgroundSublist.setHasSubtitle(true);

            backgroundItem.loadChildren(Arrays.asList(
                    new MultiFileItem.Option<>(i18n("launcher.background.default"), EnumBackgroundImage.DEFAULT)
                            .setTooltip(i18n("launcher.background.default.tooltip")),
                    new MultiFileItem.Option<>(i18n("launcher.background.classic"), EnumBackgroundImage.CLASSIC),
                    new MultiFileItem.FileOption<>(i18n("settings.custom"), EnumBackgroundImage.CUSTOM)
                            .setChooserTitle(i18n("launcher.background.choose"))
                            .addExtensionFilter(FXUtils.getImageExtensionFilter())
                            .setSelectionMode(FileSelector.SelectionMode.FILE_OR_DIRECTORY)
                            .bindBidirectional(config().backgroundImageProperty()),
                    new MultiFileItem.StringOption<>(i18n("launcher.background.network"), EnumBackgroundImage.NETWORK)
                            .setValidators(new URLValidator(true))
                            .bindBidirectional(config().backgroundImageUrlProperty()),
                    new MultiFileItem.PaintOption<>(i18n("launcher.background.paint"), EnumBackgroundImage.PAINT)
                            .bindBidirectional(config().backgroundPaintProperty())
            ));
            backgroundItem.selectedDataProperty().bindBidirectional(config().backgroundImageTypeProperty());
            backgroundSublist.subtitleProperty().bind(
                    new When(backgroundItem.selectedDataProperty().isEqualTo(EnumBackgroundImage.DEFAULT))
                            .then(i18n("launcher.background.default"))
                            .otherwise(config().backgroundImageProperty()));

            HBox opacityItem = new HBox(8);
            {
                opacityItem.setAlignment(Pos.CENTER);

                Label label = new Label(i18n("settings.launcher.background.settings.opacity"));

                JFXSlider slider = new JFXSlider(0, 100,
                        config().getBackgroundImageType() != EnumBackgroundImage.TRANSLUCENT
                                ? config().getBackgroundImageOpacity() : 50);
                slider.setShowTickMarks(true);
                slider.setMajorTickUnit(10);
                slider.setMinorTickCount(1);
                slider.setBlockIncrement(5);
                slider.setSnapToTicks(true);
                slider.setPadding(new Insets(9, 0, 0, 0));
                HBox.setHgrow(slider, Priority.ALWAYS);

                if (config().getBackgroundImageType() == EnumBackgroundImage.TRANSLUCENT) {
                    slider.setDisable(true);
                    config().backgroundImageTypeProperty().addListener(new ChangeListener<>() {
                        @Override
                        public void changed(ObservableValue<? extends EnumBackgroundImage> observable, EnumBackgroundImage oldValue, EnumBackgroundImage newValue) {
                            if (newValue != EnumBackgroundImage.TRANSLUCENT) {
                                config().backgroundImageTypeProperty().removeListener(this);
                                slider.setDisable(false);
                                slider.setValue(100);
                            }
                        }
                    });
                }

                Label textOpacity = new Label();
                FXUtils.setLimitWidth(textOpacity, 50);

                StringBinding valueBinding = Bindings.createStringBinding(() -> ((int) slider.getValue()) + "%", slider.valueProperty());
                textOpacity.textProperty().bind(valueBinding);
                slider.setValueFactory(s -> valueBinding);

                slider.valueProperty().addListener((observable, oldValue, newValue) ->
                        config().setBackgroundImageOpacity(snapOpacity(newValue.doubleValue())));

                opacityItem.getChildren().setAll(label, slider, textOpacity);
            }

            {
                LineToggleButton uiTransparencyButton = new LineToggleButton();
                uiTransparencyButton.selectedProperty().bindBidirectional(config().uiTransparencyEnabledProperty());
                uiTransparencyButton.setTitle(i18n("settings.launcher.ui_transparency"));
                uiTransparencyButton.setSubtitle(i18n("settings.launcher.ui_transparency.subtitle"));

                HBox ratioItem = new HBox(8);
                ratioItem.setAlignment(Pos.CENTER);
                ratioItem.visibleProperty().bind(config().uiTransparencyEnabledProperty());
                ratioItem.managedProperty().bind(ratioItem.visibleProperty());

                Label ratioLabel = new Label(i18n("settings.launcher.ui_transparency.ratio"));

                JFXSlider ratioSlider = new JFXSlider(0, 100, config().getUiTransparencyRatio() * 100);
                ratioSlider.setShowTickMarks(true);
                ratioSlider.setMajorTickUnit(10);
                ratioSlider.setMinorTickCount(1);
                ratioSlider.setBlockIncrement(5);
                ratioSlider.setSnapToTicks(true);
                ratioSlider.setPadding(new Insets(9, 0, 0, 0));
                HBox.setHgrow(ratioSlider, Priority.ALWAYS);

                Label ratioText = new Label();
                FXUtils.setLimitWidth(ratioText, 50);
                StringBinding ratioBinding = Bindings.createStringBinding(
                        () -> ((int) ratioSlider.getValue()) + "%", ratioSlider.valueProperty());
                ratioText.textProperty().bind(ratioBinding);
                ratioSlider.setValueFactory(s -> ratioBinding);

                ratioSlider.valueProperty().addListener((observable, oldValue, newValue) ->
                        config().setUiTransparencyRatio(newValue.doubleValue() / 100.0));

                ratioItem.getChildren().setAll(ratioLabel, ratioSlider, ratioText);

                componentList.getContent().setAll(backgroundItem, opacityItem, uiTransparencyButton, ratioItem);
            }

            content.getChildren().addAll(ComponentList.createComponentListTitle(i18n("launcher.background")), componentList);
        }

        {
            ComponentList fontPane = new ComponentList();

            {
                VBox vbox = new VBox();
                vbox.setSpacing(5);

                {
                    BorderPane borderPane = new BorderPane();
                    vbox.getChildren().add(borderPane);
                    {
                        Label left = new Label(i18n("settings.launcher.font"));
                        BorderPane.setAlignment(left, Pos.CENTER_LEFT);
                        borderPane.setLeft(left);
                    }

                    {
                        HBox hBox = new HBox();
                        hBox.setSpacing(8);

                        FontComboBox cboFont = new FontComboBox();
                        cboFont.setValue(config().getLauncherFontFamily());
                        FXUtils.onChange(cboFont.valueProperty(), FontManager::setFontFamily);

                        Button clearButton = FXUtils.newToggleButton4(SVG.RESTORE);
                        clearButton.setOnAction(e -> cboFont.setValue(null));

                        FXUtils.installFastTooltip(clearButton, i18n("button.reset"));

                        hBox.getChildren().setAll(cboFont, clearButton);

                        borderPane.setRight(hBox);
                    }
                }

                vbox.getChildren().add(new Label("Hello Minecraft! Launcher"));

                fontPane.getContent().add(vbox);
            }

            {
                var fontAntiAliasingPane = new LineSelectButton<Optional<FontSmoothingType>>();
                fontAntiAliasingPane.setTitle(i18n("settings.launcher.font.anti_aliasing"));
                fontAntiAliasingPane.setSubtitle(i18n("settings.take_effect_after_restart"));
                fontAntiAliasingPane.setNullSafeConverter(value ->
                        value.isPresent()
                                ? i18n("settings.launcher.font.anti_aliasing." + value.get().name().toLowerCase(Locale.ROOT))
                                : i18n("settings.launcher.font.anti_aliasing.auto")
                );
                fontAntiAliasingPane.setItems(
                        Optional.empty(),
                        Optional.of(FontSmoothingType.LCD),
                        Optional.of(FontSmoothingType.GRAY)
                );

                String fontAntiAliasing = globalConfig().getFontAntiAliasing();
                if ("lcd".equalsIgnoreCase(fontAntiAliasing)) {
                    fontAntiAliasingPane.setValue(Optional.of(FontSmoothingType.LCD));
                } else if ("gray".equalsIgnoreCase(fontAntiAliasing)) {
                    fontAntiAliasingPane.setValue(Optional.of(FontSmoothingType.GRAY));
                } else {
                    fontAntiAliasingPane.setValue(Optional.empty());
                }

                FXUtils.onChange(fontAntiAliasingPane.valueProperty(), value ->
                        globalConfig().setFontAntiAliasing(value.map(it -> it.name().toLowerCase(Locale.ROOT))
                                .orElse(null)));

                fontPane.getContent().add(fontAntiAliasingPane);
            }

            content.getChildren().addAll(ComponentList.createComponentListTitle(i18n("settings.launcher.font")), fontPane);
        }
    }
}
