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

import org.jackhuang.hmcl.setting.UILibrary;
import org.jackhuang.hmcl.ui.UILibraryManager;

/// 组件管理器，统一管理 UI 组件工厂
/// 提供全局访问点，简化组件创建流程
public final class ComponentManager {
    
    private static volatile ComponentManager instance;
    private final UIComponentFactory factory;
    
    private ComponentManager() {
        this.factory = createFactory(UILibraryManager.getCurrentLibrary());
    }
    
    /// 获取组件管理器实例（懒加载，线程安全）
    public static ComponentManager getInstance() {
        ComponentManager localInstance = instance;
        if (localInstance == null) {
            synchronized (ComponentManager.class) {
                localInstance = instance;
                if (localInstance == null) {
                    instance = localInstance = new ComponentManager();
                }
            }
        }
        return localInstance;
    }
    
    /// 获取当前 UI 组件工厂
    public static UIComponentFactory getFactory() {
        return getInstance().factory;
    }
    
    /// 根据 UI 库类型创建对应的组件工厂
    /// 注意：目前仅支持 AtlantaFX，JFoenix 仅保留特殊组件
    private UIComponentFactory createFactory(UILibrary library) {
        // 默认且唯一使用 AtlantaFX
        return AtlantaFXComponentFactory.INSTANCE;
    }
    
    /// 刷新组件管理器（切换 UI 库后调用）
    public static void refresh() {
        synchronized (ComponentManager.class) {
            instance = new ComponentManager();
        }
    }
}
