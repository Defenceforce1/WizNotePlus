# 数学解析状态重建：实际浏览器关联回归

`mathjax-parser-renewal-browser.js` 必须仅注入已就绪的合成Markdown页面：实际WizEditor公开API、当前自有助手、MathJax4.1.3/TeX已加载。禁止在真实笔记执行；它会替换合成正文、切换读编并注入一次性加载失败。

等待 `window.WizReview411Qualification.done`（历史探针名称），要求 `failures===0`、`association.isCandidate===true`、89项检查完成，且 `weakAuditAfter30s` 的engine/input/document各存活1份、两种样式节点各1份。无需、不得强制GC。16个正常状态还须与正式406控制比较MathML/字形/几何。

夹具覆盖删除宏/内建宏恢复/编号/定理引用/align和分式、CSS文字/属性/重复ID/错误标签、重建抛错或拒绝、完整初始化失败与重试、取消/迟到回调和原文保持。弱引用仅跟踪公开engine、startup.input数组、startup.document，不证明私有对象全部退役或长期无泄漏。

本地原生注入/运行和实际保存/磁盘档案重开、双PDF的可复核入口及结果见 `outputs/exploration-20261110/phase412`。这些额外关联门槛不能以本JS独立通过替代。若重建入口不存在，旧基准只执行66项正常控制，不能误判为当前候选验收通过。

独立运行时须新建项目内合成资料根，核对当前锁定组件、完整Core/helper路径和指纹。保持正常顶层事件循环；关页不能在JS回调内同步等待保存。正式414已纳入新资源；历史406控制仍为旧资源，不把不同资源混称同一产物。
