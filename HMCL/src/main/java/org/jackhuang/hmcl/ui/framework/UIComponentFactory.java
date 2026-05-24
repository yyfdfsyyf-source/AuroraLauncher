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

import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
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
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/// UI组件工厂接口，抽象JFoenix和AtlantaFX的控件创建
/// 实现类根据当前UI库配置返回对应风格的控件
public interface UIComponentFactory {

    /// 创建按钮
    @NotNull Button createButton();

    /// 创建带文本的按钮
    @NotNull Button createButton(@Nullable String text);

    /// 创建凸起样式按钮
    @NotNull Button createRaisedButton(@Nullable String text);

    /// 创建边框样式按钮
    @NotNull Button createBorderButton(@Nullable String text);

    /// 创建图标切换按钮（小尺寸）
    @NotNull Button createToggleButton(@NotNull Object icon, int size);

    /// 创建文本输入框
    @NotNull TextField createTextField();

    /// 创建带初始值的文本输入框
    @NotNull TextField createTextField(@Nullable String text);

    /// 创建密码输入框
    @NotNull TextField createPasswordField();

    /// 创建多行文本输入框
    @NotNull TextArea createTextArea();

    /// 创建多行文本输入框（带初始文本）
    @NotNull TextArea createTextArea(@Nullable String text);

    /// 创建复选框
    @NotNull CheckBox createCheckBox();

    /// 创建带文本的复选框
    @NotNull CheckBox createCheckBox(@Nullable String text);

    /// 创建单选按钮
    @NotNull RadioButton createRadioButton();

    /// 创建带文本的单选按钮
    @NotNull RadioButton createRadioButton(@Nullable String text);

    /// 创建滑块
    @NotNull Slider createSlider(double min, double max, double value);

    /// 创建进度条
    @NotNull ProgressBar createProgressBar();

    /// 创建加载指示器
    @NotNull ProgressIndicator createProgressIndicator();

    /// 创建下拉选择框
    @NotNull <T> ComboBox<T> createComboBox();

    /// 创建列表视图
    @NotNull <T> ListView<T> createListView();

    /// 创建标签页容器
    @NotNull TabPane createTabPane();

    /// 创建超链接
    @NotNull Hyperlink createHyperlink(@Nullable String text);

    /// 创建数字微调器
    @NotNull <T> Spinner<T> createSpinner();

    /// 创建列表单元格工厂
    @NotNull <T> Callback<ListView<T>, ListCell<T>> createListCellFactory(@NotNull Callback<T, javafx.scene.Node> graphicBuilder);

    /// 创建标签
    @NotNull Label createLabel();

    /// 创建带文本的标签
    @NotNull Label createLabel(@Nullable String text);
}
