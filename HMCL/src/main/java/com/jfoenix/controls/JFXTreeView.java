/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package com.jfoenix.controls;

import javafx.scene.control.TreeCell;
import javafx.scene.control.TreeItem;
import javafx.scene.control.TreeView;
import org.jackhuang.hmcl.ui.UILibraryManager;

/// JFXTreeView is the material design implementation of a TreeView
/// with expand/collapse animation and selection indicator.
///
/// @author Shadi Shaheen
/// @version 1.0
/// @since 2017-02-15
public class JFXTreeView<T> extends TreeView<T> {

    private static final String DEFAULT_STYLE_CLASS = "jfx-tree-view";

    public JFXTreeView() {
        init();
    }

    public JFXTreeView(TreeItem<T> root) {
        super(root);
        init();
    }

    private void init() {
        if (UILibraryManager.isJFoenix()) {
            this.setCellFactory((view) -> new JFXTreeCell<>());
        } else {
            this.setCellFactory((view) -> new TreeCell<>() {
                @Override
                protected void updateItem(T item, boolean empty) {
                    super.updateItem(item, empty);
                    if (empty || item == null) {
                        setText(null);
                        setGraphic(null);
                    } else {
                        TreeItem<T> treeItem = getTreeItem();
                        if (treeItem != null && treeItem.getGraphic() != null) {
                            setText(item.toString());
                            setGraphic(treeItem.getGraphic());
                        } else {
                            setText(item.toString());
                            setGraphic(null);
                        }
                    }
                }
            });
        }
        this.getStyleClass().add(DEFAULT_STYLE_CLASS);
    }
}
