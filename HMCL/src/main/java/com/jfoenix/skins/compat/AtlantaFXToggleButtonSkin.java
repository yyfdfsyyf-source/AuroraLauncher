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

import javafx.animation.Interpolator;
import javafx.animation.ParallelTransition;
import javafx.animation.TranslateTransition;
import javafx.geometry.Insets;
import javafx.scene.Cursor;
import javafx.scene.control.ToggleButton;
import javafx.scene.control.skin.ToggleButtonSkin;
import javafx.scene.layout.StackPane;
import javafx.scene.paint.Color;
import javafx.scene.paint.Paint;
import javafx.scene.shape.Circle;
import javafx.scene.shape.Line;
import javafx.scene.shape.StrokeLineCap;
import javafx.util.Duration;
import org.jetbrains.annotations.NotNull;

/// AtlantaFX 兼容的 ToggleButton Skin
/// 精确复现 JFXToggleButtonSkin 的布局逻辑
public class AtlantaFXToggleButtonSkin extends ToggleButtonSkin {

    private static final double DEFAULT_SIZE = 10.0;
    private static final Duration ANIMATION_DURATION = Duration.millis(100);

    private final Circle circle;
    private final Line line;
    private final StackPane circlePane;
    private final ParallelTransition transition;

    public AtlantaFXToggleButtonSkin(@NotNull ToggleButton toggleButton) {
        super(toggleButton);

        final double circleRadius;
        if (toggleButton instanceof com.jfoenix.controls.JFXToggleButton jfx) {
            circleRadius = jfx.getSize();
        } else {
            circleRadius = DEFAULT_SIZE;
        }

        final Paint onColor;
        final Paint offColor;
        final Paint onLineColor;
        final Paint offLineColor;

        if (toggleButton instanceof com.jfoenix.controls.JFXToggleButton jfx) {
            onColor = jfx.getToggleColor();
            offColor = jfx.getUnToggleColor();
            onLineColor = jfx.getToggleLineColor();
            offLineColor = jfx.getUnToggleLineColor();
        } else {
            onColor = Color.valueOf("#009688");
            offColor = Color.valueOf("#FAFAFA");
            onLineColor = Color.valueOf("#77C2BB");
            offLineColor = Color.valueOf("#999999");
        }

        boolean selected = toggleButton.isSelected();

        line = new Line();
        line.setStroke(selected ? onLineColor : offLineColor);
        line.setStartX(0);
        line.setStartY(0);
        line.setEndX(circleRadius * 2 + 2);
        line.setEndY(0);
        line.setStrokeWidth(circleRadius * 1.5);
        line.setStrokeLineCap(StrokeLineCap.ROUND);
        line.setSmooth(true);

        circle = new Circle();
        circle.setFill(selected ? onColor : offColor);
        circle.setCenterX(-circleRadius);
        circle.setCenterY(0);
        circle.setRadius(circleRadius);
        circle.setSmooth(true);

        circlePane = new StackPane();
        circlePane.getChildren().add(circle);
        circlePane.setPadding(new Insets(circleRadius * 1.5));
        circlePane.setTranslateX(computeTranslation(circleRadius));

        StackPane main = new StackPane();
        main.getChildren().setAll(line, circlePane);
        main.setCursor(Cursor.HAND);

        toggleButton.setGraphic(main);
        toggleButton.setText(null);

        TranslateTransition circleTransition = new TranslateTransition(ANIMATION_DURATION, circlePane);
        circleTransition.setInterpolator(Interpolator.EASE_BOTH);

        transition = new ParallelTransition(circleTransition);

        toggleButton.selectedProperty().addListener((obs, oldVal, newVal) -> {
            circleTransition.setToX(computeTranslation(circleRadius));
            transition.playFromStart();

            circle.setFill(newVal ? onColor : offColor);
            line.setStroke(newVal ? onLineColor : offLineColor);
        });
    }

    private double computeTranslation(double circleRadius) {
        return (getSkinnable().isSelected() ? 1 : -1) * ((line.getLayoutBounds().getWidth() / 2) - circleRadius + 2);
    }

    @Override
    public void dispose() {
        if (transition != null) {
            transition.stop();
        }
        super.dispose();
    }
}
