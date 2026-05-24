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
package org.jackhuang.hmcl.ui.construct;

import com.jfoenix.controls.JFXTextField;
import com.jfoenix.validation.base.ValidatorBase;
import javafx.scene.control.TextField;
import javafx.scene.control.SkinBase;
import javafx.collections.ObservableList;
import javafx.geometry.Insets;
import javafx.geometry.Orientation;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.TextField;
import javafx.scene.layout.FlowPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import org.jackhuang.hmcl.util.FutureCallback;

import java.util.List;
import java.util.concurrent.CompletableFuture;

import static org.jackhuang.hmcl.ui.FXUtils.onEscPressed;
import static org.jackhuang.hmcl.util.i18n.I18n.i18n;

public class InputDialogPane extends StackPane implements DialogAware {
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

    private final CompletableFuture<String> future = new CompletableFuture<>();

    private final TextField textField;
    private final Label lblCreationWarning;
    private final SpinnerPane acceptPane;
    private final Button acceptButton;

    public InputDialogPane(String text, String initialValue, FutureCallback<String> onResult) {
        this(text, initialValue, onResult, new ValidatorBase[0]);
    }

    public InputDialogPane(String text, String initialValue, FutureCallback<String> onResult, ValidatorBase... validators) {

        this.getStyleClass().add("dialog-layout");

        VBox layout = new VBox();

        this.heading.getStyleClass().add("dialog-layout-heading");
        this.heading.getStyleClass().add("title");

        this.body.getStyleClass().add("dialog-layout-body");
        this.body.prefHeightProperty().bind(this.prefHeightProperty());
        this.body.prefWidthProperty().bind(this.prefWidthProperty());

        this.actions.getStyleClass().add("dialog-layout-actions");

        layout.getChildren().setAll(this.heading, this.body, this.actions);

        this.getChildren().add(layout);

        if (validators.length > 0) {
            JFXTextField jfxField = new JFXTextField(initialValue);
            jfxField.getValidators().addAll(validators);
            textField = jfxField;
        } else {
            textField = new TextField(initialValue);
        }
        textField.getStyleClass().add("text-field");

        this.setHeading(new HBox(new Label(text)));
        this.setBody(new VBox(textField));

        lblCreationWarning = new Label();
        lblCreationWarning.setPadding(new Insets(0, 5, 0, 0));

        acceptPane = new SpinnerPane();
        acceptPane.getStyleClass().add("small-spinner-pane");
        acceptButton = new Button(i18n("button.ok"));
        acceptButton.getStyleClass().add("button");
        acceptButton.getStyleClass().add("dialog-accept");
        acceptPane.setContent(acceptButton);

        Button cancelButton = new Button(i18n("button.cancel"));
        cancelButton.getStyleClass().add("button");
        cancelButton.getStyleClass().add("dialog-cancel");

        this.setActions(lblCreationWarning, acceptPane, cancelButton);

        cancelButton.setOnAction(e -> fireEvent(new DialogCloseEvent()));
        acceptButton.setOnAction(e -> {
            acceptPane.showSpinner();

            onResult.call(textField.getText(), new FutureCallback.ResultHandler() {
                @Override
                public void resolve() {
                    acceptPane.hideSpinner();
                    future.complete(textField.getText());
                    fireEvent(new DialogCloseEvent());
                }

                @Override
                public void reject(String reason) {
                    acceptPane.hideSpinner();
                    lblCreationWarning.setText(reason);
                }
            });
        });
        textField.setOnAction(event -> acceptButton.fire());
        onEscPressed(this, cancelButton::fire);
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

    @Override
    public void onDialogShown() {
        textField.requestFocus();
    }

    public CompletableFuture<String> getCompletableFuture() {
        return future;
    }
}
