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
package com.jfoenix.skins.compat;

import javafx.scene.Node;
import javafx.scene.control.Skin;
import javafx.scene.layout.StackPane;
import org.jetbrains.annotations.NotNull;

/// AtlantaFX 兼容的 Popup Skin
/// 为 JFXPopup 在 AtlantaFX 模式下提供基础 Skin 实现
public class AtlantaFXPopupSkin implements Skin<com.jfoenix.controls.JFXPopup> {

    private final com.jfoenix.controls.JFXPopup control;
    private final StackPane root;

    public AtlantaFXPopupSkin(@NotNull com.jfoenix.controls.JFXPopup control) {
        this.control = control;
        this.root = new StackPane();
        this.root.getStyleClass().add("popup-content");
        this.root.getChildren().add(control.getPopupContent());
    }

    @Override
    public com.jfoenix.controls.JFXPopup getSkinnable() {
        return control;
    }

    @Override
    public Node getNode() {
        return root;
    }

    @Override
    public void dispose() {
        root.getChildren().clear();
    }
}
