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
package org.jackhuang.hmcl.ui.download;

import javafx.beans.property.ReadOnlyObjectProperty;
import javafx.beans.property.ReadOnlyObjectWrapper;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.Cursor;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Control;
import javafx.scene.control.Label;
import javafx.scene.control.ListCell;
import javafx.scene.control.ListView;
import javafx.scene.control.Skin;
import javafx.scene.control.SkinBase;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import org.jackhuang.hmcl.mod.ModFavorite;
import org.jackhuang.hmcl.mod.RemoteMod;
import org.jackhuang.hmcl.mod.RemoteModRepository;
import org.jackhuang.hmcl.mod.curse.CurseForgeRemoteModRepository;
import org.jackhuang.hmcl.mod.modrinth.ModrinthRemoteModRepository;
import org.jackhuang.hmcl.download.DownloadProvider;
import org.jackhuang.hmcl.setting.DownloadProviders;
import org.jackhuang.hmcl.setting.ModFavorites;
import org.jackhuang.hmcl.setting.Profile;
import org.jackhuang.hmcl.setting.Profiles;
import org.jackhuang.hmcl.task.Schedulers;
import org.jackhuang.hmcl.task.Task;
import org.jackhuang.hmcl.ui.Controllers;
import org.jackhuang.hmcl.ui.FXUtils;
import org.jackhuang.hmcl.ui.construct.ImageContainer;
import org.jackhuang.hmcl.ui.construct.RipplerContainer;
import org.jackhuang.hmcl.ui.construct.TwoLineListItem;
import org.jackhuang.hmcl.ui.decorator.DecoratorPage;
import org.jackhuang.hmcl.ui.versions.DownloadListPage;
import org.jackhuang.hmcl.ui.versions.ModTranslations;
import org.jackhuang.hmcl.util.StringUtils;
import org.jackhuang.hmcl.util.i18n.I18n;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;

import static org.jackhuang.hmcl.util.i18n.I18n.i18n;
import static org.jackhuang.hmcl.util.logging.Logger.LOG;

/// 模组收藏页面
/// 显示用户收藏的模组列表，支持批量下载
public class ModFavoritePage extends Control implements DecoratorPage {

    private final ReadOnlyObjectWrapper<State> state = new ReadOnlyObjectWrapper<>(State.fromTitle(i18n("mods.favorite")));

    public ModFavoritePage() {
    }

    @Override
    public ReadOnlyObjectProperty<State> stateProperty() {
        return state.getReadOnlyProperty();
    }

    @Override
    protected Skin<?> createDefaultSkin() {
        return new ModFavoritePageSkin(this);
    }

    private static class ModFavoritePageSkin extends SkinBase<ModFavoritePage> {

        private final List<CheckBox> checkBoxes = new ArrayList<>();

        ModFavoritePageSkin(ModFavoritePage control) {
            super(control);

            BorderPane pane = new BorderPane();
            pane.getStyleClass().add("gray-background");

            // 顶部工具栏
            HBox toolbar = new HBox(8);
            toolbar.setPadding(new Insets(8, 16, 8, 16));
            toolbar.setAlignment(Pos.CENTER_LEFT);

            Button selectAllButton = new Button(i18n("mods.favorite.selectAll"));
            Button deselectAllButton = new Button(i18n("mods.favorite.deselectAll"));
            Button batchDownloadButton = new Button(i18n("mods.favorite.batchDownload"));

            toolbar.getChildren().setAll(selectAllButton, deselectAllButton, batchDownloadButton);

            ListView<ModFavorite> listView = new ListView<>();
            listView.getStyleClass().add("no-horizontal-scrollbar");
            listView.setItems(ModFavorites.getFavorites());

            listView.setCellFactory(x -> new ListCell<>() {
                private final StackPane wrapper = new StackPane();
                private final TwoLineListItem content = new TwoLineListItem();
                private final ImageContainer imageContainer = new ImageContainer(40);
                private final CheckBox checkBox = new CheckBox();

                {
                    setPadding(new Insets(9, 9, 0, 9));
                    checkBoxes.add(checkBox);

                    HBox container = new HBox(8);
                    container.setPadding(new Insets(8));
                    container.setCursor(Cursor.HAND);
                    container.setAlignment(Pos.CENTER_LEFT);
                    imageContainer.setMouseTransparent(true);
                    container.getChildren().setAll(checkBox, imageContainer, content);
                    HBox.setHgrow(content, Priority.ALWAYS);

                    RipplerContainer graphic = new RipplerContainer(container);
                    wrapper.getChildren().setAll(graphic);

                    // 点击列表项打开详情页
                    FXUtils.onClicked(wrapper, () -> {
                        ModFavorite item = getItem();
                        if (item == null) return;

                        RemoteModRepository.Type repoType = RemoteModRepository.Type.valueOf(item.getRepoType());
                        RemoteModRepository repository = switch (repoType) {
                            case MOD -> ModrinthRemoteModRepository.MODS;
                            case RESOURCE_PACK -> ModrinthRemoteModRepository.RESOURCE_PACKS;
                            case SHADER_PACK -> ModrinthRemoteModRepository.SHADER_PACKS;
                            case MODPACK -> ModrinthRemoteModRepository.MODPACKS;
                            default -> CurseForgeRemoteModRepository.MODS;
                        };

                        DownloadListPage listPage = new DownloadListPage(repository);
                        
                        // 使用当前配置的下载提供商
                        DownloadProvider downloadProvider = DownloadProviders.getDownloadProvider();
                        
                        org.jackhuang.hmcl.task.Task.supplyAsync(() -> repository.getModById(downloadProvider, item.getSlug()))
                                .whenComplete(Schedulers.javafx(), (result, exception) -> {
                                    if (exception != null) {
                                        Controllers.dialog("Failed to load mod info: " + exception.getMessage());
                                        return;
                                    }
                                    if (result != null) {
                                        Controllers.navigate(new org.jackhuang.hmcl.ui.versions.DownloadPage(
                                                listPage, result, new Profile.ProfileVersion(null, null), null));
                                    } else {
                                        Controllers.dialog("Mod not found: " + item.getSlug());
                                    }
                                }).start();
                    });

                    setPrefWidth(0);
                }

                @Override
                protected void updateItem(ModFavorite item, boolean empty) {
                    super.updateItem(item, empty);
                    if (empty || item == null) {
                        setGraphic(null);
                    } else {
                        ModTranslations.Mod mod = ModTranslations.getTranslationsByRepositoryType(
                                RemoteModRepository.Type.valueOf(item.getRepoType()))
                                .getModByCurseForgeId(item.getSlug());
                        content.setTitle(mod != null && I18n.isUseChinese() ? mod.getDisplayName() : item.getTitle());
                        String description = item.getDescription();
                        if (description != null) {
                            description = description.replaceAll("\\R", " ");
                        }
                        content.setSubtitle(description);
                        content.getTags().clear();
                        for (String category : item.getCategories()) {
                            content.addTag(category);
                        }
                        if (StringUtils.isNotBlank(item.getIconUrl())) {
                            imageContainer.imageProperty().bind(FXUtils.newRemoteImage(item.getIconUrl(), 80, 80, true, true));
                        }
                        setGraphic(wrapper);
                    }
                }
            });

            // 全选按钮
            selectAllButton.setOnAction(e -> {
                for (CheckBox cb : checkBoxes) {
                    cb.setSelected(true);
                }
            });

            // 取消全选按钮
            deselectAllButton.setOnAction(e -> {
                for (CheckBox cb : checkBoxes) {
                    cb.setSelected(false);
                }
            });

            // 批量下载按钮
            batchDownloadButton.setOnAction(e -> {
                List<ModFavorite> selected = new ArrayList<>();
                for (int i = 0; i < listView.getItems().size(); i++) {
                    if (i < checkBoxes.size() && checkBoxes.get(i).isSelected()) {
                        selected.add(listView.getItems().get(i));
                    }
                }
                if (selected.isEmpty()) {
                    Controllers.dialog(i18n("mods.favorite.noSelection"));
                    return;
                }
                
                // 获取当前配置文件的所有版本
                Profile profile = Profiles.getSelectedProfile();
                List<String> versions = profile.getRepository().getVersions()
                    .stream()
                    .map(org.jackhuang.hmcl.game.Version::getId)
                    .collect(java.util.stream.Collectors.toList());
                
                if (versions.isEmpty()) {
                    Controllers.dialog("未找到可用的游戏版本");
                    return;
                }
                
                // 创建版本选择对话框
                var builder = new org.jackhuang.hmcl.ui.construct.PromptDialogPane.Builder(
                    "选择目标版本\n将下载 " + selected.size() + " 个模组到指定版本",
                    (questions, handler) -> handler.resolve()
                );
                
                var versionQuestion = new org.jackhuang.hmcl.ui.construct.PromptDialogPane.Builder.CandidatesQuestion(
                    "目标版本：",
                    versions.toArray(new String[0])
                );
                
                builder.addQuestion(versionQuestion);
                
                Controllers.prompt(builder).whenComplete((questions, exception) -> {
                    if (exception != null || questions == null) {
                        return; // 用户取消
                    }
                    
                    // 获取用户选择的版本
                    int selectedIndex = versionQuestion.getValue();
                    String targetVersion = versions.get(selectedIndex);
                    
                    LOG.info("Starting batch download of " + selected.size() + " mods to version: " + targetVersion);
                    
                    // 执行批量下载
                    DownloadProvider downloadProvider = DownloadProviders.getDownloadProvider();
                    
                    // 性能优化：统计信息
                    AtomicInteger successCount = new AtomicInteger(0);
                    AtomicInteger skipCount = new AtomicInteger(0);
                    AtomicInteger failCount = new AtomicInteger(0);
                    AtomicInteger retryCount = new AtomicInteger(0);
                    
                    // 获取目标目录
                    Path targetDir = profile.getRepository().hasVersion(targetVersion) 
                        ? profile.getRepository().getRunDirectory(targetVersion) 
                        : profile.getRepository().getBaseDirectory();
                    
                    // 并发控制：最多同时下载 3 个
                    final int MAX_CONCURRENT_DOWNLOADS = 3;
                    java.util.concurrent.Semaphore semaphore = new java.util.concurrent.Semaphore(MAX_CONCURRENT_DOWNLOADS);
                    
                    // 构建下载任务列表
                    List<Task<Void>> downloadTasks = new ArrayList<>();
                    
                    for (ModFavorite favorite : selected) {
                        Task<Void> downloadTask = Task.supplyAsync(() -> {
                            // 获取信号量，限制并发数量
                            try {
                                semaphore.acquire();
                            } catch (InterruptedException ex) {
                                Thread.currentThread().interrupt();
                                return null;
                            }
                            
                            try {
                                RemoteModRepository.Type repoType = RemoteModRepository.Type.valueOf(favorite.getRepoType());
                                RemoteModRepository repository = switch (repoType) {
                                    case MOD -> ModrinthRemoteModRepository.MODS;
                                    case RESOURCE_PACK -> ModrinthRemoteModRepository.RESOURCE_PACKS;
                                    case SHADER_PACK -> ModrinthRemoteModRepository.SHADER_PACKS;
                                    case MODPACK -> ModrinthRemoteModRepository.MODPACKS;
                                    default -> CurseForgeRemoteModRepository.MODS;
                                };
                                
                                String subdirectory = switch (repoType) {
                                    case RESOURCE_PACK -> "resourcepacks";
                                    case SHADER_PACK -> "shaderpacks";
                                    default -> "mods";
                                };
                                
                                Path downloadDir = targetDir.resolve(subdirectory);
                                
                                // 获取模组信息（带重试）
                                RemoteMod mod = null;
                                int retries = 0;
                                int maxRetries = 3;
                                
                                while (retries < maxRetries && mod == null) {
                                    try {
                                        mod = repository.getModById(downloadProvider, favorite.getSlug());
                                    } catch (Exception ex1) {
                                        retries++;
                                        retryCount.incrementAndGet();
                                        if (retries < maxRetries) {
                                            LOG.warning("Retry " + retries + "/" + maxRetries + " for: " + favorite.getTitle());
                                            Thread.sleep(1000 * retries); // 递增等待时间
                                        } else {
                                            LOG.warning("Failed to get mod info after " + maxRetries + " retries: " + favorite.getSlug(), ex1);
                                            return null;
                                        }
                                    }
                                }
                                
                                if (mod == null) return null;
                                
                                // 获取最新版本
                                RemoteMod.Version latestVersion = null;
                                try {
                                    var versionsStream = repository.getRemoteVersionsById(downloadProvider, favorite.getSlug());
                                    if (versionsStream != null) {
                                        var versionsList = versionsStream.toList();
                                        if (!versionsList.isEmpty()) {
                                            latestVersion = versionsList.get(0);
                                        }
                                    }
                                } catch (Exception ex2) {
                                    LOG.warning("Failed to get versions for: " + favorite.getSlug(), ex2);
                                    return null;
                                }
                                
                                if (latestVersion == null) {
                                    LOG.warning("No version found for: " + favorite.getTitle());
                                    return null;
                                }
                                
                                // 检查文件是否已存在（性能优化：跳过已下载的文件）
                                String fileName = latestVersion.getName();
                                if (fileName == null || fileName.isEmpty()) {
                                    fileName = favorite.getSlug() + ".jar";
                                }
                                
                                Path targetFile = downloadDir.resolve(fileName);
                                if (java.nio.file.Files.exists(targetFile)) {
                                    LOG.info("Skipping (already exists): " + favorite.getTitle());
                                    skipCount.incrementAndGet();
                                    return null;
                                }
                                
                                // 执行下载（带重试）
                                retries = 0;
                                while (retries < maxRetries) {
                                    try {
                                        LOG.info("Downloading [" + (successCount.get() + 1) + "/" + selected.size() + "]: " + favorite.getTitle());
                                        org.jackhuang.hmcl.ui.download.DownloadPage.download(
                                            downloadProvider, profile, targetVersion, latestVersion, subdirectory
                                        );
                                        successCount.incrementAndGet();
                                        LOG.info("Successfully downloaded: " + favorite.getTitle());
                                        return null;
                                    } catch (Exception ex3) {
                                        retries++;
                                        retryCount.incrementAndGet();
                                        if (retries < maxRetries) {
                                            LOG.warning("Retry " + retries + "/" + maxRetries + " for: " + favorite.getTitle());
                                            Thread.sleep(1000 * retries);
                                        } else {
                                            failCount.incrementAndGet();
                                            LOG.warning("Failed to download after " + maxRetries + " retries: " + favorite.getTitle(), ex3);
                                            return null;
                                        }
                                    }
                                }
                                
                                return null;
                            } finally {
                                semaphore.release(); // 释放信号量
                            }
                        });
                        
                        downloadTasks.add(downloadTask);
                    }
                    
                    // 执行所有下载任务
                    Task.allOf(downloadTasks)
                        .whenComplete(Schedulers.javafx(), (result, ex) -> {
                            if (ex != null) {
                                LOG.warning("Batch download failed", ex);
                                Controllers.dialog("批量下载失败: " + ex.getMessage());
                            } else {
                                LOG.info("Batch download completed successfully");
                                
                                // 显示详细统计报告
                                StringBuilder report = new StringBuilder();
                                report.append("批量下载完成！\n\n");
                                report.append("总计: ").append(selected.size()).append(" 个模组\n");
                                report.append("成功: ").append(successCount.get()).append("\n");
                                
                                if (skipCount.get() > 0) {
                                    report.append("跳过: ").append(skipCount.get()).append(" (已存在)\n");
                                }
                                
                                if (failCount.get() > 0) {
                                    report.append("失败: ").append(failCount.get()).append("\n");
                                }
                                
                                if (retryCount.get() > 0) {
                                    report.append("重试: ").append(retryCount.get()).append(" 次\n");
                                }
                                
                                report.append("\n目标版本: ").append(targetVersion);
                                
                                Controllers.dialog(report.toString());
                            }
                        })
                        .start();
                });
            });

            VBox emptyPane = new VBox(8);
            emptyPane.setAlignment(Pos.CENTER);
            emptyPane.getChildren().setAll(
                    new Label(i18n("mods.favorite.empty")),
                    new Label(i18n("mods.favorite.empty.hint"))
            );

            // 动态切换空视图和列表视图
            ModFavorites.getFavorites().addListener((javafx.collections.ListChangeListener<ModFavorite>) c -> {
                if (ModFavorites.getFavorites().isEmpty()) {
                    pane.setCenter(emptyPane);
                    pane.setTop(null);
                } else {
                    pane.setCenter(listView);
                    pane.setTop(toolbar);
                }
            });

            // 初始状态
            if (ModFavorites.getFavorites().isEmpty()) {
                pane.setCenter(emptyPane);
            } else {
                pane.setCenter(listView);
                pane.setTop(toolbar);
            }

            getChildren().setAll(pane);
        }
    }
}
