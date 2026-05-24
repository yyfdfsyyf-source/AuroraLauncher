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
package org.jackhuang.hmcl.ui.construct;

import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;
import javafx.collections.ObservableList;
import javafx.geometry.Insets;
import javafx.geometry.Orientation;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.ProgressBar;
import javafx.scene.layout.FlowPane;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;

import java.util.List;

import static org.jackhuang.hmcl.ui.FXUtils.onEscPressed;
import static org.jackhuang.hmcl.util.i18n.I18n.i18n;

public class DialogPane extends StackPane {
    private final StackPane heading = new StackPane();
    private final StackPane body = new StackPane();
    private final FlowPane actions = new FlowPane() {
        protected double computeMinWidth(double height) {
            if (this.getContentBias() == Orientation.HORIZONTAL) {
                double maxPref = 0.0;
                for (Node child : this.getChildren()) {
                    if (child.isManaged()) {
                        maxPref = Math.max(maxPref, child.minWidth(-1.0));
                    }
                }

                Insets insets = this.getInsets();
                return insets.getLeft() + this.snapSizeX(maxPref) + insets.getRight();
            } else {
                return this.computePrefWidth(height);
            }
        }

        protected double computeMinHeight(double width) {
            if (this.getContentBias() == Orientation.VERTICAL) {
                double maxPref = 0.0;

                for (Node child : this.getChildren()) {
                    if (child.isManaged()) {
                        maxPref = Math.max(maxPref, child.minHeight(-1.0));
                    }
                }

                Insets insets = this.getInsets();
                return insets.getTop() + this.snapSizeY(maxPref) + insets.getBottom();
            } else {
                return this.computePrefHeight(width);
            }
        }
    };

    private final StringProperty title = new SimpleStringProperty();
    private final BooleanProperty valid = new SimpleBooleanProperty(true);
    protected final SpinnerPane acceptPane = new SpinnerPane();
    protected final Button cancelButton = new Button();
    protected final Label warningLabel = new Label();
    private final ProgressBar progressBar = new ProgressBar();

    public DialogPane() {
        this.getStyleClass().add("jfx-dialog-layout");

        VBox layout = new VBox();

        this.heading.getStyleClass().add("jfx-layout-heading");
        this.heading.getStyleClass().add("title");

        this.body.getStyleClass().add("jfx-layout-body");
        this.body.prefHeightProperty().bind(this.prefHeightProperty());
        this.body.prefWidthProperty().bind(this.prefWidthProperty());

        this.actions.getStyleClass().add("jfx-layout-actions");

        layout.getChildren().setAll(this.heading, this.body, this.actions);

        this.getChildren().add(layout);

        Label titleLabel = new Label();
        titleLabel.textProperty().bind(title);
        setHeading(titleLabel);
        getChildren().add(progressBar);

        progressBar.setVisible(false);
        StackPane.setMargin(progressBar, new Insets(-24.0D, -24.0D, -16.0D, -24.0D));
        StackPane.setAlignment(progressBar, Pos.TOP_CENTER);
        progressBar.setMaxWidth(Double.MAX_VALUE);

        Button acceptButton = new Button(i18n("button.ok"));
        acceptButton.getStyleClass().add("jfx-button");
        acceptButton.setOnAction(e -> onAccept());
        acceptButton.disableProperty().bind(valid.not());
        acceptButton.getStyleClass().add("dialog-accept");
        acceptPane.getStyleClass().add("small-spinner-pane");
        acceptPane.setContent(acceptButton);

        cancelButton.setText(i18n("button.cancel"));
        cancelButton.getStyleClass().add("jfx-button");
        cancelButton.setOnAction(e -> onCancel());
        cancelButton.getStyleClass().add("dialog-cancel");
        onEscPressed(this, cancelButton::fire);

        setActions(warningLabel, acceptPane, cancelButton);
    }

    public ObservableList<Node> getHeading() {
        return this.heading.getChildren();
    }

    public void setHeading(Node... titleContent) {
        this.heading.getChildren().setAll(titleContent);
    }

    public ObservableList<Node> getBody() {
        return this.body.getChildren();
    }

    public void setBody(Node... body) {
        this.body.getChildren().setAll(body);
    }

    public ObservableList<Node> getActions() {
        return this.actions.getChildren();
    }

    public void setActions(Node... actions) {
        this.actions.getChildren().setAll(actions);
    }

    public void setActions(List<? extends Node> actions) {
        this.actions.getChildren().setAll(actions);
    }

    protected ProgressBar getProgressBar() {
        return progressBar;
    }

    public String getTitle() {
        return title.get();
    }

    public StringProperty titleProperty() {
        return title;
    }

    public void setTitle(String title) {
        this.title.set(title);
    }

    public boolean isValid() {
        return valid.get();
    }

    public BooleanProperty validProperty() {
        return valid;
    }

    public void setValid(boolean valid) {
        this.valid.set(valid);
    }

    protected void onCancel() {
        fireEvent(new DialogCloseEvent());
    }

    protected void onAccept() {
        fireEvent(new DialogCloseEvent());
    }

    protected void setLoading() {
        acceptPane.showSpinner();
        warningLabel.setText("");
    }

    protected void onSuccess() {
        acceptPane.hideSpinner();
        fireEvent(new DialogCloseEvent());
    }

    protected void onFailure(String msg) {
        acceptPane.hideSpinner();
        warningLabel.setText(msg);
    }
}
