package org.jackhuang.hmcl;

import org.jackhuang.hmcl.util.SwingUtils;

import javax.swing.*;
import java.io.File;
import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.file.*;
import java.security.CodeSource;
import java.security.ProtectionDomain;
import java.util.*;
import java.util.stream.Collectors;
import java.util.stream.Stream;

public final class Main {
    private static final int MINIMUM_JAVA_VERSION = 17;
    private static final String DOWNLOAD_PAGE = "https://hmcl.huangyuhui.net/download/";

    private Main() {
    }

    static int findFirstNotNumber(String str, int start) {
        if (start >= str.length())
            return -1;

        char ch = str.charAt(start);
        if (ch < '0' || ch > '9')
            return -1;

        for (int i = start + 1; i < str.length(); i++) {
            ch = str.charAt(i);
            if (ch < '0' || ch > '9')
                return i;
        }
        return str.length();
    }

    static int getJavaFeatureVersion(String javaVersion) {
        if (javaVersion == null)
            return -1;

        try {
            int end = findFirstNotNumber(javaVersion, 0);
            if (end < 0)
                return -1; // No valid version number found

            int major = Integer.parseInt(javaVersion.substring(0, end));
            if (major > 1)
                return major;

            if (major < 1)
                return -1; // Invalid major version

            // Java 1.x versions
            int start = end + 1;
            end = findFirstNotNumber(javaVersion, start);

            if (end < 0)
                return -1; // No valid minor version found

            return Integer.parseInt(javaVersion.substring(start, end));
        } catch (NumberFormatException e) {
            return -1; // version number is too long
        }
    }

    static void showErrorAndExit(String[] args) {
        SwingUtils.initLookAndFeel();

        ResourceBundle resourceBundle = BootProperties.getResourceBundle();
        String errorTitle = resourceBundle.getString("boot.message.error");

        if (args.length > 0 && args[0].equals("--apply-to")) {
            String errorMessage = resourceBundle.getString("boot.manual_update");
            System.err.println(errorMessage);
            int result = JOptionPane.showOptionDialog(null, errorMessage, errorTitle, JOptionPane.YES_NO_OPTION,
                    JOptionPane.ERROR_MESSAGE, null, null, null);

            if (result == JOptionPane.YES_OPTION) {
                System.out.println("Open " + DOWNLOAD_PAGE);
                DesktopUtils.openLink(DOWNLOAD_PAGE);
            }
        } else {
            String errorMessage = resourceBundle.getString("boot.unsupported_java_version");
            System.err.println(errorMessage);
            SwingUtils.showErrorDialog(errorMessage, errorTitle);
        }

        System.exit(1);
    }

    private static void checkDirectoryPath() {
        String currentDir = System.getProperty("user.dir", "");
        String jarPath = getThisJarPath();
        if (currentDir.contains("!")) {
            SwingUtils.initLookAndFeel();
            System.err.println("The current working path contains an exclamation mark: " + currentDir);
            SwingUtils.showErrorDialog("Exclamation mark(!) is not allowed in the working path.\n" + "The path is " + currentDir);
            System.exit(1);
        } else if (jarPath != null && jarPath.contains("!")) {
            SwingUtils.initLookAndFeel();
            System.err.println("The jar path contains an exclamation mark: " + jarPath);
            SwingUtils.showErrorDialog("Exclamation mark(!) is not allowed in the path where HMCL is in.\n" + "The path is " + jarPath);
            System.exit(1);
        }
    }

    private static String getThisJarPath() {
        ProtectionDomain protectionDomain = Main.class.getProtectionDomain();
        if (protectionDomain == null)
            return null;

        CodeSource codeSource = protectionDomain.getCodeSource();
        if (codeSource == null || codeSource.getLocation() == null)
            return null;

        try {
            return Paths.get(codeSource.getLocation().toURI()).toAbsolutePath().normalize().toString();
        } catch (FileSystemNotFoundException | IllegalArgumentException | URISyntaxException e) {
            return null;
        }
    }

    // ========= 新增：查找缓存的 JavaFX JAR =========
    private static List<File> findJavaFXJars() {
        Path cacheDir = Paths.get(System.getProperty("user.home"), ".hmcl", "cache");
        if (!Files.isDirectory(cacheDir)) {
            return Collections.emptyList();
        }
        try (Stream<Path> stream = Files.list(cacheDir)) {
            return stream
                    .filter(Files::isRegularFile)
                    .filter(path -> {
                        String name = path.getFileName().toString();
                        return name.startsWith("javafx-") && name.endsWith(".jar");
                    })
                    .map(Path::toFile)
                    .collect(Collectors.toList());
        } catch (IOException e) {
            return Collections.emptyList();
        }
    }

    // ========= 新增：以模块路径重启 JVM =========
    private static void restartWithModulePath(String[] args) throws Exception {
        List<File> javafxJars = findJavaFXJars();
        if (javafxJars.isEmpty()) {
            EntryPoint.main(args); // 找不到 JavaFX 缓存，回退到普通运行
            return;
        }

        String javaExe = Paths.get(System.getProperty("java.home"), "bin", "java").toString();

        List<String> command = new ArrayList<>();
        command.add(javaExe);

        // 构建模块路径
        String modulePath = javafxJars.stream()
                .map(File::getAbsolutePath)
                .collect(Collectors.joining(File.pathSeparator));
        command.add("--module-path");
        command.add(modulePath);

        // 添加 JavaFX 模块
        command.add("--add-modules=javafx.controls,javafx.graphics,javafx.base");

        // 添加所有必需的 --add-opens
        String[] addOpens = {
                "java.base/java.lang=ALL-UNNAMED",
                "java.base/java.lang.reflect=ALL-UNNAMED",
                "java.base/jdk.internal.loader=ALL-UNNAMED",
                "javafx.graphics/javafx.css=ALL-UNNAMED",
                "javafx.graphics/javafx.stage=ALL-UNNAMED",
                "javafx.graphics/javafx.scene=ALL-UNNAMED",
                "javafx.graphics/com.sun.glass.ui=ALL-UNNAMED",
                "javafx.graphics/com.sun.javafx.stage=ALL-UNNAMED",
                "javafx.graphics/com.sun.javafx.util=ALL-UNNAMED",
                "javafx.graphics/com.sun.prism=ALL-UNNAMED",
                "javafx.controls/com.sun.javafx.scene.control=ALL-UNNAMED",
                "javafx.controls/com.sun.javafx.scene.control.behavior=ALL-UNNAMED",
                "javafx.graphics/com.sun.javafx.tk.quantum=ALL-UNNAMED",
                "javafx.controls/javafx.scene.control.skin=ALL-UNNAMED",
                "jdk.attach/sun.tools.attach=ALL-UNNAMED"
        };
        for (String open : addOpens) {
            command.add("--add-opens=" + open);
        }
        command.add("--enable-native-access=ALL-UNNAMED");

        // 标记已重启，避免无限循环
        command.add("-Dhmcl.add-opens.applied=true");

        command.add("-jar");
        command.add(getThisJarPath());

        // 传递原始参数
        command.addAll(Arrays.asList(args));

        ProcessBuilder pb = new ProcessBuilder(command);
        pb.inheritIO();
        Process process = pb.start();
        int exitCode = process.waitFor();
        System.exit(exitCode);
    }

    // ========= 主入口 =========
    public static void main(String[] args) throws Throwable {
        checkDirectoryPath();
        if (getJavaFeatureVersion(System.getProperty("java.version")) < MINIMUM_JAVA_VERSION) {
            showErrorAndExit(args);
            return;
        }

        // 如果已经是重启过的进程，直接运行主程序
        if (System.getProperty("hmcl.add-opens.applied") != null) {
            EntryPoint.main(args);
            return;
        }

        // 检测当前是否为模块化 JavaFX 环境，如果不是则重启
        boolean needRestart = true;
        try {
            ModuleLayer.boot().findModule("javafx.graphics").orElseThrow();
            needRestart = false; // 已存在 javafx.graphics 模块，说明加载正确
        } catch (Exception ignored) {
        }

        if (needRestart) {
            restartWithModulePath(args);
        } else {
            EntryPoint.main(args);
        }
    }
}