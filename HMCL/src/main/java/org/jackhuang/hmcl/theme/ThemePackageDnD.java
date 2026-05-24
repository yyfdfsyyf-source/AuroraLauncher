/*
 * Hello Minecraft! Launcher
 * Copyright (C) 2020  huangyuhui <huanghongxun2008@126.com> and contributors
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

import javafx.event.EventHandler;
import javafx.scene.input.DragEvent;
import javafx.scene.input.Dragboard;
import javafx.scene.input.TransferMode;

import java.io.File;
import java.util.Optional;
import java.util.function.Consumer;

public final class ThemePackageDnD {

    private static final String EXTENSION = ".hmclt";

    private ThemePackageDnD() {}

    public static Optional<File> parseFileFromDragboard(Dragboard dragboard) {
        if (dragboard.hasFiles()) {
            for (File file : dragboard.getFiles()) {
                if (file.getName().toLowerCase().endsWith(EXTENSION)) {
                    return Optional.of(file);
                }
            }
        }
        return Optional.empty();
    }

    public static EventHandler<DragEvent> dragOverHandler() {
        return event -> parseFileFromDragboard(event.getDragboard()).ifPresent(file -> {
            event.acceptTransferModes(TransferMode.COPY);
            event.consume();
        });
    }

    public static EventHandler<DragEvent> dragDroppedHandler(Consumer<File> onFileTransferred) {
        return event -> parseFileFromDragboard(event.getDragboard()).ifPresent(file -> {
            event.setDropCompleted(true);
            event.consume();
            onFileTransferred.accept(file);
        });
    }

}
