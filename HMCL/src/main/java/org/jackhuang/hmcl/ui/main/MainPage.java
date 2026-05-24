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
package org.jackhuang.hmcl.ui.main;

import com.jfoenix.controls.JFXPopup;
import javafx.animation.KeyFrame;
import javafx.animation.KeyValue;
import javafx.animation.Timeline;
import javafx.beans.property.*;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.event.EventHandler;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.Cursor;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.Tooltip;
import javafx.scene.image.ImageView;
import javafx.scene.input.MouseButton;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.scene.text.TextFlow;
import javafx.util.Duration;
import org.jackhuang.hmcl.Metadata;
import org.jackhuang.hmcl.download.DefaultDependencyManager;
import org.jackhuang.hmcl.download.DownloadProvider;
import org.jackhuang.hmcl.download.VersionList;
import org.jackhuang.hmcl.game.Version;
import org.jackhuang.hmcl.setting.DownloadProviders;
import org.jackhuang.hmcl.setting.Profile;
import org.jackhuang.hmcl.setting.Profiles;
import org.jackhuang.hmcl.task.Schedulers;
import org.jackhuang.hmcl.task.Task;
import org.jackhuang.hmcl.theme.Themes;
import org.jackhuang.hmcl.ui.Controllers;
import org.jackhuang.hmcl.ui.FXUtils;
import org.jackhuang.hmcl.ui.SVG;
import org.jackhuang.hmcl.ui.animation.AnimationUtils;
import org.jackhuang.hmcl.ui.animation.ContainerAnimations;
import org.jackhuang.hmcl.ui.animation.TransitionPane;
import org.jackhuang.hmcl.ui.construct.MessageDialogPane;
import org.jackhuang.hmcl.ui.construct.TwoLineListItem;
import org.jackhuang.hmcl.ui.decorator.DecoratorPage;
import org.jackhuang.hmcl.ui.versions.GameListPopupMenu;
import org.jackhuang.hmcl.ui.versions.Versions;
import org.jackhuang.hmcl.upgrade.RemoteVersion;
import org.jackhuang.hmcl.upgrade.UpdateChecker;
import org.jackhuang.hmcl.upgrade.UpdateHandler;
import org.jackhuang.hmcl.util.*;
import org.jackhuang.hmcl.util.i18n.I18n;
import org.jackhuang.hmcl.util.javafx.BindingMapping;
import org.jackhuang.hmcl.util.platform.OperatingSystem;
import org.jackhuang.hmcl.util.platform.Platform;
import org.jackhuang.hmcl.util.versioning.GameVersionNumber;

import java.io.IOException;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CancellationException;
import java.util.function.Consumer;

import static org.jackhuang.hmcl.download.RemoteVersion.Type.RELEASE;
import static org.jackhuang.hmcl.setting.ConfigHolder.config;
import static org.jackhuang.hmcl.ui.FXUtils.SINE;
import static org.jackhuang.hmcl.util.i18n.I18n.i18n;
import static org.jackhuang.hmcl.util.logging.Logger.LOG;

public final class MainPage extends StackPane implements DecoratorPage {
    private static final String ANNOUNCEMENT = "announcement";

    private final ReadOnlyObjectWrapper<State> state = new ReadOnlyObjectWrapper<>();

    private final StringProperty currentGame = new SimpleStringProperty(this, "currentGame");
    private final BooleanProperty showUpdate = new SimpleBooleanProperty(this, "showUpdate");
    private final ObjectProperty<RemoteVersion> latestVersion = new SimpleObjectProperty<>(this, "latestVersion");
    private final ObservableList<Version> versions = FXCollections.observableArrayList();
    private Profile profile;

    private TransitionPane announcementPane;
    private final StackPane updatePane;
    private final Button menuButton;

    {
        HBox titleNode = new HBox(8);
        titleNode.setPadding(new Insets(0, 0, 0, 2));
        titleNode.setAlignment(Pos.CENTER_LEFT);

        ImageView titleIcon = new ImageView(FXUtils.newBuiltinImage("/assets/img/icon-title.png"));
        Label titleLabel = new Label(Metadata.FULL_TITLE);
        if (I18n.isUpsideDown()) {
            titleIcon.setRotate(180);
            titleLabel.setRotate(180);
        }
        titleLabel.getStyleClass().add("jfx-decorator-title");
        titleLabel.textFillProperty().bind(Themes.titleFillProperty());
        titleNode.getChildren().setAll(titleIcon, titleLabel);

        state.setValue(new State(null, titleNode, false, false, true));

        setPadding(new Insets(0));

        // Main content area with background
        StackPane contentArea = new StackPane();
        contentArea.setPadding(new Insets(32));

        // Center info panel
        VBox centerPanel = new VBox();
        centerPanel.setAlignment(Pos.CENTER);
        centerPanel.setSpacing(20);
        centerPanel.setMaxWidth(400);
        StackPane.setAlignment(centerPanel, Pos.CENTER);

        // Game icon / logo
        ImageView gameIcon = new ImageView(FXUtils.newBuiltinImage("/assets/img/icon-title.png"));
        gameIcon.setFitWidth(80);
        gameIcon.setFitHeight(80);

        // Current game label
        Label gameLabel = new Label();
        gameLabel.getStyleClass().add("game-title");
        gameLabel.setStyle("-fx-font-size: 24px; -fx-font-weight: bold;");
        gameLabel.textProperty().bind(BindingMapping.of(currentGameProperty()).map(game ->
                game == null ? i18n("version.launch.empty") : game));

        // Version info
        Label versionInfo = new Label();
        versionInfo.getStyleClass().add("subtitle-label");
        versionInfo.textProperty().bind(BindingMapping.of(currentGameProperty()).map(game ->
                game == null ? i18n("version.launch.empty.hint") : i18n("version.launch.hint")));

        centerPanel.getChildren().setAll(gameIcon, gameLabel, versionInfo);

        // Launch button area
        HBox launchPane = new HBox();
        launchPane.getStyleClass().add("launch-pane");
        FXUtils.onScroll(launchPane, versions, list -> {
            String currentId = getCurrentGame();
            return Lang.indexWhere(list, instance -> instance.getId().equals(currentId));
        }, it -> profile.setSelectedVersion(it.getId()));

        StackPane.setAlignment(launchPane, Pos.BOTTOM_RIGHT);
        StackPane.setMargin(launchPane, new Insets(0, 32, 32, 0));
        {
            Button launchButton = new Button();
            launchButton.getStyleClass().addAll("launch-button", "button-primary");
            launchButton.setDefaultButton(true);
            {
                HBox graphic = new HBox();
                graphic.setAlignment(Pos.CENTER);
                graphic.setSpacing(8);
                Label launchLabel = new Label(i18n("version.launch"));
                launchLabel.setStyle("-fx-font-size: 15px; -fx-font-weight: 600;");

                graphic.getChildren().setAll(SVG.ROCKET_LAUNCH.createIcon(18), launchLabel);

                FXUtils.onChangeAndOperate(currentGameProperty(), new Consumer<>() {
                    private Tooltip tooltip;

                    @Override
                    public void accept(String currentGame) {
                        if (currentGame == null) {
                            FXUtils.setOnActionWithCooldown(launchButton, MainPage.this::launchNoGame);
                            if (tooltip == null)
                                tooltip = new Tooltip(i18n("version.launch.empty.tooltip"));
                            FXUtils.installFastTooltip(launchButton, tooltip);
                        } else {
                            FXUtils.setOnActionWithCooldown(launchButton, MainPage.this::launch);
                            if (tooltip != null)
                                Tooltip.uninstall(launchButton, tooltip);
                        }
                    }
                });

                launchButton.setGraphic(graphic);
            }

            menuButton = new Button();
            menuButton.getStyleClass().add("menu-button");
            menuButton.setOnAction(e -> GameListPopupMenu.show(
                    menuButton,
                    JFXPopup.PopupVPosition.BOTTOM,
                    JFXPopup.PopupHPosition.RIGHT,
                    0,
                    -menuButton.getHeight(),
                    profile, versions
            ));
            FXUtils.installFastTooltip(menuButton, i18n("version.switch"));
            menuButton.setGraphic(SVG.ARROW_DROP_UP.createIcon(24));

            EventHandler<MouseEvent> secondaryClickHandle = event -> {
                if (event.getButton() == MouseButton.SECONDARY && event.getClickCount() == 1) {
                    menuButton.fire();
                    event.consume();
                }
            };
            launchButton.addEventHandler(MouseEvent.MOUSE_CLICKED, secondaryClickHandle);
            menuButton.addEventHandler(MouseEvent.MOUSE_CLICKED, secondaryClickHandle);

            launchPane.getChildren().setAll(launchButton, menuButton);
        }

        contentArea.getChildren().setAll(centerPanel, launchPane);

        // Update notification (top right, smaller)
        updatePane = new StackPane();
        updatePane.setVisible(false);
        updatePane.getStyleClass().add("bubble");
        FXUtils.setLimitWidth(updatePane, 200);
        FXUtils.setLimitHeight(updatePane, 44);
        StackPane.setAlignment(updatePane, Pos.TOP_RIGHT);
        StackPane.setMargin(updatePane, new Insets(16));
        FXUtils.onClicked(updatePane, this::onUpgrade);
        updatePane.setCursor(Cursor.HAND);
        FXUtils.onChange(showUpdateProperty(), this::showUpdate);

        {
            HBox hBox = new HBox();
            hBox.setSpacing(8);
            hBox.setAlignment(Pos.CENTER_LEFT);
            StackPane.setAlignment(hBox, Pos.CENTER_LEFT);
            StackPane.setMargin(hBox, new Insets(0, 8, 0, 12));
            {
                Label prompt = new Label();
                prompt.getStyleClass().add("subtitle-label");
                prompt.textProperty().bind(BindingMapping.of(latestVersionProperty()).map(latestVersion ->
                        latestVersion == null ? "" : i18n("update.bubble.title", latestVersion.version())));

                hBox.getChildren().setAll(SVG.UPDATE.createIcon(16), prompt);
            }

            Button closeUpdateButton = new Button();
            closeUpdateButton.setGraphic(SVG.CLOSE.createIcon(10));
            StackPane.setAlignment(closeUpdateButton, Pos.TOP_RIGHT);
            closeUpdateButton.getStyleClass().add("toggle-icon-tiny");
            StackPane.setMargin(closeUpdateButton, new Insets(4));
            closeUpdateButton.setOnAction(e -> closeUpdateBubble());

            updatePane.getChildren().setAll(hBox, closeUpdateButton);
        }

        getChildren().setAll(contentArea, updatePane);

    }

    private void showUpdate(boolean show) {
        doAnimation(show);

        if (show && !config().isDisableAutoShowUpdateDialog()
                && getLatestVersion() != null
                && !Objects.equals(config().getPromptedVersion(), getLatestVersion().version())) {
            Controllers.dialog(new MessageDialogPane.Builder("", i18n("update.bubble.title", getLatestVersion().version()), MessageDialogPane.MessageType.INFO)
                    .addAction(i18n("button.view"), () -> {
                        config().setPromptedVersion(getLatestVersion().version());
                        onUpgrade();
                    })
                    .addCancel(null)
                    .build());
        }
    }

    private void doAnimation(boolean show) {
        if (AnimationUtils.isAnimationEnabled()) {
            Duration duration = Duration.millis(320);
            Timeline nowAnimation = new Timeline();
            nowAnimation.getKeyFrames().addAll(
                    new KeyFrame(Duration.ZERO,
                            new KeyValue(updatePane.translateXProperty(), show ? 260 : 0, SINE)),
                    new KeyFrame(duration,
                            new KeyValue(updatePane.translateXProperty(), show ? 0 : 260, SINE)));
            if (show) nowAnimation.getKeyFrames().add(
                    new KeyFrame(Duration.ZERO, e -> updatePane.setVisible(true)));
            else nowAnimation.getKeyFrames().add(
                    new KeyFrame(duration, e -> updatePane.setVisible(false)));
            nowAnimation.play();
        } else {
            updatePane.setVisible(show);
        }
    }

    private void launch() {
        Profile profile = Profiles.getSelectedProfile();
        Versions.launch(profile, profile.getSelectedVersion());
    }

    private void launchNoGame() {
        DownloadProvider downloadProvider = DownloadProviders.getDownloadProvider();
        VersionList<?> versionList = downloadProvider.getVersionListById("game");

        Holder<String> gameVersionHolder = new Holder<>();
        Task<?> task = versionList.refreshAsync("")
                .thenSupplyAsync(() -> versionList.getVersions("").stream()
                        .filter(it -> it.getVersionType() == RELEASE)
                        .filter(it -> NativePatcher.checkSupportedStatus(GameVersionNumber.asGameVersion(it.getGameVersion()), Platform.SYSTEM_PLATFORM, OperatingSystem.SYSTEM_VERSION) != NativePatcher.SupportStatus.UNSUPPORTED)
                        .sorted()
                        .findFirst()
                        .orElseThrow(() -> new IOException("No versions found")))
                .thenComposeAsync(version -> {
                    Profile profile = Profiles.getSelectedProfile();
                    DefaultDependencyManager dependency = profile.getDependency();
                    String gameVersion = gameVersionHolder.value = version.getGameVersion();

                    return dependency.gameBuilder()
                            .name(gameVersion)
                            .gameVersion(gameVersion)
                            .buildAsync();
                })
                .whenComplete(any -> profile.getRepository().refreshVersions())
                .whenComplete(Schedulers.javafx(), (result, exception) -> {
                    if (exception == null) {
                        profile.setSelectedVersion(gameVersionHolder.value);
                        launch();
                    } else if (exception instanceof CancellationException) {
                        Controllers.showToast(i18n("message.cancelled"));
                    } else {
                        LOG.warning("Failed to install game", exception);
                        Controllers.dialog(StringUtils.getStackTrace(exception),
                                i18n("install.failed"),
                                MessageDialogPane.MessageType.WARNING);
                    }
                });
        Controllers.taskDialog(task, i18n("version.launch.empty.installing"), TaskCancellationAction.NORMAL);
    }

    private void onUpgrade() {
        RemoteVersion target = UpdateChecker.getLatestVersion();
        if (target == null) {
            return;
        }
        UpdateHandler.updateFrom(target);
    }

    private void closeUpdateBubble() {
        showUpdate.unbind();
        showUpdate.set(false);
    }

    @Override
    public ReadOnlyObjectWrapper<State> stateProperty() {
        return state;
    }

    public Profile getProfile() {
        return profile;
    }

    public String getCurrentGame() {
        return currentGame.get();
    }

    public StringProperty currentGameProperty() {
        return currentGame;
    }

    public void setCurrentGame(String currentGame) {
        this.currentGame.set(currentGame);
    }

    public ObservableList<Version> getVersions() {
        return versions;
    }

    public boolean isShowUpdate() {
        return showUpdate.get();
    }

    public BooleanProperty showUpdateProperty() {
        return showUpdate;
    }

    public void setShowUpdate(boolean showUpdate) {
        this.showUpdate.set(showUpdate);
    }

    public RemoteVersion getLatestVersion() {
        return latestVersion.get();
    }

    public ObjectProperty<RemoteVersion> latestVersionProperty() {
        return latestVersion;
    }

    public void setLatestVersion(RemoteVersion latestVersion) {
        this.latestVersion.set(latestVersion);
    }

    public void initVersions(Profile profile, List<Version> versions) {
        FXUtils.checkFxUserThread();
        this.profile = profile;
        this.versions.setAll(versions);
    }
}
