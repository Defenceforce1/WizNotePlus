# WizNotePlus 使用说明：CSS 与数学笔记

适用于 Apple Silicon 14.4.14—14.4.16。重点是改变笔记正文的外观，不改变原来的 Markdown 编辑方式，也不需要安装 TeX。

## 1. 先试一下 CSS

1. 打开一篇 Markdown 笔记，切到阅读预览。
2. 选择菜单“工具 → 笔记样式（CSS）…”。
3. 勾选“启用自定义 CSS”。如果已经有样式，先复制一份留作备份，不要直接清空。
4. 将下面的 CSS 追加到编辑框末尾，点击“预览到当前笔记”。

```css
/* 先用明显的字号、行距和颜色确认效果 */
.wiz-editor-body.markdown-body p {
  font-size: 18px;
  line-height: 1.85;
  color: #263950;
}
```

满意后点击“保存”。不想保留则点击“取消”，恢复打开设置前的样式。

只复制代码框里的 CSS，不要把三反引号、`css` 字样或 `<style>...</style>` 一起粘进去。笔记正文仍写正常 Markdown，不用把整份 CSS 填进每篇笔记。

## 2. 这份 CSS 作用在哪里

- 保存后作用于本机当前资料设置下的笔记，不需要逐篇设置；下面示例用选择器限制为 Markdown 阅读预览。
- 不会改写 Markdown 源码，不会把预览编号、边框等装饰写进笔记正文。
- 不是整个 App 的主题：不能用它调整搜索框、标题栏或侧栏。
- 源码编辑区和阅读预览不是同一种页面，阅读样式不一定影响正在输入的 Markdown 源码。
- CSS 是本机设置，不会随账号自动同步到其他 Mac。另一台电脑需要复制同一份 CSS 并保存。

常用选择器：

| 选择器 | 用途 |
| --- | --- |
| `.wiz-editor-body.markdown-body` | Markdown 阅读正文的容器 |
| `.wiz-editor-body.markdown-body p` | 普通段落 |
| `.wiz-editor-body.markdown-body h1` | 一级标题；二、三级为 `h2`、`h3` |
| `.wiz-editor-body.markdown-body blockquote` | 普通 `>` 引用块 |
| `.wiz-editor-body.markdown-body .theorem` | 启用数学环境后的定理块 |
| `.wiz-editor-body.markdown-body .definition` | 启用数学环境后的定义块 |
| `.wiz-editor-body.markdown-body .proof` | 启用数学环境后的证明块 |

如果希望影响富文本正文，可以使用 `.wiz-editor-body`，不加 `.markdown-body`。但富文本经常已经带有自己的字号、颜色，简单的继承规则可能覆盖不了它；建议先只设置 Markdown，不用全局 `*` 或大量 `!important` 强行统一所有内容。

## 3. 一套可直接使用的阅读样式

将下面这份 CSS 放进设置。已有数学环境模板时，可以把它放在模板之前，保留后面的数学分类规则。

```css
/* 正文：字号、行距、颜色和适度留白 */
.wiz-editor-body.markdown-body {
  box-sizing: border-box;
  max-width: 920px;
  margin: 0 auto;
  padding: 24px 28px;
  font-family: -apple-system, BlinkMacSystemFont, "PingFang SC", sans-serif;
  font-size: 17px;
  line-height: 1.75;
  color: #263950;
  background: #ffffff;
}

.wiz-editor-body.markdown-body p,
.wiz-editor-body.markdown-body li {
  font-size: inherit;
  line-height: inherit;
}

.wiz-editor-body.markdown-body p {
  margin: 0.7em 0;
}

/* 标题 */
.wiz-editor-body.markdown-body h1,
.wiz-editor-body.markdown-body h2,
.wiz-editor-body.markdown-body h3 {
  color: #23476b;
  line-height: 1.4;
  margin-top: 1.5em;
  margin-bottom: 0.65em;
}

.wiz-editor-body.markdown-body h1 {
  font-size: 1.8em;
  padding-bottom: 0.3em;
  border-bottom: 1px solid #dce5ee;
}

.wiz-editor-body.markdown-body h2 { font-size: 1.4em; }
.wiz-editor-body.markdown-body h3 { font-size: 1.15em; }

/* 链接、图片和表格 */
.wiz-editor-body.markdown-body a { color: #2762a0; }
.wiz-editor-body.markdown-body img { max-width: 100%; height: auto; }
.wiz-editor-body.markdown-body table { border-collapse: collapse; }
.wiz-editor-body.markdown-body th,
.wiz-editor-body.markdown-body td {
  border: 1px solid #dce5ee;
  padding: 0.5em 0.75em;
}
.wiz-editor-body.markdown-body th { background: #f3f6fa; }
```

常改的几项：`17px` 是正文字号；`1.75` 是行距；`920px` 是最大阅读宽度；`24px 28px` 是上下／左右留白。`#263950` 这样的十六进制值表示颜色。

不要对所有元素统一字体或强改 MathJax 内部结构，否则上下标、积分号等可能变形。正文样式不是完整的 App 深色主题。

## 4. 两个模板按钮怎么选

“添加模板：引用块”只美化普通 `>` 引用块，所有引用块采用统一外观，不负责区分定理和定义。

“添加模板：数学环境（定理／定义／证明）”会识别引用块首行的数学标题，为不同类型设置颜色，并启用数学块编号和引用。你要做数学笔记，建议优先用这一套。

操作方法：

1. 点击“添加模板：数学环境（定理／定义／证明）”。
2. 点击“预览到当前笔记”或“保存”，才会真正应用。
3. 在笔记里按下一节写定义、定理和证明，再切到阅读预览。

同一个模板可以多次点击，识别标记未被删除时不会重复插入。两套可以同时用；建议先引用块模板，再数学环境模板，最后追加你自己的覆盖规则。

请保留数学环境模板开头这行：

```css
/* WizNotePlus scientific math environments v1 */
```

它不仅是说明文字，也是程序识别“启用数学块处理”的开关。只复制一段颜色规则、没有这个标记，不会自动开启定理识别和编号。

## 5. 定义、定理、证明怎么写

以下是笔记里的 Markdown，不是填写到 CSS 设置里的内容。可以整段复制到一篇新的 Markdown 笔记中试用：

```markdown
# 一个数学笔记示例

> **定义（偶数）** \label{def:even}
>
> 若存在整数 $k$，使 $n=2k$，则称整数 $n$ 为偶数。

> **定理（偶数的平方）** \label{thm:even-square}
>
> 偶数的平方仍是偶数。

> **证明**
>
> 由定义，令 $n=2k$，则 $n^2=4k^2=2(2k^2)$，因此结论成立。

由定义 \ref{def:even}，可得到定理 \ref{thm:even-square}。

> **例（一个简单例子）** \label{ex:two}
>
> $2^2=4$，二者都是偶数。
```

注意标题单独作为引用块的第一段，加粗写出“定义”“定理”等名称。`\label{...}` 放在加粗标题之后；正文中的引用推荐直接写 `\ref{...}`，不要包在 `$...$` 中。

支持的类型包括定理、引理、命题、推论、定义、例／例子、注／注记、证明，也支持相应英文标题。

## 6. 编号与 label 的关系

`\label{def:even}` 是给这个定义起一个固定引用名，不是手动指定编号。前面增加或删除数学块后，显示编号和对应的 `\ref` 会一起变化。

- 定理、引理、命题、推论共用一套编号。
- 定义单独编号，例子也单独编号。
- 注记和证明不自动编号。
- 每篇笔记从头计数，不跨笔记接续。
- 同一篇笔记的引用名必须唯一；建议用 `def:`、`thm:`、`ex:` 等前缀区分。
- 名称限 1–128 个英文字母、数字或 `_ : . / -`，不要用中文或空格。
- 重复名称会提示并取消链接；找不到的引用保留原文。
- 暂不支持跨笔记 label 引用。

如果想手写编号：

```markdown
> **定理 7（手动指定编号）** \label{thm:manual}
>
> 这里写正文。
```

它会保留数字 7，同一组后面的自动编号从 8 继续。写“定理 2.3”可以原样显示，但不会自动实现“按章节编号”。不要仅靠 CSS 计数器修改程序的编号逻辑，否则显示编号可能与你期待的引用规则不同。

公式仍使用原来的 MathJax：行内 `$...$`，独立公式 `$$...$$`。公式自己的 `\label`／`\eqref` 和定理块不是同一套编号表，标签名要分开。定理块引用放在普通正文里最稳妥；完整 `.tex` 文档编译和跨笔记引用不在本版范围内。

## 7. 调整定理、定义的颜色与间距

先插入数学环境模板，然后将以下代码追加到所有模板的最后：

```css
/* 定理：蓝色；定义：绿色；例子：橙色 */
.wiz-editor-body.markdown-body {
  --math-blue: #35599b;
  --math-blue-border: #5b7fc2;
  --math-blue-background: #f2f6fd;

  --math-green: #287659;
  --math-green-border: #4a9b78;
  --math-green-background: #f0f8f3;

  --math-orange: #9c5b24;
  --math-orange-border: #c48c52;
  --math-orange-background: #fff7ed;

  --math-environment-radius: 8px;
  --math-environment-padding: 1em 1.15em;
  --math-environment-margin: 1.25em 0;
}

/* 定理和定义的正文行距 */
.wiz-editor-body.markdown-body .theorem,
.wiz-editor-body.markdown-body .definition {
  line-height: 1.8;
}

/* 证明结束符；默认也是空心方块 */
.wiz-editor-body.markdown-body .proof::after {
  content: "□";
  text-align: right;
  color: #526579;
}
```

颜色、背景、边框、圆角和间距由 CSS 控制；识别类型、编号和建立引用由程序处理。可以调整外观，不需要改程序。但自定义新的语义、自动章节编号或跨笔记引用，不能仅靠改颜色的 CSS 实现。

## 8. 保存、恢复与备份 CSS

- “预览到当前笔记”：临时试效果，不等于保存。
- “保存”：保留设置，之后其他笔记／重开程序沿用。
- “取消”：撤销此次未保存的预览与修改。
- 取消勾选“启用自定义 CSS”再保存：保留编辑框内容，但停用自定义样式。
- “恢复默认”：清空自定义 CSS 并预览默认效果；只有再点“保存”才持久化，点“取消”则恢复原设置。

调整前，将整个编辑框复制到一个本地 `.css` 文件，例如 `我的数学笔记样式.css`。换电脑时复制这个文件，在新电脑的 CSS 设置中粘贴并保存。不需要共用或覆盖整个资料库。

CSS 最多 65536 个字符。只使用可信来源；`@import`、远程字体和 `url(...)` 图片可能联网，不建议把不熟悉的网站脚本或 Notebooks 专用 JavaScript 粘进来。

## 9. 导出 PDF

继续使用原来的“另存为 PDF”。同一份 CSS 参与笔记显示和旧 PDF 导出，不另设打印 CSS，也没有新的打印预览流程。

屏幕和纸张宽度不同，分页、长公式与表格可能有所变化，不能保证逐像素一致。定理／定义引用在 PDF 中可能跳到正确页但上下位置有偏移，这是保留的已知限制；本说明不宣称已修复。

## 10. 没有效果时检查什么

1. 是否勾选了“启用自定义 CSS”，并点击了预览或保存？
2. 是否在 Markdown 阅读预览，而不是源码输入区？
3. CSS 框里是否误粘了 `<style>`、Markdown 代码围栏或 JavaScript？
4. 选择器是否正确？先用第 1 节的段落样式确认，不只改 `body`。
5. 后面是否有另一条同样或更精确的规则覆盖了前面的设置？把自己的覆盖规则放到最后。
6. 数学环境模板的识别标记是否还在？标题是否是引用块第一段的加粗标题？
7. label 是否唯一、没有中文或空格，引用名是否完全一致？
8. 另一台 Mac 上是否也保存了同一份 CSS？它不会随账号自动同步。

如果显示异常，先停用自定义 CSS 并保存。正文和 Markdown 源码不会因此删除，可以再从自己的 `.css` 备份恢复。

## 11. 其他日常使用提醒

使用系统“应用程序”中的 WizNotePlus Apple Silicon，可继续登录原来的为知账号。新版资料目录为 `~/Library/Application Support/WizNotePlus-AppleSilicon/`，原版通常为 `~/.wiznote/`；不要让新旧程序共用一个本地目录，也不要同时运行同账号副本。

14.4.15 移除个人自用版的旧创建／管理团队网页入口；已有群组笔记和同步不因此删除，不代表已把旧团队管理网页修好。原始笔记／文件夹交换包和通用 HTML 导出用途不同，详见 [导入导出说明](https://github.com/Defenceforce1/WizNotePlus/blob/compat/macos-arm64-qt6/docs/native-note-transfer.md)。

英文拼写可在偏好设置中启用，不自动改词，也不是数学／TeX 语法检查。程序仍为免费本地签名、未公证的独立适配版；改名、剪藏器更新和新功能不会在这次说明中悄悄改变。
