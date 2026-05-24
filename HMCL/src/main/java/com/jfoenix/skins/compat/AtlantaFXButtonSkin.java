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
package com.jfoenix.skins.compat;

import javafx.scene.control.Button;
import javafx.scene.control.skin.ButtonSkin;
import org.jetbrains.annotations.NotNull;

/// AtlantaFX 兼容的 Button Skin
/// 使用标准 JavaFX ButtonSkin
public class AtlantaFXButtonSkin extends ButtonSkin {

    public AtlantaFXButtonSkin(@NotNull Button button) {
        super(button);
    }
}
