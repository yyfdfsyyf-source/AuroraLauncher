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

import atlantafx.base.controls.CustomTextField;
import atlantafx.base.controls.PasswordTextField;
import atlantafx.base.controls.ToggleSwitch;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.ContentDisplay;
import javafx.scene.control.Hyperlink;
import javafx.scene.control.Label;
import javafx.scene.control.ListCell;
import javafx.scene.control.ListView;
import javafx.scene.control.ProgressBar;
import javafx.scene.control.ProgressIndicator;
import javafx.scene.control.RadioButton;
import javafx.scene.control.Slider;
import javafx.scene.control.Spinner;
import javafx.scene.control.TabPane;
import javafx.scene.control.TextArea;
import javafx.scene.control.TextField;
import javafx.scene.control.ToggleButton;
import javafx.util.Callback;
import org.jackhuang.hmcl.ui.SVG;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/// AtlantaFX控件工厂实现
/// 使用AtlantaFX控件和标准JavaFX控件的组合
public final class AtlantaFXComponentFactory implements UIComponentFactory {

    public static final AtlantaFXComponentFactory INSTANCE = new AtlantaFXComponentFactory();

    private AtlantaFXComponentFactory() {
    }

    @Override
    public @NotNull Button createButton() {
        return new Button();
    }

    @Override
    public @NotNull Button createButton(@Nullable String text) {
        return new Button(text);
    }

    @Override
    public @NotNull Button createRaisedButton(@Nullable String text) {
        Button button = new Button(text);
        button.getStyleClass().addAll("button", "button-primary");
        return button;
    }

    @Override
    public @NotNull Button createBorderButton(@Nullable String text) {
        Button button = new Button(text);
        button.getStyleClass().addAll("button", "button-outlined");
        return button;
    }

    @Override
    public @NotNull Button createToggleButton(@NotNull Object icon, int size) {
        Button button = new Button();
        button.getStyleClass().add("toggle-icon4");
        if (icon instanceof SVG svg) {
            button.setGraphic(svg.createIcon(size));
        }
        return button;
    }

    @Override
    public @NotNull TextField createTextField() {
        return new CustomTextField();
    }

    @Override
    public @NotNull TextField createTextField(@Nullable String text) {
        CustomTextField field = new CustomTextField();
        field.setText(text);
        return field;
    }

    @Override
    public @NotNull TextField createPasswordField() {
        return new PasswordTextField();
    }

    @Override
    public @NotNull TextArea createTextArea() {
        return new TextArea();
    }

    @Override
    public @NotNull TextArea createTextArea(@Nullable String text) {
        TextArea area = new TextArea();
        area.setText(text);
        return area;
    }

    @Override
    public @NotNull CheckBox createCheckBox() {
        return new CheckBox();
    }

    @Override
    public @NotNull CheckBox createCheckBox(@Nullable String text) {
        return new CheckBox(text);
    }

    @Override
    public @NotNull RadioButton createRadioButton() {
        return new RadioButton();
    }

    @Override
    public @NotNull RadioButton createRadioButton(@Nullable String text) {
        return new RadioButton(text);
    }

    @Override
    public @NotNull Slider createSlider(double min, double max, double value) {
        return new Slider(min, max, value);
    }

    @Override
    public @NotNull ProgressBar createProgressBar() {
        return new ProgressBar();
    }

    @Override
    public @NotNull ProgressIndicator createProgressIndicator() {
        return new ProgressIndicator();
    }

    @Override
    public @NotNull <T> ComboBox<T> createComboBox() {
        return new ComboBox<>();
    }

    @Override
    public @NotNull <T> ListView<T> createListView() {
        return new ListView<>();
    }

    @Override
    public @NotNull TabPane createTabPane() {
        return new TabPane();
    }

    @Override
    public @NotNull Hyperlink createHyperlink(@Nullable String text) {
        return new Hyperlink(text);
    }

    @Override
    public @NotNull <T> Spinner<T> createSpinner() {
        return new Spinner<>();
    }

    @Override
    public @NotNull <T> Callback<ListView<T>, ListCell<T>> createListCellFactory(@NotNull Callback<T, javafx.scene.Node> graphicBuilder) {
        return view -> new ListCell<T>() {
            @Override
            public void updateItem(T item, boolean empty) {
                super.updateItem(item, empty);
                if (!empty) {
                    setContentDisplay(ContentDisplay.GRAPHIC_ONLY);
                    setGraphic(graphicBuilder.call(item));
                }
            }
        };
    }

    @Override
    public @NotNull Label createLabel() {
        return new Label();
    }

    @Override
    public @NotNull Label createLabel(@Nullable String text) {
        return new Label(text);
    }
}
