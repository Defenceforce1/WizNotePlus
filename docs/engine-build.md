# 固定 Qt 引擎候选的构建与打包

仅开发说明，不是自动更新／安装命令，也不代表所有验收已完成。日常用户使用已验证安装包即可，无需自行编译。原编辑器、MathJax、资料格式、账号和目录保持；不把新组件直接装进原 SDK。

## 来源与边界

- Qt／WebEngine：6.11.3；模块 `8c73295524f3ce6a2e10ad2f71141c695376c876`。
- Chromium gitlink：`7927ce61b5a302a3da8172eefbe260ec17484bcd`。
- 官方源组件：`https://download.qt.io/online/qtsdkrepository/all_os/extensions/qtwebengine/6113/src_doc_examples/Updates.xml`。
- 归档为 `extensions.qtwebengine.6113.src/6.11.3-0-202609240437qtwebengine-everywhere-src-6.11.3.tar.xz`，SHA1 `e545e2a47265b873f685a513f2768c259799c623`。归档没有顶层包装目录，解压到全新目录，**不要 strip-components**；先校验相对路径和校验值。
- 必须使用对应原 SDK，不能仅凭同一个版本号混用自编 QtBase／Quick。打包工具还核对已审阅的四个底层 arm64 构建身份。
- Metal 修补只针对固定 Quick Widgets 离屏路径，不能推广到仍有 GPU 命令在途的普通 Qt Quick。完整源码／许可证审核和跨系统兼容不因一份清单自动完成。

## 隔离构建

使用完整 Xcode 和实际可用的官方 Metal Toolchain，检查 `xcrun --sdk macosx metal --version`；只找到 metal 路径可能仍是占位程序。不要关闭 Metal、压过工具检测或抑制 SDK 新于 Qt 测试版本的警告。Python 使用任务独立环境和 `tests/standalone-qt-pdf/engine-patches/requirements-build.txt`，不改系统 Python。

示例中的路径均由维护者替换为独立绝对路径；build／stage／runtime 不能指向原 SDK 或 Applications。先构建未修改的原版并确认与原 SDK 的 PDF／内存行为一致，再应用固定补丁、同配置增量构建。各完整运行时单独复制保存，正在验证时不得重链接它。

```sh
env PATH="$qt_engine_tools:$PATH" NINJAFLAGS=-j6 \
  "$qt_engine_tools/cmake" -S "$qt_engine_source" -B "$qt_engine_build" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$qt_engine_sdk" \
  -DCMAKE_INSTALL_PREFIX="$qt_engine_stage" \
  -DCMAKE_MAKE_PROGRAM="$qt_engine_tools/ninja" \
  -DNinja_EXECUTABLE="$qt_engine_tools/ninja" \
  -DPython3_EXECUTABLE="$qt_engine_python" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DQT_IS_MACOS_UNIVERSAL=OFF \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF \
  -DFEATURE_webenginedriver=OFF
env PATH="$qt_engine_tools:$PATH" \
  "$qt_engine_tools/cmake" --build "$qt_engine_build" \
  --target WebEngineWidgets QtWebEngineProcess --parallel 2
```

`NINJAFLAGS` 在 configure 时进入嵌套构建；六任务仅是本机 32 GiB／12 核的观察值，需按压力减少，不能直接无限并行。SDK 导出 universal 默认值，单设 arm64 不够，必须显式关闭 universal。13.0 是匹配现有引擎的编译目标，不是新的主 App 最低系统承诺。

检查缓存中 `QT_FEATURE_qtwebengine_core_build` 确实为 ON；CMake 退出成功但跳过 WebEngine不是编译成功。Qt 的本版本将 Chromium test 模板设为空 group，`blink_unittests` 无工作不代表上游单测执行。

补丁固定值和检查入口分别在 `tests/standalone-qt-pdf/engine-patches`、`tests/standalone-qt-memory/engine-patches`。诊断／开关原型用于归因，不能自动混同后续干净发布候选；以相应状态的源码哈希为准。

构建成功后，将 `lib/QtWebEngineCore.framework`、`lib/QtWebEngineWidgets.framework`（包括同次 helper／资源）复制到新 runtime 的 `lib/`，其他框架和插件仅链接到匹配 SDK。runtime 是开发输入，不是独立安装包；不能删除 SDK 后再指望这些链接可运行。

## 只创建新候选，不覆盖输入

基础 App 应从当前主程序源码按 `build/osx/package-arm64.sh` 正常部署，不能用不明旧程序替代。然后显式记录／合并引擎，并按原 arm64 资源规则精简：

```sh
python3 build/osx/engine-candidate.py record \
  "$qt_engine_source" "$qt_engine_runtime" "$qt_engine_manifest"
python3 build/osx/engine-candidate.py package \
  "$main_base_app" "$qt_engine_runtime" "$qt_engine_manifest" "$main_new_app"
python3 build/osx/thin-arm64-bundle.py "$main_new_app" "$main_thin_app"
```

输出 App 必须是全新绝对路径，保留项目英文文件名；拒绝 Applications、覆盖和回指输入的别名。原 SDK、基础 App 和回退包不动。记录只证明指定文件／资源清单，不证明整个源码树、实际编译或功能验收。

工具保留其余框架、编辑器、数学字体、PDF、图片／媒体与工具资源，只替换新副本中的两个引擎框架，移除开发元数据／本地符号，重新签名。仍为免费 ad-hoc、未公证候选；不下载证书，不改变系统保护，不自动安装。

最后必须运行 9 场景几何、实际旧阅读器／公式、保存／打印生命周期、内存正负／压力、完整主程序和包内测试，重新核对签名、依赖、原图标与冻结回退。真实前台焦点／红黄绿／全屏必须在解锁会话验证，不把锁屏时的失败改成通过。见第二十三至二十五轮记录。
