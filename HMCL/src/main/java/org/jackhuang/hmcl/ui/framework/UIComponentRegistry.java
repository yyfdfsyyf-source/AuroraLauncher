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
package org.jackhuang.hmcl.ui.framework;

import javafx.beans.property.ObjectProperty;
import javafx.beans.property.SimpleObjectProperty;
import org.jackhuang.hmcl.setting.UILibrary;
import org.jetbrains.annotations.NotNull;

import static org.jackhuang.hmcl.setting.ConfigHolder.config;

/// UI组件注册表，管理当前活动的组件工厂
/// 支持动态切换UI库实现
public final class UIComponentRegistry {

    private static final ObjectProperty<UIComponentFactory> factory = new SimpleObjectProperty<>();

    static {
        factory.set(resolveFactory(config().getUiLibrary()));
        config().uiLibraryProperty().addListener((obs, old, val) -> {
            if (val != null) {
                factory.set(resolveFactory(val));
            }
        });
    }

    private UIComponentRegistry() {
    }

    private static @NotNull UIComponentFactory resolveFactory(@NotNull UILibrary library) {
        // 仅支持 AtlantaFX，JFoenix 仅保留特殊组件
        return AtlantaFXComponentFactory.INSTANCE;
    }

    /// 获取当前活动的组件工厂
    public static @NotNull UIComponentFactory getFactory() {
        return factory.get();
    }

    /// 当前工厂属性
    public static ObjectProperty<UIComponentFactory> factoryProperty() {
        return factory;
    }

    /// 快捷方法：创建按钮
    public static @NotNull javafx.scene.control.Button createButton() {
        return getFactory().createButton();
    }

    public static @NotNull javafx.scene.control.Button createButton(String text) {
        return getFactory().createButton(text);
    }

    public static @NotNull javafx.scene.control.Button createRaisedButton(String text) {
        return getFactory().createRaisedButton(text);
    }

    public static @NotNull javafx.scene.control.Button createBorderButton(String text) {
        return getFactory().createBorderButton(text);
    }

    public static @NotNull javafx.scene.control.Button createToggleButton(@NotNull Object icon, int size) {
        return getFactory().createToggleButton(icon, size);
    }

    public static @NotNull javafx.scene.control.TextField createTextField() {
        return getFactory().createTextField();
    }

    public static @NotNull javafx.scene.control.TextField createTextField(String text) {
        return getFactory().createTextField(text);
    }

    public static @NotNull javafx.scene.control.TextField createPasswordField() {
        return getFactory().createPasswordField();
    }

    public static @NotNull javafx.scene.control.TextArea createTextArea() {
        return getFactory().createTextArea();
    }

    public static @NotNull javafx.scene.control.TextArea createTextArea(String text) {
        return getFactory().createTextArea(text);
    }

    public static @NotNull javafx.scene.control.CheckBox createCheckBox() {
        return getFactory().createCheckBox();
    }

    public static @NotNull javafx.scene.control.CheckBox createCheckBox(String text) {
        return getFactory().createCheckBox(text);
    }

    public static @NotNull javafx.scene.control.RadioButton createRadioButton() {
        return getFactory().createRadioButton();
    }

    public static @NotNull javafx.scene.control.RadioButton createRadioButton(String text) {
        return getFactory().createRadioButton(text);
    }

    public static @NotNull javafx.scene.control.Slider createSlider(double min, double max, double value) {
        return getFactory().createSlider(min, max, value);
    }

    public static @NotNull javafx.scene.control.ProgressBar createProgressBar() {
        return getFactory().createProgressBar();
    }

    public static @NotNull <T> javafx.scene.control.ComboBox<T> createComboBox() {
        return getFactory().createComboBox();
    }

    public static @NotNull <T> javafx.scene.control.ListView<T> createListView() {
        return getFactory().createListView();
    }

    public static @NotNull javafx.scene.control.TabPane createTabPane() {
        return getFactory().createTabPane();
    }

    public static @NotNull javafx.scene.control.Hyperlink createHyperlink(String text) {
        return getFactory().createHyperlink(text);
    }

    public static @NotNull <T> javafx.scene.control.Spinner<T> createSpinner() {
        return getFactory().createSpinner();
    }

    public static @NotNull <T> javafx.util.Callback<javafx.scene.control.ListView<T>, javafx.scene.control.ListCell<T>> createListCellFactory(
            @NotNull javafx.util.Callback<T, javafx.scene.Node> graphicBuilder) {
        return getFactory().createListCellFactory(graphicBuilder);
    }

    public static @NotNull javafx.scene.control.Label createLabel() {
        return getFactory().createLabel();
    }

    public static @NotNull javafx.scene.control.Label createLabel(String text) {
        return getFactory().createLabel(text);
    }
}
