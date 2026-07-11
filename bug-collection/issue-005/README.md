\# Issue #5：只读全局变量跨函数读取时传播不稳定



\## Bug 类型



源码逻辑缺陷，开发阶段：后期。



\## 问题说明



开发过程中发现，全局变量如果初始化后没有再被写入，理论上可以作为只读全局变量参与常量传播。但旧逻辑对跨函数读取场景分析不稳定，可能无法正确识别全局变量是否被写入，影响优化结果和性能表现。



\## 最小复现



对应测试文件：



\- `tests/readonly\_global.tc`

\- `tests/readonly\_global.expected`



核心代码：



```c

int factor = 17;



int calculate(int x) {

&#x20; return x \* factor + factor;

}



int main() {

&#x20; return calculate(10);

}

