# 捆绑网页组件与素材核查

日期：2026-10-14，更新至 Apple Silicon v14.4-rc5。此清单记录已找到的文件证据与尚缺材料，不是完整再分发或安全审计通过的声明。

## 已核对的组件标识

| 组件 | 本地版本证据 | 本地许可证据 |
| --- | --- | --- |
| MathJax | MathJax.js 声明 2.7.6 | Apache 2.0 版权头；补入官方 2.7.6 标签的完整 LICENSE |
| jQuery | 编辑器 3.3.1、1.11.3；欢迎页 1.11.3 | 原版权头与许可网址保留；补入官方 3.3.1 标签的完整 MIT 许可 |
| jQuery UI | 欢迎页 1.12.1 自定义构建 | 版权头声明 MIT |
| Underscore | 1.8.3 | 版权头声明 MIT |
| Raphaël | 2.1.4，包含 Eve 0.4.2 | 版权头声明 MIT；嵌入依赖仍需精确来源核对 |
| flowchart.js | 1.11.3 | 版权头声明 MIT |
| js-sequence-diagrams | 2.0.1 | 版权头声明 Simplified BSD；包含生成解析器，需补齐对应声明 |
| Kity | 外层构建头 2.0.4，内部代码另有 2.0.0 标识 | 构建头声明 BSD；精确源码与一致版本待核对 |
| KityMinder Core | 1.4.52 | 构建头声明 BSD-3-Clause |
| Qt WebChannel JS | 自带 2016 Qt／KDAB 版权头 | LGPL 许可段保留 |
| JsonCpp | 捆绑 C++ 文件 | 源码含 MIT 完整许可段及其他选择 |
| FramelessHelper | 固定子模块 f4d20e5 | MIT 完整 LICENSE；此次补入 App 许可目录 |

这些标识来自已捆绑文件，不证明它们与原发布包逐字节相同。没有替换编辑器、Markdown 解析器或数学渲染器，以免改变当前编辑、CSS 和 PDF 行为。

## 已找到的缺口

- marked-0.7.0.min.js 的文件名有版本标识，但本地开头没有许可头；不能只凭文件名完成归属或许可证核查。
- i18next.min.js 开头没有明确版本／许可标识，需核对确切版本和对应源码。
- wizEditorForMac.js 自述编辑器 1.1.6、内容模块 0.1.7、绘图模块 0.1.9，但它是压缩合并文件，包含进一步依赖；需要核对对应源码及完整第三方声明。
- WizContent.js、WizSvgPainter.js 以及旧代码高亮文件的独立来源／许可声明未完整定位，暂不能用主项目 GPL 声明替代每个依赖的核查。
- share 下有 2 个字体文件（同一套 wizEditorIcons 的 TTF 与 WOFF）。两者命名表均未包含版权、许可说明或许可网址字段。WOFF 额外元数据的偏移与长度均为 0，也没有独立许可字段。它不证明该字体不可使用，也不能推断字体自动属于 MIT、SIL 或 GPL；来源许可仍待确认。
- share 下 568 个 PNG／GIF／JPEG／SVG／ICNS 图像文件未逐项确认权利来源；包括历史界面素材，不能凭可公开下载认定全部可再分发。
- Qt 6.11.3／Chromium 140.0.7339.225 的精确版本、完整第三方许可声明和对应源码仍需补齐。此前只读探查未找到可直接提取的独立许可页面，不代表运行时没有任何许可声明。

字体命名字段的解释依据：[OpenType naming table](https://learn.microsoft.com/en-us/typography/opentype/spec/name)。记录的组件证据位于主仓库 share/files、src/share/jsoncpp 和固定子模块，不扫描用户笔记或附件。

本次补入的完整文本分别来自 [MathJax 2.7.6 LICENSE](https://github.com/mathjax/MathJax/blob/2.7.6/LICENSE) 和 [jQuery 3.3.1 LICENSE](https://github.com/jquery/jquery/blob/3.3.1/LICENSE.txt)，保存于 build/osx/licenses，自动随包进入 Full-license-texts。保留原版权头；添加许可文本不代表所有压缩组件或对应源码核查都已通过。jQuery 1.11.3、其他组件和素材缺口仍需分别补齐。

## 后续顺序

先补齐各已识别组件的精确版许可文件与对应源码，再解决字体、图像和压缩编辑器的来源缺口；最后核对 Qt／Chromium 的完整声明。不能用更新到最新版或移除现有组件替代兼容性与许可核查。

Apple 签名／公证是独立步骤，仍需要有效 Developer ID 身份。真实账号与长期同步验证也独立于许可证核查：由用户在备份后验证，不代理修改正式笔记。
