# 独立展示公式示例

将以下原文复制到一篇 Markdown 笔记中，再进入阅读状态。展示环境独立成块，不需要外层美元定界符。

\begin{equation}
E=mc^2\label{eq:energy}
\end{equation}

\begin{align}
a_1&=b_1+c_1\label{eq:first-row}\\
a_2&=b_2+c_2+\eqref{eq:energy}\label{eq:second-row}
\end{align}

原有美元公式写法继续支持。代码围栏中的示例保持代码，不作为数学环境处理。

```latex
\begin{equation}
this_is_only_a_code_example
\end{equation}
```
