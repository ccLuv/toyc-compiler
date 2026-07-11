\# Issue #4：尾递归优化后返回值被错误清零



\## Bug 类型



源码逻辑缺陷，开发阶段：中期。



\## 问题说明



开发过程中发现，在优化模式下进行尾递归转换时，如果直接覆盖函数形参，可能导致后续参数计算使用到已经被更新的旧形参，最终返回值错误。



\## 最小复现



对应测试文件：



\- `tests/tail\_recursion.tc`

\- `tests/tail\_recursion.expected`



核心代码：



```c

int sum\_to(int n, int acc) {

&#x20; if (n == 0) return acc;

&#x20; return sum\_to(n - 1, acc + n);

}



int main() {

&#x20; return sum\_to(50, 0) % 256;

}

