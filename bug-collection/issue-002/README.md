\# Issue #2：嵌套循环 break/continue 跳转目标错误



\## Bug 类型



源码逻辑缺陷，开发阶段：中期。



\## 问题说明



开发过程中发现，在嵌套 while 循环中，如果内层循环同时包含 `continue` 和 `break`，编译器生成的跳转标签可能指向错误位置，导致循环执行次数异常。



\## 最小复现



对应测试文件：



\- `tests/nested\_loops.tc`

\- `tests/nested\_loops.expected`



核心代码：



```c

int main() {

&#x20; int i = 0;

&#x20; int result = 0;

&#x20; while (i < 4) {

&#x20;   int j = 0;

&#x20;   i = i + 1;

&#x20;   while (j < 5) {

&#x20;     j = j + 1;

&#x20;     if (j == 2) continue;

&#x20;     if (i == 3) break;

&#x20;     result = result + 1;

&#x20;   }

&#x20; }

&#x20; return result;

}

