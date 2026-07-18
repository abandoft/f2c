# F2C

[English](https://github.com/abandoft/f2c) · [中文文档](https://github.com/abandoft/f2c/blob/main/README-ZH.md)

现代化、高性能的 Fortran 到 C17 转译器，目标是生成可移植的商业级 C 代码。使用 C17 编写，不依赖额外的生成代码运行时。

## 主要特性

- 自动识别自由格式和固定格式源码，并提供显式 CLI 选项。
- 类型化表达式与语句 AST，携带 kind、rank、shape 和值类别信息。
- 不依赖外部运行时的 C17 输出，面向桌面、服务器、移动端和 WebAssembly 工具链。
- 严格跨平台 CI、sanitizer、确定性输出检查、模糊测试、BLAS/LAPACK 差分验证、性能门禁。

## 环境要求与构建

项目要求 CMake 3.20 或更高版本，以及支持 C17 的编译器。完整数值差分测试还需要 Python 3
和 gfortran。

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DF2C_BUILD_TESTING=ON \
  -DF2C_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

所有生成文件和本地构建产物必须位于根目录的 `build/` 下。项目有意不提供 CMake 安装规则。

## 命令行用法

转译单个源文件并编译生成的 C：

```sh
build/f2c input.f90 -o output.c
cc -std=c17 -O3 output.c -lm -o output
```

将多个文件作为同一项目转译并生成共享 C 接口：

```sh
build/f2c caller.f90 implementation.f90 -o project.c --header project.h
```

管道调用可使用 `-` 作为输入或输出路径，例如
`build/f2c - --free-form -o - < input.f90`。文件产物会先全部暂存再替换；任一 C 或头文件无法写入时，
已有输出保持不变。

源码形式默认自动识别：`.f`、`.for` 和 `.ftn` 使用固定格式，`.f90` 等现代扩展名使用自由格式。
文件名无法表达真实物理布局时，可使用 `--free-form` 或 `--fixed-form` 覆盖自动识别；
`--comments` 会在生成的 C 中保留源码行注释。

条件预处理采用显式、大小写敏感的宏环境。可通过 `-DNAME` 或 `-DNAME=EXPR` 定义名称，
并用 `-UNAME` 移除之前的命令行定义，例如：

```sh
build/f2c -I include -DUSE_ISNAN=1 source.F90 -o output.c
```

内建契约支持对象式和函数式 `#define` 正文展开、`#undef`、`#if`、`#ifdef`、`#ifndef`、`#elif`、
`#else`、`#endif`、`#include`、`#line`、数字行标记及标准 Fortran `INCLUDE`。函数式宏支持零参数、
嵌套参数、可变参数、字符串化、token 粘贴和反斜杠续接的预处理指令逻辑行；include operand
也可以由宏展开产生。整数条件采用确定性的 64 位有符号/无符号值，支持字符常量、正常优先级和
短路求值。宏诊断同时保留展开位置与定义拼写位置；不兼容重定义、畸形调用和递归调用均为硬错误。
转译器不会隐式猜测项目或平台特性宏。API/CLI 定义保持对象式并对项目内每个输入生效，源码内部
定义严格限制在当前输入内。CLI 使用 `-I` 配置 include 目录；引号 include 会先搜索当前源码所在目录。

完整 CLI 说明见 `build/f2c --help`。

## 库接口

公共头文件 [`include/f2c/f2c.h`](include/f2c/f2c.h) 提供内存内转译接口：

```c
#include <f2c/f2c.h>

F2cOptions options = {"input.f90", F2C_SOURCE_AUTO, 0};
F2cResult result = f2c_transpile(source, source_length, &options);

if (result.error_count == 0U) {
    /* result.code 包含自包含的 C17 实现。 */
}

f2c_result_free(&result);
```

使用 `F2cInput[]` 和 `f2c_transpile_project` 可在共享过程注册表下转译多个源文件。
`result.header` 包含该项目定义的外部过程声明。发生硬错误时，`code` 和 `header` 均为 `NULL`，
`diagnostics` 提供可操作的文件、行和列信息。

长期运行的服务可设置请求级资源预算，不会引入进程全局状态：

```c
F2cConfig config = {0};
config.structure_size = sizeof(config);
config.limits.max_input_bytes = 64U * 1024U * 1024U;
config.limits.max_output_bytes = 128U * 1024U * 1024U;

F2cPreprocessorDefinition definitions[] = {{"USE_ISNAN", "1"}};
config.preprocessor_definitions = definitions;
config.preprocessor_definition_count = 1U;

/* 可选：通过回调提供 include 源码，使核心库不绑定文件系统。 */
config.include_resolver = resolve_include;
config.include_release = release_include;
config.include_user_data = resolver_context;

F2cResult result = f2c_transpile_project_config(inputs, input_count, &config);
```

限制字段为零时使用对应的 `F2C_DEFAULT_*` 默认值。预算覆盖项目输入及保留的预处理总字节数、
条件宏数量、宏参数数量、宏/include 深度、include 数量、逻辑行、canonical token、表达式 AST
节点与深度、常量求值工作量、诊断数量与字节数，以及分别生成的代码和头文件；超限时不会返回
不完整的 C 代码。include 解析是请求级回调，因此核心库不依赖文件系统。还可通过同步
`F2cDiagnosticCallback` 获取结构化诊断分类、严重级别、展开/拼写源码范围和消息，无需解析文本
诊断。`structure_size` 必须等于 `sizeof(F2cConfig)`。项目尚未冻结公共 ABI，因此旧版或更大的配置
布局都会被拒绝，当前结构的全部字段均属于唯一有效的 API。

## 支持状态

当前经过测试的实现包括：

- 统一化的自由/固定源码形式、续行、标签、有界对象宏与条件预处理、回调式 include、行重映射、
  程序单元、内部过程、模块、宿主关联与 `USE` 关联；
- 内建数值、逻辑、字符和复数类型，显式/隐式类型，类型化表达式、数组构造器、数组段、向量下标、
  归约及选定的 transformational intrinsic；
- 显式、抽象、泛型和过程指针接口，以及受支持 ABI 路径上的位置、关键字、可选和过程实参；
- 可分配/指针对象、描述符、自动重分配、`SOURCE=`、`MOLD=`、`MOVE_ALLOC`、派生组件、内建赋值
  和所有权清理；
- 派生类型、继承、动态类型标签、`SELECT TYPE`、类型绑定分派、精确 rank 的 `FINAL` 过程，
  以及已支持控制流路径上的构造作用域终结；
- 长度感知的字符赋值与比较、子串、数组、延迟长度、函数结果及验证语料使用的 gfortran 兼容
  尾随长度 ABI；
- 结构化/旧式控制流、格式化和列表导向 I/O、内部文件、非前进 I/O、定义 I/O，以及文档化路径上的
  递归 NAMELIST；
- 测试覆盖的数值、字符和派生类型组合中的 `RESHAPE`、`PACK`、`UNPACK`、`SPREAD`、
  `CSHIFT`、`EOSHIFT` 和 `FINDLOC` 降级。

重要剩余工作包括：声明/模块的完整 token 流覆盖，所有 kind/rank 与任意数组表达式组合，完整模块
泛型和子模块，动态多态分配，命名/关联构造的终结边界，全部格式化 I/O 布局规则，NAMELIST 输入
中的指针重关联，以及多编译器 ABI 认证。不支持的语义必须产生诊断，不能生成看似合理但错误的 C。
详细清单维护在 [TODO.md](TODO.md)。

## 验证体系

固定 Reference LAPACK 语料目前提供以下自动化门禁：

- 155/155 个 Reference BLAS 和 3,535/3,535 个 BLAS/LAPACK/INSTALL/TESTING 源文件完成转译，
  生成代码按严格 C17 无警告编译；
- 生成 BLAS/LAPACK 静态库执行端到端 `DGESV` 求解；
- 四项 INSTALL 检查和 52,512 项 S/D/C/Z RFP 检查与原生 Fortran 匹配；
- 官方 BLAS Level 1/2/3、完整 S/D/C/Z LIN 及全部 80 套 EIG 输入与同一固定原生构建差分；
- 100 个数值驱动、5,807,798 条并集记录的内部逐项审计产物；
- 71 项生成 C/原生 Fortran 性能矩阵，每项比值上限为 1.05。

内部审计会有意保存有限舍入差异、未配对内部记录和 NaN 策略差异。门禁通过表示没有新增生成端
官方阈值回归或覆盖回归，不表示生成 C 与原生 Fortran 逐位相等。

代表性的本地门禁包括：

```sh
sh test/reference_blas_compile.sh build/f2c
sh test/reference_lapack_core_compile.sh build/f2c
sh test/reference_blas_tests.sh build/f2c
sh test/reference_lapack_lin.sh build/f2c
sh test/reference_lapack_eig.sh build/f2c
sh test/reference_blas_exhaustive.sh build/f2c
sh test/reference_lapack_exhaustive.sh build/f2c
sh test/reference_performance_matrix.sh build/f2c
```

CI 职责与触发策略见 [`.github/workflows/README.md`](.github/workflows/README.md)。

## 项目结构

```text
include/f2c/   公共嵌入接口
src/cli/       命令行适配层
src/core/      公共 API 实现与流水线编排
src/frontend/  源码归一化、程序单元、声明和接口
src/semantic/  类型、常量、intrinsic、字符和过程语义
src/ast/       表达式/语句 AST、词法器、解析器和访问器
src/codegen/   C17 降级、数组、I/O、所有权、类型和程序单元
src/internal/  私有跨模块数据结构与接口
test/          单元、执行、差分、模糊和性能测试
```

## 参与贡献

欢迎能够改进可移植 C17 生成和可审计 Fortran 语义的聚焦变更。请保持以下项目约束：

- 使用项目名称 `f2c`，所有生成产物均放在 `build/` 下；
- 不修改归档源码或项目范围说明；
- 生成 C 只能依赖 ISO C17、libc 和 libm，不得增加独立运行时；
- 扩展类型化 AST/语句 IR，不重新解析生成文本；
- 对不支持的语义给出诊断，不生成近似实现；
- 保留工作区中的无关修改，并使用仓库 `.clang-format` 格式化 C 代码。

解析、语义、ABI、所有权、控制流、I/O 或代码生成的变更都应增加执行回归。提交前运行上述严格
CMake/CTest 命令；较大的转译器变更还应运行受影响的 BLAS/LAPACK 门禁。性能敏感变更必须在
空闲机器上运行 `test/reference_performance_matrix.sh`。

## 安全策略

当前默认分支和最新标签版本接收安全修复。不要在公开 issue、discussion 或 pull request 中披露
疑似漏洞，请使用 GitHub 私有漏洞报告，并尽可能提供：

- 受影响的版本或提交、目标平台和 C 编译器；
- 能复现问题的最小 Fortran 输入；
- 可以安全共享的生成 C；
- 影响、sanitizer 诊断及资源使用观测。

生成代码内存安全、解析器资源耗尽、路径处理、发布来源证明，以及意外访问请求输入之外的内容，
均按安全问题处理。维护者会通过私有报告确认、复现、评估并协调修复和披露。

标签发布产物包含 SHA-256 校验和及 GitHub artifact attestation：

```sh
sha256sum -c f2c-<version>-<platform>.<archive>.sha256
gh attestation verify f2c-<version>-<platform>.<archive> --repo abandoft/f2c
```

macOS 可使用 `shasum -a 256 -c` 校验相同的 checksum 文件。

## 许可证

- [f2c](https://github.com/abandoft/f2c) 的代码使用 [MIT 许可证](LICENSE)。
- [netlib-f2c](https://www.netlib.org/f2c) 的代码遵循原项目许可证。
> **注：**`f2c`是重构的新项目，并不依赖`netlib-f2c`。
