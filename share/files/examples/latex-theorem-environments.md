# LaTeX 风格定理环境示例

见命题 \ref{prop:square} 和推论 \ref{cor:sum}。

\begin{definition}[实数]
\label{def:real}
实数集记为 $\mathbb{R}$。
\end{definition}

\begin{lemma}
实数的平方非负。
\end{lemma}

\begin{proposition}[平方非负]
\label{prop:square}
对于 $x\in\mathbb{R}$，有

\begin{equation}
x^2\ge 0\label{eq:square}
\end{equation}

\begin{proof}
由实数乘法与序的相容性可得。
\end{proof}
\end{proposition}

\begin{corollary}
\label{cor:sum}
对于实数 $x,y$，有 $x^2+y^2\ge 0$。
\end{corollary}

\begin{theorem*}[不编号的补充]
星号环境不占用自动编号。
\end{theorem*}

\begin{example}
取 $x=2$，则 $x^2=4$。
\end{example}

\begin{remark}
正文仍可使用 **Markdown 加粗**、列表和代码；公式引用为 \eqref{eq:square}。
\end{remark}
