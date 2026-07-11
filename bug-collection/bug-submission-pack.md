# ToyC 编译器 Bug 收集提交材料草稿

本文档根据教师发布的《Bug 收集仓库操作规范》整理。下面 5 条均按“Issue -> 修复分支 -> 失败测试 -> 修复代码 -> PR -> 合入 -> 归档记录”的格式准备，便于复制到 GitHub Issue 和 PR 中。

注意：Issue 编号、PR 编号需要以 GitHub 实际创建后的编号为准。本文档先使用 #1 - #5 作为建议编号。

## 总体操作清单

1. 确认 GitHub 仓库为 Private。
2. 在 `Settings -> Collaborators` 中添加本组成员、`give-to`、`ILsoMachine`，并给予读写权限。
3. 按本文档创建 5 个 GitHub Issue。
4. 每个 Issue 单独创建一个修复分支，分支名见对应条目。
5. 每个 Bug 至少保留两类证据：测试文件、修复文件。
6. 每个 Bug 通过 PR 合入，PR 描述中写明修复前失败和修复后通过。
7. 合入后更新 `commits-log.xlsx`。

## Issue #1：短路逻辑错误导致右侧表达式副作用被执行

建议标题：`【前期 - 源码逻辑 Bug】短路逻辑未阻止右侧表达式副作用`

建议分支：`fix/issue-001-short-circuit-side-effect`

测试类型：集成测试

测试文件：`tests/short_circuit.tc`、`tests/short_circuit.expected`

修复文件：`src/main.cpp`

关键修复位置：表达式降低阶段的 `&&`、`||` 短路控制流生成。

### Issue 内容

```markdown
## 【Bug分类】源码逻辑缺陷｜开发阶段：前期

## 关联信息（PR合并后补充）
1. Issue编号：#1
2. 预计修复分支：fix/issue-001-short-circuit-side-effect

## 基础环境
1. 编译环境：Ubuntu/WSL，C++20，Makefile 构建；运行环境为 riscv64-linux-gnu-as、riscv64-linux-gnu-ld、qemu-riscv32。

## 测试类型
集成测试

## 最小化 buggy 方法
```c
int touched = 0;

int mark() {
  touched = touched + 1;
  return 1;
}

int main() {
  int a = 0 && mark();
  int b = 1 || mark();
  if (a || !b) return 99;
  return touched;
}
```

## 自动化测试断言
`tests/short_circuit.expected` 中期望程序退出码为 `0`。如果右侧表达式被错误求值，`touched` 会变为 `2`，退出码不为 `0`。

## 复现步骤
1. 执行构建命令：`make`
2. 执行测试命令：`bash tests/run_tests.sh`
3. 观察 `short_circuit` 或 `short_circuit_opt` 测试。修复前右侧 `mark()` 被错误执行，实际返回 `2`；期望返回 `0`。

## 观察到的错误行为
逻辑与 `0 && mark()` 和逻辑或 `1 || mark()` 没有正确短路，导致本不应执行的函数调用修改了全局变量。

## 预期正确行为
`&&` 左侧为假时不计算右侧，`||` 左侧为真时不计算右侧，最终 `touched` 应保持为 `0`。
```

### PR 描述

```markdown
关联 Issue：Fixes #1

修复内容：在 IR 降低阶段将 `&&` 和 `||` 转换为显式条件跳转，避免直接按普通二元表达式计算左右两侧。

测试覆盖：`tests/short_circuit.tc` 覆盖短路逻辑右侧带全局副作用的情况。

验证结果：
- 修复前：`mark()` 被错误执行，程序返回 `2`
- 修复后：`short_circuit` 与 `short_circuit_opt` 均返回 `0`

影响范围：仅影响逻辑与、逻辑或表达式降低，不改变普通算术表达式和关系表达式处理。

主要 Commit：
- [Issue #1] add failing test for short-circuit side effect
- [Issue #1] lower logical operators with control flow
```

## Issue #2：嵌套循环中 break/continue 目标标签错误

建议标题：`【中期 - 源码逻辑 Bug】嵌套循环 break/continue 跳转目标错误`

建议分支：`fix/issue-002-nested-loop-labels`

测试类型：集成测试

测试文件：`tests/nested_loops.tc`、`tests/nested_loops.expected`

修复文件：`src/main.cpp`

关键修复位置：循环上下文栈、`break`/`continue` 降低逻辑。

### Issue 内容

```markdown
## 【Bug分类】源码逻辑缺陷｜开发阶段：中期

## 关联信息（PR合并后补充）
1. Issue编号：#2
2. 预计修复分支：fix/issue-002-nested-loop-labels

## 基础环境
1. 编译环境：Ubuntu/WSL，C++20，Makefile 构建；运行环境为 riscv64-linux-gnu-as、riscv64-linux-gnu-ld、qemu-riscv32。

## 测试类型
集成测试

## 最小化 buggy 方法
```c
int main() {
  int i = 0;
  int result = 0;
  while (i < 4) {
    int j = 0;
    i = i + 1;
    while (j < 5) {
      j = j + 1;
      if (j == 2) continue;
      if (i == 3) break;
      result = result + 1;
    }
  }
  return result;
}
```

## 自动化测试断言
`tests/nested_loops.expected` 中期望退出码为 `12`。

## 复现步骤
1. 执行构建命令：`make`
2. 执行测试命令：`bash tests/run_tests.sh`
3. 观察 `nested_loops_opt`。开发过程中曾出现期望 `12`、实际 `11` 的错误结果。

## 观察到的错误行为
嵌套 while 中的 `continue` 或 `break` 跳转到了错误标签，使内层循环计数少执行一次，最终结果变为 `11`。

## 预期正确行为
内层循环中的 `continue` 只跳转到当前内层循环条件位置，`break` 只跳出当前内层循环，最终返回 `12`。
```

### PR 描述

```markdown
关联 Issue：Fixes #2

修复内容：为 while 降低阶段维护循环上下文栈，进入循环时压入当前 break/continue 标签，退出循环时弹出，确保嵌套循环跳转目标正确。

测试覆盖：`tests/nested_loops.tc` 覆盖嵌套循环、内层 continue、内层 break 的组合场景。

验证结果：
- 修复前：`nested_loops_opt` 返回 `11`
- 修复后：`nested_loops` 与 `nested_loops_opt` 均返回 `12`

影响范围：仅影响 while、break、continue 的控制流生成。

主要 Commit：
- [Issue #2] add failing test for nested loop control flow
- [Issue #2] fix loop context stack for break and continue
```

## Issue #3：超过 8 个参数的函数调用栈传参错误

建议标题：`【前期 - 源码逻辑 Bug】函数调用超过 8 个参数时栈上传参错误`

建议分支：`fix/issue-003-stack-arguments`

测试类型：集成测试

测试文件：`tests/many_args.tc`、`tests/many_args.expected`

修复文件：`src/main.cpp`

关键修复位置：RISC-V 后端函数调用参数传递、函数入口参数装载。

### Issue 内容

```markdown
## 【Bug分类】源码逻辑缺陷｜开发阶段：前期

## 关联信息（PR合并后补充）
1. Issue编号：#3
2. 预计修复分支：fix/issue-003-stack-arguments

## 基础环境
1. 编译环境：Ubuntu/WSL，C++20，Makefile 构建；运行环境为 riscv64-linux-gnu-as、riscv64-linux-gnu-ld、qemu-riscv32。

## 测试类型
集成测试

## 最小化 buggy 方法
```c
int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
  return a + b + c + d + e + f + g + h + i + j;
}

int main() {
  return sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
}
```

## 自动化测试断言
`tests/many_args.expected` 中期望退出码为 `55`。

## 复现步骤
1. 执行构建命令：`make`
2. 执行测试命令：`bash tests/run_tests.sh`
3. 观察 `many_args`。修复前第 9、10 个参数不能正确从栈上传递/读取，返回值不等于 `55`。

## 观察到的错误行为
编译器只正确处理了 `a0-a7` 中的前 8 个参数，超过 8 个的参数在调用端或被调端栈偏移处理错误。

## 预期正确行为
前 8 个参数使用 `a0-a7`，第 9 个及之后的参数通过栈传递，被调函数能正确读取，最终返回 `55`。
```

### PR 描述

```markdown
关联 Issue：Fixes #3

修复内容：补全 RISC-V 调用约定中超过 8 个参数的栈上传递和函数入口读取逻辑。

测试覆盖：`tests/many_args.tc` 覆盖 10 参数函数调用。

验证结果：
- 修复前：返回值不等于 `55`
- 修复后：`many_args` 与 `many_args_opt` 均返回 `55`

影响范围：函数调用后端生成和函数参数读取。

主要 Commit：
- [Issue #3] add failing test for stack arguments
- [Issue #3] fix stack argument passing for calls
```

## Issue #4：尾递归优化后返回值错误

建议标题：`【中期 - 源码逻辑 Bug】尾递归优化后返回值被错误清零`

建议分支：`fix/issue-004-tail-recursion-result`

测试类型：集成测试

测试文件：`tests/tail_recursion.tc`、`tests/tail_recursion.expected`

修复文件：`src/main.cpp`

关键修复位置：尾递归识别、参数更新顺序、优化模式 IR 生成。

### Issue 内容

```markdown
## 【Bug分类】源码逻辑缺陷｜开发阶段：中期

## 关联信息（PR合并后补充）
1. Issue编号：#4
2. 预计修复分支：fix/issue-004-tail-recursion-result

## 基础环境
1. 编译环境：Ubuntu/WSL，C++20，Makefile 构建；运行环境为 riscv64-linux-gnu-as、riscv64-linux-gnu-ld、qemu-riscv32。

## 测试类型
集成测试

## 最小化 buggy 方法
```c
int sum_to(int n, int acc) {
  if (n == 0) return acc;
  return sum_to(n - 1, acc + n);
}

int main() {
  return sum_to(50, 0) % 256;
}
```

## 自动化测试断言
`tests/tail_recursion.expected` 中期望退出码为 `251`。

## 复现步骤
1. 执行构建命令：`make`
2. 执行测试命令：`bash tests/run_tests.sh`
3. 观察 `tail_recursion_opt`。开发过程中曾出现期望 `251`、实际 `0` 的错误。

## 观察到的错误行为
优化模式下尾递归转换没有正确保留新参数值，导致最终累加结果错误，程序返回 `0`。

## 预期正确行为
尾递归应被转换为等价循环，参数 `n` 和 `acc` 按调用参数同步更新，最终返回 `1275 % 256 = 251`。
```

### PR 描述

```markdown
关联 Issue：Fixes #4

修复内容：修正尾递归转换中的参数更新顺序，先计算所有新实参到临时值，再统一写回形参并跳转到函数入口。

测试覆盖：`tests/tail_recursion.tc` 覆盖自尾递归累加。

验证结果：
- 修复前：`tail_recursion_opt` 返回 `0`
- 修复后：`tail_recursion` 与 `tail_recursion_opt` 均返回 `251`

影响范围：仅影响自尾递归调用优化。

主要 Commit：
- [Issue #4] add failing test for tail recursion result
- [Issue #4] fix tail recursion argument update order
```

## Issue #5：只读全局变量传播不稳定

建议标题：`【后期 - 源码逻辑 Bug】只读全局变量跨函数读取时传播不稳定`

建议分支：`fix/issue-005-readonly-global-propagation`

测试类型：集成测试

测试文件：`tests/readonly_global.tc`、`tests/readonly_global.expected`

修复文件：`src/main.cpp`

关键修复位置：全局变量写入分析、只读全局变量传播、函数内全局读取优化。

### Issue 内容

```markdown
## 【Bug分类】源码逻辑缺陷｜开发阶段：后期

## 关联信息（PR合并后补充）
1. Issue编号：#5
2. 预计修复分支：fix/issue-005-readonly-global-propagation

## 基础环境
1. 编译环境：Ubuntu/WSL，C++20，Makefile 构建；运行环境为 riscv64-linux-gnu-as、riscv64-linux-gnu-ld、qemu-riscv32。

## 测试类型
集成测试

## 最小化 buggy 方法
```c
int factor = 17;

int calculate(int x) {
  return x * factor + factor;
}

int main() {
  return calculate(10);
}
```

## 自动化测试断言
`tests/readonly_global.expected` 中期望退出码为 `187`。

## 复现步骤
1. 执行构建命令：`make`
2. 执行测试命令：`bash tests/run_tests.sh`
3. 观察 `readonly_global` 与 `readonly_global_opt`。修复前只读全局分析不完整时，可能把跨函数全局读取处理为普通内存访问或错误传播，导致优化模式结果或性能异常。

## 观察到的错误行为
全局变量 `factor` 初始化后没有被写入，应可安全视为只读全局；旧逻辑没有稳定识别跨函数只读全局读取，影响生成代码和优化结果。

## 预期正确行为
编译器应识别 `factor` 未被写入，在优化模式下把函数内读取传播为常量 `17`，最终返回 `10 * 17 + 17 = 187`。
```

### PR 描述

```markdown
关联 Issue：Fixes #5

修复内容：增加全局变量写入统计，只有从未被 StoreGlobal 写入的全局变量才参与只读传播；函数内 LoadGlobal 在安全条件下替换为常量。

测试覆盖：`tests/readonly_global.tc` 覆盖跨函数只读全局读取。

验证结果：
- 修复前：只读全局变量传播不稳定，优化模式可能结果异常或性能异常
- 修复后：`readonly_global` 与 `readonly_global_opt` 均返回 `187`

影响范围：只读全局分析和 LoadGlobal 优化，不影响会被写入的全局变量。

主要 Commit：
- [Issue #5] add failing test for readonly global propagation
- [Issue #5] fix readonly global propagation analysis
```

