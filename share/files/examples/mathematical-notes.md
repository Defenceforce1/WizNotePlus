# 数学笔记示例

先在“笔记样式（CSS）”中添加“数学环境（定理／定义／证明）”模板并保存，再切换到阅读预览。这里仍使用普通 Markdown 引用块，不需要宏。

可以向后引用：见定义 \ref{def:real}、定理 \ref{thm:positive}、引理 \ref{lem:sum}。

## 定义、定理和证明

> **定义（实数）** \label{def:real}
>
> 实数集记为 $\mathbb{R}$。

> **定义（平方）** \label{def:square}
>
> 对 $x\in\mathbb{R}$，记 $x^2=x\cdot x$。

> **定理（平方非负）** \label{thm:positive}
>
> 对任意实数 $x$，有 $x^2\ge 0$。
>
> $$x^2 \ge 0$$

> **证明**
>
> 若 $x\ge 0$，则 $x\cdot x\ge 0$；若 $x<0$，则 $x^2=(-x)^2>0$。

> **引理（平方和非负）** \label{lem:sum}
>
> 对任意 $x,y\in\mathbb{R}$，有 $x^2+y^2\ge 0$。
>
> 这是定理 \ref{thm:positive} 的直接结果。

> **例（取具体数值）** \label{ex:numbers}
>
> 取 $x=3$、$y=4$，则 $x^2+y^2=25$。

> **注（编号方式）**
>
> 定理与引理共用序列；定义、例子各自编号。证明和注不编号。

由定义 \ref{def:square} 和引理 \ref{lem:sum}，可以继续推导。

## 手写编号

> **定理 7（保留手写编号）** \label{thm:manual}
>
> 本条显示为定理 7，不被自动编号覆盖。

> **推论（继续自动编号）** \label{cor:next}
>
> 本条接着显示为推论 8。

手写编号见定理 \ref{thm:manual}；下一条见推论 \ref{cor:next}。

## 普通公式、表格和引用块

行内公式：$a^2+b^2=c^2$。多行公式仍沿用原来的公式引擎：

$$
\begin{aligned}
(x+y)^2 &= x^2+2xy+y^2 \\
(x-y)^2 &= x^2-2xy+y^2.
\end{aligned}
$$

| 写法 | 含义 |
| --- | --- |
| `\label{thm:positive}` | 给数学块一个固定引用名，不是编号数字 |
| `\ref{thm:positive}` | 在正文显示对应编号 |
| `**定理 7（名称）**` | 保留手写的数字 7 |

> **定理相关问题**
>
> 这是普通引用块，不会因为包含“定理”二字而被自动编号。

代码中的命令也保持原样，不会变成可点击引用：

```text
\ref{thm:positive}
> **定理** \label{code:untouched}
```

PDF 仍使用原来的“另存为 PDF”。定义、定理引用在 PDF 中的跳转位置可能稍有偏移，这是本版已知问题。
