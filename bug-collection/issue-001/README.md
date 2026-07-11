\# Issue #1：短路逻辑未阻止右侧表达式副作用



\## Bug 类型



源码逻辑缺陷，开发阶段：前期。



\## 问题说明



开发过程中发现，逻辑与 `\&\&` 和逻辑或 `||` 如果按普通二元表达式处理，会错误计算右侧表达式，导致本应被短路跳过的函数调用仍然执行。



\## 最小复现



对应测试文件：



\- `tests/short\_circuit.tc`

\- `tests/short\_circuit.expected`



核心代码：



```c

int touched = 0;



int mark() {

&#x20; touched = touched + 1;

&#x20; return 1;

}



int main() {

&#x20; int a = 0 \&\& mark();

&#x20; int b = 1 || mark();

&#x20; if (a || !b) return 99;

&#x20; return touched;

}

