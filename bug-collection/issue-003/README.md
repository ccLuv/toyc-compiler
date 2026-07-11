\# Issue #3：函数调用超过 8 个参数时栈上传参错误



\## Bug 类型



源码逻辑缺陷，开发阶段：前期。



\## 问题说明



开发过程中发现，当函数参数数量超过 8 个时，编译器没有正确处理 RISC-V 调用约定中的栈上传参，导致第 9 个及之后的参数读取错误，函数返回值不正确。



\## 最小复现



对应测试文件：



\- `tests/many\_args.tc`

\- `tests/many\_args.expected`



核心代码：



```c

int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {

&#x20; return a + b + c + d + e + f + g + h + i + j;

}



int main() {

&#x20; return sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);

}

