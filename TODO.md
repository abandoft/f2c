# F2C 实现状态与路线图

本文档是当前实现的审计结果和后续工作的唯一任务清单，不记录逐次开发历史。开发历史应写入
`CHANGELOG.md`；设计理由应写入相应模块文档或代码注释。

## 状态规则

- `[x]`：实现、自动化测试和对应 CI 门禁均已存在，并在当前审计中得到验证。
- `[ ]`：尚未完成，或仅有部分实现。部分完成不能标记为 `[x]`。
- `P0`：达到“商业级 Fortran 90 及更早版本转译器”之前必须完成。
- `P1`：达到稳定公共产品、长期维护和多平台交付之前必须完成。
- `P2`：Fortran 2003 及以后标准扩展，不阻塞 F90 核心闭环，但已经支持的子集也必须满足同样的
  正确性和质量要求。

每个任务只有同时满足以下条件才能关闭：

1. 实现不依赖 `libf2c` 或其他新增运行时，只生成依赖 libc/libm 的 C17。
2. 正向、负向、边界和内存失败测试齐全；诊断不得静默忽略语义。
3. 生成代码通过严格 C17 编译，并与原生 Fortran 执行结果进行适当粒度的差分。
4. 相关测试进入 CI；涉及性能的改动还必须通过固定统计方法的性能门禁。
5. 不修改或引用 `netlib-f2c`；所有中间产物只写入根目录的 `build/`；不增加 CMake 安装规则。

## 当前已验证基线

- [x] 项目使用 C17 和 CMake，提供 `f2c_core` 库及 `f2c` CLI，生成代码无需链接 `libf2c`。
- [x] 源码按 `cli`、`core`、`frontend`、`ast`、`semantic`、`codegen` 等职责分目录。
- [x] Linux、macOS、Windows 的静态/共享 Debug/Release 构建与测试已经进入 CI。
- [x] ASan/UBSan、libFuzzer、生成结果复现、WebAssembly 构建、BLAS/LAPACK 数值验证、性能和
  发布已经拆分为独立工作流。
- [x] 当前本地严格 AppleClang 静态 Debug、静态 Release、共享 Release 与 ASan/UBSan Debug
  构建基线已经建立；当前默认严格 CTest 为 45/45，架构边界检查作为独立测试运行。
- [x] 固定 Reference LAPACK 3.12.1 提交
  `6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca`；3,535 个 Fortran 文件和 155 个 BLAS 文件
  已有严格 C17 编译门禁。
- [x] 固定官方 LIN、EIG、RFP、BLAS 驱动及统一数值审计已经进入 CI；当前记录中生成端新增
  官方阈值失败为 0。原生 Fortran 基线在特定平台出现阈值失败时会保留稳定的失败数不回归记录，
  不再静默丢失对应测试族并导致汇总覆盖数量随平台变化。Linux 与 Darwin 的原生编译器输出差异
  使用逐字段精确的独立覆盖 profile 验证；生成端套件数和记录数在所有 profile 中必须完全一致，
  未登记或混合 profile 仍会使门禁失败。
- [x] 71 项生成 C 与原生 Fortran 性能矩阵已经进入 CI，当前规则为任何单项不得慢于 5%。

以上内容是“当前已通过的基线”，不是完整 Fortran 标准支持或商业化完成声明。

## P0：编译器核心正确性

### P0-FE-01 统一源码映射和词法流

- [x] 自由格式、固定格式、续行和分号拆分现在使用紧凑的连续区间源码映射；token、表达式 AST、
  语句 AST 及结构化词法诊断均保留原始文件的物理起止行列。自由/固定续行、分号拆句及续行声明
  诊断已有回归，映射不会按输入字符分配独立位置对象。
- [ ] 旧式标号 `DO` 已删除源码文本改写和合成 token 路径，终止标签、循环控制、共享终止语句及
  嵌套动作均直接从 canonical token 构建 AST，并保留标签和表达式的精确物理 span。预处理对象宏
  已经保留 expansion/spelling 双范围和重映射源码名，文本与结构化诊断均可同时呈现；仍需让其余
  parser/semantic 诊断全部直接消费 token/AST span，并增加完整宏/include 栈 related location，才能
  满足“任意诊断均有精确范围”的验收标准。过程设计符、关键字实参、`MOVE_ALLOC` 和规格表达式
  诊断现已直接使用 AST 的起止范围；同名过程/关键字回归会验证诊断不会退回到源码字符串中的首个
  文本匹配。
- [ ] 让声明、程序单元头、`USE`、`NAMELIST`、旧式语句和 I/O 全部消费统一 token 流，删除
  `f2c_identifier`、`f2c_split_*`、`f2c_starts_word`、括号/引号手工扫描等生产解析路径。表达式和
  语句入口现已接受预先生成的 canonical token 流；程序单元、模块/接口/派生类型边界、过程引用、
  `USE`、`PROCEDURE`、`NAMELIST`、`COMMON` 及 `EQUIVALENCE` 的外层结构和实体设计子已经迁移；
  类型声明的 type/kind/length 选择器、属性、实体、维度和初始化，以及程序单元头和 `IMPLICIT`
  映射/符号发现均消费 canonical token，并把规格表达式范围保存到语义模型。`EQUIVALENCE` 下标
  直接构建并常量求值 token AST；属性关键字会结合顶层赋值 token 判别，`DIMENSION`、`EXTERNAL`、
  `PARAMETER`、`SAVE` 和 `EQUIVALENCE` 作为合法变量名时不再被误判为声明。`DATA` 的组、重复因子、
  设计子和嵌套隐式 DO 已完全消费 canonical token range；`READ/WRITE`、文件控制语句及其控制项和
  I/O item、`PRINT` 的格式与输出项以及带标签的 `FORMAT` 语句均已消费 canonical token range；
  计数/无控制/`WHILE`/标号 `DO`、单行/块/算术 `IF`、直接/计算/赋值 `GOTO`、`ASSIGN` 和带标签
  嵌套动作也已迁移。`CALL/MOVE_ALLOC`、`ALLOCATE/DEALLOCATE/NULLIFY`、`STOP/ERROR STOP`、
  `RETURN`、普通赋值和指针赋值现同样直接从 canonical token range 构建 AST；关键字实参、分配
  类型说明、停止码及赋值两端均保留精确物理 span。`f2c_identifier`、全部 `f2c_split_*`、
  `f2c_starts_word`、整数文本求值器和表达式文本查询包装器已经从生产代码删除；架构测试禁止在
  parser 之外重新调用原始文本表达式入口，也禁止恢复这些旧解析器。`PROGRAM`、`SUBROUTINE`、
  `FUNCTION`、`MODULE` 和 `BLOCK DATA` 头、前缀、哑实参、`RESULT`、
  `PROCEDURE(interface)` 声明及程序单元终止语句现均先建立结构化语法 AST，再降级到语义模型；
  开闭类型与名称会在生成前校验，续行 token 和错误 token 的精确物理 span 已有回归，架构门禁
  禁止恢复旧式头部和终止谓词。`USE`
  语句现也由 canonical token 建立独立语法 AST，完整区分 nature、模块名、空 `ONLY`、重命名及
  泛型设计符，并保存每个关联项的精确范围；统一候选判定保证名为 `use` 的变量、数组和组件不会被
  当成声明，架构门禁禁止生产代码恢复关键字文本猜测。该任务仍因部分声明/属性和其他旧式规格
  语句中的手写扫描尚未全部迁移而保持未关闭。
  源码归一化层的注释识别、分号拆句和代码大小写处理已经
  改为消费 canonical token，不会在 token 化之前破坏字符或 Hollerith 载荷。
- [x] 只保留 `frontend/token.h` 定义的 `F2cToken/F2cTokenStream`。表达式 AST 已删除独立的
  `AstTokenKind/AstToken`，`ast/token_stream.c` 只负责表达式上下文的 token 约束，不再实现第二套
  词法规则；预分词表达式和语句的源码 span 已由单元测试覆盖。公共的有界 token cursor、源码
  range 和混合定界符匹配已经建立，且不存在固定 64 层嵌套上限。
- [x] 建立显式 `source → syntax program → typed program → C emitter` 编排入口；编译上下文、程序
  单元和语句分别记录阶段，emitter 会拒绝未经过语义验证的 unit 或 statement IR。
- [x] 字符字面量、Hollerith、BOZ、kind 前缀/后缀、无空格点运算符和固定格式列边界已有正向、
  负向及端到端测试矩阵；自由格式字符续行和固定格式跨卡片字符/Hollerith 载荷保持原字节，宏
  展开不会侵入续行载荷。非法 BOZ 会按基数验证数字并定位具体错误字符。LF、CRLF、UTF-8 BOM、
  无末尾换行和嵌入 NUL 均有 API/预处理回归；生成端与 gfortran 字面量差分及严格 C17 执行已进入
  数值 CI。本轮固定 Reference LAPACK 的 155 个 BLAS 和 3,535 个 LAPACK 源文件继续全部严格编译，
  DGESV、INSTALL、52,512 条 RFP、146 个 BLAS 结果组与 262,388 次 BLAS 调用、200 个 LIN 结果组
  及四种精度 80 套 EIG 输入均通过生成 C/原生 Fortran 差分。
- [x] 已将面向 LAPACK 的简化预处理逻辑替换为明确、有界的
  `#define/#if/#include/#line` 预处理契约；未承诺的指令会产生带源码位置的硬错误。
  固定 16 层嵌套数组和 `USE_ISNAN` 特例判断已经删除；独立预处理模块现使用动态条件栈、
  大小写敏感的输入局部定义表、带正常优先级和短路规则的整数条件表达式，并通过 API/CLI 的
  `F2cPreprocessorDefinition` 与 `-D/-U` 显式接收初始环境。对象宏递归正文展开、`#include`、标准
  Fortran `INCLUDE`、`#line`/数字行标记、API resolver/release、CLI `-I`、跨 include 宏环境、循环与
  深度/数量/总字节预算，以及 expansion/spelling 双范围均已有独立正负向测试。源码内函数式宏
  支持零参数、嵌套和可变实参、`#` 字符串化、`##` token 粘贴、参数数量预算和不兼容重定义诊断；
  预处理指令支持反斜杠续接逻辑行，宏展开可生成 include operand。`#if` 采用确定性的 64 位
  有符号/无符号整数模型，覆盖标准整数后缀、字符与转义常量、短路、溢出和非法运算诊断。以上
  契约均有正向、负向、资源边界、严格编译和 sanitizer 回归。

验收标准：所有解析阶段只共享一种 token 定义；任意诊断均具有文件、起止行列和稳定错误码；
续行、宏展开和固定格式样本的定位由快照测试验证。

### P0-AST-01 完整类型化 AST 与规格表达式

- [ ] 为所有表达式节点完整确定基础类型、kind、逐维 shape、动态 extent、可定义性、值类别、
  所有权和多态动态类型；不能确定时必须保存显式约束，而不是退回字符串推断。
- [ ] 完成标量、数组、数组段、向量下标、数组构造器、隐式 DO、子串、组件引用和函数结果的
  统一形状传播、合形检查与别名分析。`ANY/ALL/COUNT` 现在可以直接归约同 type/kind 的数值、
  LOGICAL、COMPLEX 和 CHARACTER 数组关系，支持动态元素数、数组段、标量广播、扁平构造器及
  无 `PAD/ORDER` 的 `RESHAPE` 视图；运行时会验证动态合形。CHARACTER 数组表达式和派生类型
  仿射数组段现可按元素长度或深拷贝所有权快照；返回派生类型的用户 ELEMENTAL 函数结果会在
  快照后销毁逐元素返回临时值。不同数值 kind 的通用隐式提升、除 `TRANSPOSE` 外的嵌套
  transformational 结果、带 `DIM/MASK` 的归约和其他非 designator 派生类型数组表达式仍需纳入
  统一临时量引擎。
- [ ] 将参数、kind、字符长度、数组边界和初始化中的规格表达式全部纳入溢出安全的常量求值器，
  补齐标准允许的 inquiry/specification intrinsic。`SIZE/SHAPE/LBOUND/UBOUND` 现已建立 typed
  rank/shape/kind、关键字关联、常量 `DIM/KIND` 约束及溢出安全的 C17 降级。数值模型 inquiry
  `DIGITS/EPSILON/HUGE/KIND/MAXEXPONENT/MINEXPONENT/PRECISION/RADIX/RANGE/TINY` 以及
  `SELECTED_INT_KIND/SELECTED_REAL_KIND` 已进入 typed IR 和同一常量求值器，可用于参数和类型
  kind 选择器；查询实参不会被求值。实数表示 intrinsic
  `EXPONENT/FRACTION/NEAREST/RRSPACING/SCALE/SET_EXPONENT/SPACING` 已进入 typed IR 和常量
  求值器，覆盖 binary32/binary64、零、signed zero、subnormal、非有限值、关键字关联和支持的整数
  指数 kind，并生成精确十六进制静态常量。仍需补齐其余规格 intrinsic 和所有允许出现位置。字符常量
  求值现已覆盖 `ACHAR/ADJUSTL/ADJUSTR/CHAR/IACHAR`、
  `ICHAR/INDEX/LEN/LEN_TRIM/REPEAT/SCAN/TRIM/VERIFY`、连接、参数引用、嵌入 NUL、空串、反向搜索
  和结果 kind 范围，并可直接生成 CHARACTER 声明初始化器。
- [x] 移除 `f2c_emit_cached_expression` 和 `f2c_translate_expression` 原始文本降级路径；模块实体、
  派生类型组件、声明初始化、字符长度、数组边界、语句函数结果和模块常量均在语义阶段建立表达式
  AST。除表达式 parser 自身及其测试入口声明外，全部生产代码均不得调用原始文本表达式解析器；
  `architecture_boundaries` 同时禁止恢复已经删除的文本拆分、标识符和整数求值包装器。
- [ ] 保证 Fortran 未指定求值顺序不会被错误固化，具有副作用的实参和下标只求值一次；数组
  重叠、函数结果和临时对象具有可证明的生命周期。
- [x] 语句函数已建立真实的类型化定义 AST；直接和嵌套调用使用每次调用独立的 C17 实参临时值，
  不再通过字符串宏式替换重复求值。带副作用外部函数实参的严格编译、执行回归会验证只调用一次。

验收标准：生产代码生成路径不存在表达式重新解析；不同 kind/rank、动态 shape、向量下标和
重叠赋值均有 gfortran 差分及 sanitizer 回归。

### P0-SEM-01 类型、kind 和 ABI

- [ ] 建立目标 ABI 数据模型，覆盖所承诺的 INTEGER、REAL、COMPLEX、LOGICAL 和 CHARACTER
  kind，不再把 kind 仅当作附加整数元数据。当前已集中定义 `INTEGER(KIND=1/2/4/8)`、IEEE
  binary32 `REAL(KIND=4)` 和 binary64 `REAL(KIND=8)` 的 radix、digits、precision、range、指数
  边界及极值，并在生成 C 中使用静态断言拒绝不匹配的目标模型；其他 REAL、LOGICAL、
  CHARACTER kind 及完整目标 ABI 仍需完成。
- [ ] 完整区分显式形状、假定大小、假定形状和延迟形状，统一标量、数组、字符、可分配及指针
  哑实参和函数结果的描述符契约。假定形状哑实参现在通过签名元数据和数组描述符传递逐维 extent，
  保留哑实参声明下界，并支持固定形状/可分配整数组实参、内部子程序和函数表达式；DT I/O 与
  rank-specific FINAL 调用已同步该 ABI。描述符现保存逐维有符号 stride，具名数组以及由标量下标
  和 triplet 组成的任意 rank 仿射数组段可用于语句调用和函数表达式，并可跨假定形状过程继续传递；
  transformational intrinsic 读取假定形状实参时也会按 descriptor stride 一次性快照。正/负非单位
  步长均有严格 C17 执行与 gfortran 差分。transformational intrinsic 已能逐元素读取向量下标并
  一次性物化；普通 `CALL` 语句现也会为向量下标、数组构造器、嵌套 `TRANSPOSE`、用户
  `ELEMENTAL` 结果和其他可元素化数组表达式生成连续临时量及描述符，覆盖数值、CHARACTER 和
  含可分配组件的派生类型，并按 `INTENT` 管理复制和所有权。假定长度 CHARACTER 数组从描述符
  绑定元素长度；向量下标用于 `INTENT(OUT/INOUT)` 会在语义阶段硬失败。函数表达式中的同类过程
  实参、局部指针的非连续关联、假定大小最后一维及 FINAL 的完整逐维 shape 仍未统一，故本任务
  保持未关闭。
- [ ] 把字符长度、逐维 shape、`VALUE`、`OPTIONAL`、`INTENT`、别名限制和嵌套过程签名纳入
  项目级接口兼容检查。
- [x] 过程接口参数类型、kind、rank、intent、可选性、动态属性及嵌套过程元数据均使用原子扩容的
  动态存储；语义绑定不再截断到 64 项，70 个哑实参的端到端生成与严格 C17 编译已经进入测试。
- [ ] 对公开生成 ABI 建立版本化规范，并用 C 调用者、C++ 调用者和多个 Fortran 编译器做互操作
  测试；不得依赖某一个 gfortran 版本的未记录实现细节。

### P0-SEM-02 程序单元、声明和模块

- [ ] 将声明语法解析与符号/语义分析彻底分离，统一检查重复或冲突的类型、属性、初始化、
  `SAVE`、`PARAMETER`、`TARGET`、`POINTER`、`ALLOCATABLE`、`EXTERNAL` 和 `INTRINSIC`。当前
  `declaration/type.c`、`entity.c`、`dimension.c` 已承担 token 语法解析，重复属性、重复 shape、
  冲突类型和非法指针初始化会硬失败；`PROCEDURE(interface)` 的接口、属性和实体列表也已使用独立
  语法 AST，并在降级阶段执行接口绑定和属性语义检查。仍需把所有属性组合和跨声明一致性集中到
  独立语义阶段。
- [ ] 补齐 `PROGRAM`、`MODULE`、`BLOCK DATA`、`SUBROUTINE`、`FUNCTION`、`ENTRY`、内部过程
  和任意层宿主关联；验证 `RECURSIVE`、`PURE`、`ELEMENTAL` 等过程属性。现有程序单元与模块的
  头部、名称、过程前缀、哑实参、函数结果及终止语句已保存精确范围，并验证开闭类型和名称；模块
  扫描不会再把内部过程终止语句误当作模块终止。具名、匿名和旧式连写 `BLOCK DATA` 已作为独立
  非过程程序单元进入统一发现、AST、语义和终止匹配流程，不会污染过程注册表或生成伪调用入口；
  其可执行语句和非法嵌套会在生成前硬失败。用户定义
  `ELEMENTAL` 函数和子程序现已在 typed IR 上保存具体过程绑定，并检查标量哑实参、`INTENT`、
  禁止的 `ALLOCATABLE/POINTER` 属性、标量函数结果、显式接口属性一致性以及数组实参逐维合形；
  数组实际参数会把 rank、动态 extent 和 shape 传播到函数结果，标量实参按元素广播。代码生成
  使用统一 elementizer 处理全数组、任意 rank 数组段、关键字实参、模块/内部过程、单行及块
  `WHERE` 和 elemental 子程序调用；数值、LOGICAL、CHARACTER、派生类型、零大小、重叠赋值及
  可分配目标均先完整求值到临时量，再按 Fortran 所有权规则提交。数组构造器实参复用通用临时量
  引擎，覆盖嵌套构造值和隐式 DO。独立负向语义测试、严格 C17 执行夹具及 gfortran 逐行差分已
  加入数值验证 CI；本任务仍因其他程序单元和过程属性组合未完成而保持未关闭。
- [ ] 完成显式/隐式接口、关键字与可选实参、过程实参、交替返回、字符隐藏长度和存储序列关联。
- [x] `DATA` 已建立 canonical token → 语句 AST → typed IR → emitter 的完整路径；语义阶段展开重复
  因子和任意嵌套隐式 DO，验证初始化常量、类型/kind、精确元素数、常量下标、边界、重复初始化与
  资源预算。局部标量、整数组和部分/隐式 DO 数组元素可直接生成 C17 静态初始化器；CHARACTER、
  COMPLEX 及不能表示为 C 常量表达式的合法值使用过程入口的一次性初始化区。`SAVE` 生命周期会沿
  `EQUIVALENCE` 传播到实际根存储。模块规格部分现也建立完整语句 typed IR，模块 `DATA` 的数值、
  逻辑、复数和 CHARACTER 标量/数组会直接进入模块静态存储；动态目标、无法静态表示的值及模块
  可执行语句均在生成前硬失败。重复调用执行回归、严格 C17、原生 Fortran 差分和固定
  BLAS/LAPACK 全量差分均已覆盖。
- [ ] 完成命名及空白 `COMMON`、`EQUIVALENCE`、`DATA` 和 `SAVE` 的布局、初始化顺序、重叠与
  跨程序单元一致性。命名及空白 `COMMON` 已使用统一语法路径，并按每个程序单元计算确定的字节
  偏移、对齐和总 extent；同一全局联合存储为每个声明单元生成独立类型化视图及 `offsetof/sizeof`
  编译期断言。数组与标量重分组、等宽 `INTEGER/REAL` 异类型视图、空白块不同长度和命名块总 extent
  冲突已有严格语义及原生差分覆盖，固定长度 CHARACTER 不再向关联存储写入伪 NUL 字节。
  `EQUIVALENCE` 现先保存 canonical 设计子约束，待 type/kind/shape 完成后统一求解连通组的字节
  偏移；支持跨语句传递关联、异类型标量、移位数组、复数/实数和 CHARACTER 数组覆盖，冲突、溢出
  及 C17 无法安全表示的未对齐视图会在生成前硬失败。局部覆盖存储使用按职责拆分的类型化联合生成器，
  `DATA/SAVE` 生命周期仍传播到整组。`BLOCK DATA` 可将命名 `COMMON` 中的数值、逻辑、复数及
  CHARACTER 标量/数组直接写入 C17 静态全局初始化；非法初始化所有权和重复所有者均为硬错误。
  `COMMON` 与 `EQUIVALENCE` 现共享同一项目级字节布局：命名及空白块均可从锚定成员向块尾扩展，
  每个关联实体拥有带偏移和总大小静态断言的全局类型化视图；跨两个块的关联、相互矛盾的锚点、
  向块首地址之前扩展、偏移溢出和不可表示的对齐均会在生成前硬失败。跨程序单元重分组及块尾扩展
  已加入严格 C17、ASan/UBSan 和原生 Fortran 运行差分，并进入数值验证 CI。`BLOCK DATA` 现可通过
  单一 `EQUIVALENCE` 类型化视图静态初始化命名 `COMMON`，包括从原块尾继续扩展的整数组；初始化
  所有权仍按块检查，多个重叠联合视图不会生成含糊的 C 初始化器。仍需统一同一块中多个视图的
  非重叠初始化、可移植的未对齐访问、零大小存储实体、派生类型存储序列和真正重叠值的一致性判定，
  因此本项保持未关闭。
- [ ] 完成模块 `PUBLIC/PRIVATE`、`ONLY`、重命名、泛型接口、运算符/赋值泛型和模块过程。普通
  模块关联现从结构化 `USE` AST 降级，支持空 `ONLY`、实体重命名、远端名称隐藏，以及变量、常量、
  外部过程完整签名和派生类型的本地别名；别名冲突、重复本地名称和非法关联具有精确诊断。项目内
  模块会按依赖图稳定拓扑排序后分析和生成，依赖环在模块名称处硬失败；接口体拥有独立作用域，不会
  再把其 `USE`、隐式规则或派生类型泄漏到宿主模块。模块级 `PUBLIC/PRIVATE`、声明属性和泛型设计符
  已建立 canonical token → 访问语法 AST → 语义模型路径；默认/逐实体可见性会过滤无 `ONLY` 导入，
  显式导入私有变量、类型或过程在远端名称处硬失败，宿主关联仍可访问本模块私有成员。导入实体可按
  本地可见性重导出，同一最终实体经直接及包装模块的多路径关联会合并，不同最终实体的同名关联会
  冲突；生成端只由提供模块定义存储，包装模块不会重复定义。上述规则已有精确诊断、严格生成 C17、
  原生 gfortran 差分和架构门禁。`LA_CONSTANTS` 已支持完整导入、重命名及提供者存储所有权，但仍是
  硬编码特例。`INTERFACE`/`END INTERFACE` 的名称、`OPERATOR`、`ASSIGNMENT` 和定义 I/O
  generic-spec 已建立结构化语法 AST，校验开闭设计符并保存精确范围；统一的
  `[MODULE] PROCEDURE [::] specific-list` AST 支持普通及模块过程列表。命名泛型保存完整类型化
  候选集，并按实参 type/kind/rank、关键字和过程类别选择唯一候选；无匹配、歧义、重复、未定义
  具体过程和非法 specific 契约均在精确 token 处硬失败。普通 `PROCEDURE` 列表当前可绑定项目内
  可见的具体外部过程及模块所含过程，尚未覆盖过程指针、过程哑实参和无法在项目中解析的外部目标。
  定义一元/二元运算符及内在运算符扩展现按标准优先级进入 typed IR，选择后保留真正的具体过程、
  结果 type/kind/rank/shape 和 ELEMENTAL 形状；定义赋值可选择二实参子程序，并支持标量和
  ELEMENTAL 数组降级。特殊泛型设计符支持 `PUBLIC/PRIVATE`、`USE ONLY`、导入及包装模块重导出。
  生成端始终调用真正的提供者具体过程；结构化语法、严格 C17、ASan/UBSan、非法/歧义诊断及
  原生 gfortran 差分均已覆盖。子模块、普通 `PROCEDURE` 的其余目标类别、作用域泛型定义 I/O 与
  现有 DT I/O 的统一，以及 `LA_CONSTANTS` 通用化仍未实现，因此本项保持未关闭。
- [ ] 对缺失的非 intrinsic 模块建立明确策略；不能把无法解析的 `USE` 静默当成普通外部过程。

验收标准：以标准小程序覆盖每类程序单元和关联规则；跨文件接口不一致必须在生成 C 之前报告；
Reference LAPACK 继续全量严格编译且源码中不再存在模块名称硬编码。

### P0-SEM-03 Intrinsic 注册与降级

- [ ] 用单一声明式注册表描述每个 intrinsic 的标准版本、泛型候选、参数关联、kind/rank/shape
  规则、常量折叠和 C17 降级，删除语义分析与代码生成之间的重复分派。
- [ ] 完成 F90 全部 intrinsic 及项目承诺的旧式 intrinsic；重点补齐位操作、字符处理、数值模型、
  kind 选择、数组 inquiry 和随机数语义。数组 inquiry 子集已覆盖非默认下界、零 extent 的标准
  `LBOUND=1/UBOUND=0`、动态 `DIM`、`KIND=1/2/4/8`、切片/构造器/elemental 数组表达式、可分配
  结果和假定形状哑实参；独立负向语义测试、严格 C17 执行及 gfortran 差分已进入 CI。位操作
  `BIT_SIZE/BTEST/IAND/IBCLR/IBITS/IBSET/IEOR/IOR/ISHFT/ISHFTC/NOT/MVBITS` 已覆盖
  `INTEGER(KIND=1/2/4/8)`、关键字参数、常量折叠、elemental 数组、符号位及完整位宽边界；
  `MVBITS` 对标量别名、重叠数组段和标量广播使用写入前快照。严格 C17、UBSan 及 gfortran
  逐项差分已进入 CI。字符 intrinsic `ACHAR/ADJUSTL/ADJUSTR/CHAR/IACHAR/ICHAR/INDEX/LEN`、
  `LEN_TRIM/REPEAT/SCAN/TRIM/VERIFY` 现已覆盖类型化参数关联、结果 kind/长度、常量折叠、关键字
  `BACK/KIND`、标量与 elemental 数组、空串及嵌入 NUL；生成端使用无符号字节、溢出检查和有界
  临时存储，严格 C17、UBSan 及 gfortran 字节级差分已进入 CI。数值模型 intrinsic
  `DIGITS/EPSILON/HUGE/KIND/MAXEXPONENT/MINEXPONENT/PRECISION/RADIX/RANGE/TINY` 和
  `SELECTED_INT_KIND/SELECTED_REAL_KIND` 已覆盖类型与 kind 契约、关键字关联、失败码、常量折叠、
  声明选择器和动态实参一次求值；生成端仅依赖固定宽度整数及 `float.h` 常量，查询实参不求值，
  严格 C17、UBSan 与 gfortran 差分已进入 CI。实数表示 intrinsic
  `EXPONENT/FRACTION/NEAREST/RRSPACING/SCALE/SET_EXPONENT/SPACING` 已覆盖类型化参数关联、
  elemental rank/shape、binary32/binary64 结果 kind、常量折叠和边界语义；运行时降级仅使用 libc/libm，
  对超出 C `int` 范围的整数指数执行显式保护，并由严格 C17、ASan/UBSan 与原生 Fortran 逐值差分
  验证。`RANDOM_NUMBER` 与 `RANDOM_SEED` 已按 intrinsic subroutine 建模，覆盖 REAL 标量、任意 rank
  数组及非连续段、`SIZE/PUT/GET`、无参重置、种子状态往返和线程局部状态；数组实参使用统一
  描述符临时量及写回路径，严格 C17、ASan/UBSan 与原生 Fortran 属性差分已进入数值验证 CI。
  显式 `EXTERNAL` 的同名过程优先于内建函数。其他 F90 intrinsic 尚未全部完成，因此本项保持未关闭。
- [ ] 让 `RESHAPE/PACK/UNPACK/SPREAD/CSHIFT/EOSHIFT/TRANSPOSE/MATMUL` 等支持任意合法数组
  表达式、所有已支持 kind/rank、零大小数组和非默认下界，而不是只接受具名整数组。上述 intrinsic
  的数值、LOGICAL、COMPLEX 和 CHARACTER 输入现共用列主序数组视图与一次性临时量引擎；
  派生类型仿射数组段使用逐元素深拷贝及逆序销毁。MASK、SHIFT、BOUNDARY、PAD、SHAPE 和 ORDER
  数组表达式也只求值一次。`TRANSPOSE` 支持任意可元素化 rank-2 输入，`MATMUL` 覆盖矩阵×矩阵、
  矩阵×向量、向量×矩阵、混合数值 kind 提升及 LOGICAL/COMPLEX 运算，并在语义阶段拒绝非标准
  的向量×向量和静态不合形内维。非连续数组段经假定形状过程转发、可分配及零大小结果、严格
  C17 执行和 gfortran 差分均已覆盖；嵌套 `TRANSPOSE` 及用户 ELEMENTAL 派生类型结果也已进入
  同一临时量流程。数值、CHARACTER 和含可分配组件的派生类型向量下标均已通过严格执行与差分，
  派生类型结果使用深拷贝快照。仍需完成其他嵌套 transformational 结果、更多非 designator
  派生类型表达式，以及全部 CHARACTER/派生类型可选参数组合，故本任务未关闭。
- [ ] 对后续标准已经实现的 `FINDLOC` 等 intrinsic 逐项声明版本，并补齐字符、派生类型定义相等、
  `DIM/MASK/BACK/KIND` 的全部合法组合。
- [ ] 为每个 intrinsic 建立表驱动正向/负向组合测试，并与至少两个原生 Fortran 编译器差分。

### P0-STMT-01 语句 AST 和控制流

- [x] 删除 `F2C_STMT_UNSUPPORTED` 及其注释式输出路径；无法识别的语句现在以结构化
  `F2C_DIAGNOSTIC_UNSUPPORTED` 硬失败且不生成 C，emitter 对缺失生成器的 typed IR 只报告内部
  错误。typed 语句表达式无法生成时也会产生硬诊断并抑制全部 C 输出，禁止再用常量 `0` 静默
  代替；对应负向回归同时验证这一契约。正向生成、负向诊断和架构边界测试均已进入现有 CI。
- [ ] 完成 F90 `WHERE/ELSEWHERE`、所有合法 `DO` 形式、单行/块 IF 和旧式控制语句。命名
  `DO`、`IF`、`SELECT CASE`、`SELECT TYPE` 和 `BLOCK` 已进入统一 token → 语句 AST → typed IR
  流程；分支及结束名称、活动名称唯一性、`CYCLE/EXIT` 的命名 DO 目标都由构造绑定阶段验证。
  跨层 `CYCLE/EXIT` 使用稳定 IR 编号的 C17 标签，并在跳转前执行作用域清理；正向严格编译、
  负向语义矩阵及 gfortran 执行差分均已进入测试和数值验证 CI。
  `SELECT CASE` 已使用独立 typed IR 支持标量 INTEGER、默认 kind CHARACTER 和 LOGICAL 选择器，
  支持值列表、开区间/闭区间及任意位置的 DEFAULT；选择器只求值一次，初始化表达式、类型、重复
  DEFAULT、LOGICAL 范围和可静态判定的重叠范围均在生成前验证。构造绑定阶段会记录每个 CASE、
  TYPE/CLASS guard 和终止语句的直接所有者，区分 END IF/END DO，并拒绝错配、缺失、嵌套位置错误
  及首个 CASE 之前的可执行语句。严格 C17 执行测试和 gfortran 输出差分已进入 CI；非默认
  CHARACTER kind 仍需随完整 kind/ABI 数据模型一并实现。
  `WHERE/ELSEWHERE` 已使用独立语句 IR 和构造绑定，支持单行、块、命名、masked/default branch
  及嵌套形式；掩码在进入分支时快照，masked assignment 先快照全部选中右值再写回，覆盖数值、
  LOGICAL、CHARACTER 和派生类型数组。逐维静态/运行时合形、零大小与乘法溢出检查、动态数组段、
  任意 rank 的 Fortran 元素顺序、重叠数组段及 `::` triplet token 均已有回归。掩码、数组段边界
  和赋值中的非平凡标量子表达式会在元素循环外物化；数值、逻辑、CHARACTER 与派生类型数组构造器
  均通过通用动态 shape/value 临时量先完整求值再按掩码选择，覆盖嵌套构造值、隐式 DO、整数组值、
  动态字符长度以及派生对象 clone/finalization。具体派生函数结果类型也会从 unit header 传播到结果
  符号、过程签名和调用表达式。严格 C17 生成执行夹具和 gfortran 差分已加入数值验证 CI；该任务仍
  因其他语句及完整控制流生命周期分析未完成而保持未关闭。F90 的计数、无控制和 `WHILE` DO，
  以及旧式单一/共享终止标签、非 `CONTINUE` 合法终止动作和带标签 `END DO` 已建立显式 AST/typed
  IR 所有者；终止动作会在正确循环层级执行，循环后控制变量语义与原生 Fortran 差分一致。
- [x] 标签、直接/计算/赋值 `GOTO`、算术 IF、旧式标号 DO 及 I/O `ERR/END/EOR` 已进入显式
  语句标签图。语义阶段统一规范化前导零，拒绝重复/未定义/非可执行目标、从外部进入结构化构造，
  以及 IF/SELECT CASE/SELECT TYPE/WHERE 兄弟分支块之间的非法跳转；单一/共享 DO 终止标签在
  typed IR 中按内到外顺序绑定。每个直接、计算、赋值和算术分支以及 I/O `ERR/END/EOR` 控制项
  都在语义阶段绑定目标并保存离开 BLOCK 所需的逆序清理计划，emitter 不再根据源码行重新推断
  目标或扫描 `ASSIGN` 语句。正向、负向、严格 C17、sanitizer 和 gfortran 执行回归已进入测试。
- [ ] 将 `RETURN`、`STOP`、`CYCLE`、`EXIT`、交替返回及异常 I/O 边整合为完整过程级 CFG，补齐
  可达性、基本块、赋值标签数据流和每条边的生命周期证明。当前显式转移和异常 I/O 已有语义阶段
  清理计划，`RETURN` 走统一单元清理，`CYCLE/EXIT` 已绑定具体 DO；但仍不是完整 CFG，裸赋值
  `GOTO` 的目标集合仍按全单元 `ASSIGN` 来源保守解析，交替返回也尚未实现。
- [ ] 对任何离开作用域的边执行正确的临时量释放、可分配对象清理和派生对象终结；异常 I/O 分支
  也必须走同一生命周期模型。当前正常 BLOCK 结束、`RETURN`、`CYCLE/EXIT`、所有标签分支及
  `ERR/END/EOR` 会复用 typed cleanup plan，覆盖 BLOCK 内可分配对象和标量/数组派生对象；仍需把
  表达式临时量、函数结果、隐式错误边和后续完整 CFG 的所有边纳入同一所有权数据流后才能关闭。
- [ ] 完成语句级错误恢复，在单个输入中报告多个独立错误，同时保证错误结果不生成半成品 C。

### P0-IO-01 F90 外部与内部 I/O

- [ ] 完成 F90 `OPEN`/`CLOSE` 全部控制项，以及 `INQUIRE`、`BACKSPACE` 和 `ENDFILE`。外部文件
  形式现已进入统一 token → AST → typed IR → emitter 流程；生成端用线程局部文件单元状态机实现
  `OLD/NEW/SCRATCH/REPLACE/UNKNOWN`、`KEEP/DELETE`、顺序记录倒退、标准 C17 物理 `ENDFILE`
  截断，以及按 `UNIT/FILE` 查询连接和文件属性。`INQUIRE(IOLENGTH=)` 已使用独立 typed IR 控制项
  和无文件系统计数流复用真实无格式 wire-size 规则，覆盖所有整数结果 kind、零大小数组、数组段、
  向量下标、数组构造器、数组表达式、隐式 DO、复数/逻辑/字符和静态组件派生对象；结果可直接作为
  同类型/参数/shape/order 输出列表的直接无格式 `RECL`。正负向语义、严格 C17 执行、ASan/UBSan 和
  gfortran 差分已进入测试与 CI；更完整错误分类及后续标准控制项仍未完成。
- [ ] 完成顺序/直接访问、格式化/非格式化记录、`REC`、`RECL`、`ACCESS`、`ACTION`、`STATUS`、
  `BLANK`、`PAD`、`DELIM`、`POSITION` 和所有对应 `IOSTAT/IOMSG/ERR/END/EOR` 状态。连接状态现保存
  上述属性，记录传输路径已经覆盖直接访问 `READ/WRITE REC=`、定长记录边界、动作/格式/访问
  不匹配的无崩溃错误传播，以及顺序非格式化记录协议。仍需补齐所有控制项和错误状态的标准组合、
  非元素化数组函数结果和其他嵌套 transformational 结果的无格式传输，以及 stream access、异步
  I/O 和跨编译器二进制文件互操作策略。
- [x] 生成端使用统一 transfer/stream/record 状态机执行顺序与直接、格式化与非格式化记录传输；
  `UNIT` 和 `REC` 表达式只求值一次，`IOSTAT/IOMSG/ERR/END/EOR` 在清理记录和内部文件注册后分支。
  直接文件使用经过溢出检查的 `(REC-1)*RECL` 定位，格式化记录支持空格填充、斜杠编辑和 FORMAT
  回转跨记录，非格式化记录补零；缺失记录、非法记录号和 `NEXTREC` 已有执行回归。顺序非格式化
  文件使用固定 8 字节小端长度首尾标记，读取时验证损坏记录，`BACKSPACE` 按记录边界定位。
  已实现标量 kind、复数、逻辑、字符、任意 rank 具名数组、仿射数组段、向量下标、数组构造器、
  可元素化数组表达式、隐式 DO、静态组件派生对象和定义 I/O 的二进制传输；数组 shape、段边界、
  步长和非平凡标量子表达式会在元素循环外求值一次，复数与默认派生输出表达式也不会因分组件传输
  重复求值。严格 C17、ASan/UBSan、损坏文件回归及 gfortran 逐记录差分均已进入数值验证 CI。
- [x] `PRINT`、`READ` 和 `WRITE` 的显式格式共用 canonical token → FORMAT AST → typed IR →
  formatted item emitter；常量字符、标签和 F77 已赋值 FORMAT 均在编译期生成只读 C17 指令表，
  不再扫描源码行或在运行时重新解析常量文本。真正的运行时 CHARACTER 格式使用同一描述符接口的
  有界动态解析路径。嵌套组、无限组、冒号、空数据列表、literal、定位/比例/符号/空白/小数/舍入
  控制和 DT 元数据均有结构化节点；运行帧、DT iotype/v-list 和动态 literal 不再有 32、128、1,024
  等固定截断。40 层静态/动态嵌套、1,100 字节 literal、40 项 DT v-list、严格生成 C17 和 gfortran
  逐字节差分均已进入测试，固定 Reference LAPACK 3,535 文件、DGESV、INSTALL 和 52,512 项 RFP
  差分在本轮实现上重新通过。
- [ ] 补齐 `I/B/O/Z/F/E/EN/ES/D/G/L/A` 的宽度、精度、指数位数、舍入、符号、比例因子和
  星号溢出规则，并逐字段对比不同原生编译器。当前原生差分矩阵已覆盖整数基数、`F/E/D/G`、
  符号、比例、小数点/小数逗号、定位、嵌套、动态和标签格式；`E/D` 的 0P 规范化、三位指数省略
  标记、极大/极小双精度和 `G` 有效数字/尾随空白已经匹配 gfortran。仍需完成所有 `EN/ES`、显式
  指数位、六种 ROUND 模式、舍入进位边界、特殊值、超宽输入字段及全部描述符交叉组合。
  标准规定的最右嵌套组 FORMAT 回转点及其与非前进 I/O、冒号和无限组的全部组合也尚需逐项差分。
- [x] 用统一、可定位的内存记录引擎实现内部文件，不再通过 `tmpfile()` 模拟或结束时回读复制。
  字符标量和一维记录数组直接绑定原存储，覆盖多记录、默认 PAD、`T/TL/TR/X` 定位、已写记录
  空格填充、未触及记录保持和记录溢出 `IOSTAT`；显式格式、列表导向、NAMELIST 与定义 I/O 共用
  同一流及内部负单元游标。标准禁止的内部文件 `ADVANCE/EOR/SIZE` 和无格式传输会在生成前拒绝。
  严格 C17、ASan/UBSan、gfortran 差分以及 Emscripten `-sFILESYSTEM=0` Node 执行均已进入回归。
- [ ] 完成 F90 NAMELIST 的大小写、重复值、子串/数组段、派生对象扩展和错误恢复；动态分配必须
  先验证完整输入再原子提交，失败不得破坏原对象。
- [ ] 统一文件单元生命周期、预连接单元、并发访问和错误映射，明确线程安全策略。文件单元表及
  内部单元编号现为线程局部，预连接 `0/5/6`、隐式 `fort.<unit>` 连接和查询已有明确路径；仍需
  完成同一外部文件的跨单元冲突检测、细分标准 I/O 状态码、动作权限失败和并发文件访问契约。

验收标准：建立按语句、控制项、描述符和数据类型组合生成的 I/O 测试矩阵；生成 C 与至少两个
原生编译器逐记录差分，并在原生、Windows 和 WebAssembly 环境实际执行。

### P0-CG-01 C17 代码生成正确性和质量

- [ ] 代码生成只接受已验证的不可变 IR；消除 `codegen` 中的源码字符串识别、语义诊断和重新
  解析。普通/字符/派生类型赋值、指针赋值和 `NULLIFY` 已拆入独立 statement emitter，并仅消费
  已绑定的表达式与符号；标签和 I/O 跳转仅消费语义阶段生成的目标及清理计划。其他 codegen 模块
  仍存在少量源码字符串识别和生成期语义判断，因此本项保持未关闭。
- [ ] 系统审计严格别名、整数溢出、移位、浮点收缩、复数、求值顺序和指针算术，保证生成代码
  不依赖未定义行为或编译器扩展。位操作 intrinsic 已统一使用固定位宽无符号表示和 `memcpy`
  位复制，规避有符号移位、移位量等于位宽及别名未定义行为，并以符号位、完整位宽、零长度和
  重叠 `MVBITS` 的 UBSan 执行覆盖；字符 intrinsic 已使用无符号字节、`size_t` 溢出检查、显式
  长度和可复用临时存储覆盖空串、嵌入 NUL 及嵌套变换；数值模型 inquiry 已改为不求值实参的
  typed 常量降级，并以 C17 静态断言锁定整数宽度和 binary32/binary64 模型；其他生成路径仍需
  继续审计。
- [ ] 对辅助函数做基于 IR 使用信息的可达性生成，控制单文件输出的体积、C 编译时间和链接重复；
  不得以引入新的独立运行时库解决该问题。
- [ ] 为数组描述符、I/O、格式、NAMELIST、派生类型复制/终结等生成逻辑建立结构化 emitter，
  替换难以审查的大段 C 字符串模板。文件控制 emitter 已按 common/open/position/inquire 职责拆分，
  文件单元支持生成也已从总控模块隔离，格式控制解析与 formatted transfer 已从总控模块提取为
  共享 emitter；FORMAT 支持进一步按 state/parser/program/real 分责，AST→静态指令表 emitter 独立，
  但字段输入输出、NAMELIST、数组和生命周期模板仍需继续迁移。
- [ ] 建立生成代码可读性规范：稳定命名、源位置注释、确定性声明顺序、合理作用域和格式化；
  对公开接口及有代表性的生成实现使用快照测试。
- [ ] 记录并限制单个源文件的转译时间、峰值内存、生成 C 大小和生成 C 编译时间。

### P0-NUM-01 BLAS/LAPACK 数值等价

- [ ] 解释并消除固定审计基线中的未配对或超打印精度差异。当前统一审计记录为 5,807,798 条
  并集记录，其中 5,806,718 条双边配对，生成端和原生端各有 540 条独有记录；992,357 条有限
  数值差异超过双方打印舍入误差。它们不等同于官方阈值失败，但也不能宣称逐项数值等价。
- [ ] 为 NaN、无穷、signed zero、MIN/MAX、复数和浮点收缩建立书面语义策略；逐项证明差异是
  标准允许、原生编译器差异或转译错误，并为每个例外保留机器可审计理由。
- [ ] 在 GCC、Clang、MSVC/clang-cl 生成端及 gfortran、LLVM Flang、可用商业 Fortran 编译器
  上运行完整或分层 BLAS/LAPACK 正确性矩阵，不能只认证单一 Ubuntu/gfortran 组合。
- [ ] 将数值清单中的套件数、记录数、配对数和摘要作为单一机器可读来源，README/TODO/CI 不得
  手工复制出互相矛盾的统计值。README 中旧的 5,807,941 条统计已纠正为当前审计值 5,807,798，
  但仍需改为由清单自动生成并在 CI 核对。

### P0-PERF-01 性能闭环

- [ ] 将 71 项矩阵扩展到全部 BLAS 级别的代表例程、更多分块分解、特征值/SVD、不同规模、
  非单位步长、转置、线程和缓存工作集。
- [ ] 在 x86_64、AArch64、Windows、macOS 和 Linux 上分别建立稳定基线；共享 runner 的噪声
  必须用置信区间、重复轮次和历史趋势控制，不能只看单次中位数。
- [ ] 同时跟踪运行时间、生成 C 编译时间、代码大小和峰值内存；任何针对单一内核的 pragma 或
  特例都必须有通用适用条件和反向回归测试。
- [ ] 在约定矩阵全部通过前，只能声称“当前固定矩阵持平”，不能声称整体性能已经持平或超过
  原生 Fortran。

## P1：商业产品工程质量

### P1-ARCH-01 模块边界和可维护性

- [x] 拆分当前超大实现文件：`semantic/validation.c`、`frontend/parser.c`、`codegen/io.c`、
  `codegen/array.c`、`ast/parser.c`、`codegen/expression.c` 和 `codegen/unit.c`。按声明、接口、
  控制流、I/O 语义、数组构造、transform、过程调用、生命周期和 emitter 职责划分；当前生产
  C/H 文件均少于 1,000 行。动作/分配语句 AST、赋值 AST、赋值 emitter 和控制流生命周期规划
  也已分别落入 `ast/statement/`、`codegen/statement/` 和 `semantic/validation/`。数组 inquiry、
  数组关系归约、transform inquiry、`SELECT TYPE`
  guard、矩阵 transformational intrinsic 及关系归约生成支持分别位于独立模块，避免重新堆回通用
  call/statement/transpile 文件。
  数值模型定义、常量折叠、语义验证、表达式降低和生成端支持也分别位于 `semantic/`、
  `semantic/constant/`、`semantic/validation/intrinsic/`、`codegen/expression/` 和
  `core/generated/`，未恢复通用调用生成器中的名称分派。
- [x] 将 `src/internal/f2c.h` 从 730 行全局定义缩减为轻量跨域聚合头；基础设施、token、type、
  expression IR、statement IR、symbol/model、context、semantic 与 codegen 分别使用私有头文件。
- [x] 用 `F2cCompilationPhase`、`F2cUnitPhase` 和 `F2cIrState` 明确 source → token/syntax AST →
  typed IR → emitter 的阶段契约；typed IR 构建集中在 `frontend/pipeline.c`，代码生成入口会拒绝
  未完成语义阶段的对象。
- [x] 将误放在 `frontend/modules.c` 中的模块 emitter 拆到 `codegen/module.c`；生产 C/H 文件
  必须少于或等于 1,000 行，且 CMake、生产源码和公共头文件不得引用 `netlib-f2c`，这些约束均
  由跨平台 CMake 测试持续检查。表达式内建调用的 type/kind/rank/shape 绑定已从通用
  `ast/parser.c` 拆入 `ast/intrinsic.c`，解析器继续满足文件规模门禁。
- [ ] 统一动态数组、字符串、arena/对象所有权和错误传播工具；区分内存耗尽、输入错误、内部错误
  和未支持特性，审计所有 `size_t` 加乘法及 `realloc` 提交顺序。
- [ ] 将超过 2,700 行的 `test/test_transpile.c` 和超过 870 行的 `test/compile_generated.cmake` 按模块与
  测试层级拆分，避免单一测试文件成为变更冲突中心。

### P1-API-01 稳定公共 API 和 CLI

- [ ] 结构化诊断公共回调已经提供稳定分类码、严重级别、文件、起止位置和瞬时消息，同时保留
  `F2cResult.diagnostics` 文本层；仍需把目前使用通用分类的全部 parser/semantic 诊断细分为稳定
  子码，并补充 related location、修复建议和跨版本错误码基线。
- [ ] 在产品接口冻结前制定 ABI/API 兼容和弃用策略，增加导出符号基线、结构体大小校验和
  跨版本二进制测试。当前阶段不兼容历史 f2c API：`F2cConfig.structure_size` 必须精确等于当前
  `sizeof(F2cConfig)`，不存在 V1 前缀、逐字段读取或旧布局包装；首次稳定发布前仍需正式冻结接口。
- [x] 提供请求级、无全局状态的 `F2cConfig/F2cLimits` 和
  `f2c_transpile_project_config`，可限制输入字节、逻辑行、token、AST 节点、表达式解析/求值深度、
  常量求值步数、诊断数量、诊断字节和生成物大小；默认值以公共 `F2C_DEFAULT_*` 宏公开，超限
  不会返回半成品 C。
- [ ] 明确库的可重入、线程安全、取消和内存所有权契约；允许调用方设置 allocator、诊断回调、
  资源限额和取消令牌。
- [ ] CLI 已支持 stdin/stdout，输入使用有预算的流式增长读取，不再依赖 `fseek/ftell`；C 与头文件
  会全部暂存成功后再事务提交，提交失败恢复原文件；条件宏 `-D/-U`、附着/分离式 `-I`、相对当前
  源文件的引号 include 和系统 include 路径均已完成。仍需响应文件或项目清单、机器可读诊断及
  跨进程崩溃恢复日志。
- [ ] 为嵌入式使用提供不依赖 seek、文件系统或进程全局状态的流式输入/输出接口。

### P1-TEST-01 测试体系

- [ ] 按 lexer、preprocessor、parser、AST、语义、常量折叠、intrinsic、I/O、生命周期、IR 和
  emitter 建立独立单元测试，并保留项目级端到端测试。preprocessor 已从超大端到端测试拆为独立
  测试目标；数值模型契约及其 intrinsic 语义也已有独立测试目标，其他模块仍需继续拆分。
- [ ] 建立完整负向语料库，断言错误码、源码范围、恢复位置和输出抑制；禁止只匹配易变英文文本。
- [ ] 增加行/分支覆盖率门禁和历史趋势，分别统计转译器、生成辅助代码和数值脚本。
- [ ] 当 `F2C_BUILD_TESTING=ON` 时，数值脚本所需 Python 应成为明确依赖或由独立选项控制；不能
  因未找到 Python 而静默少注册 8 个测试。
- [ ] 将随机表达式差分扩展到所有 kind/rank、数组段、向量下标、字符、复杂别名、I/O 和过程
  调用，并自动缩减失败样本。
- [ ] 引入可合法复用的 F90 标准一致性语料及独立 oracle，避免只以 Reference LAPACK 覆盖率
  代表语言完整度。

### P1-SEC-01 鲁棒性与供应链

- [x] 请求级预算覆盖输入及预处理字节、条件定义和宏展开深度、逻辑行、token、AST 节点、
  表达式树/解析深度、常量求值步数、诊断及生成物；循环参数/宏引用、超深表达式/条件嵌套和各类
  超限路径均可预测失败且不返回部分输出。
- [ ] 继续建立总分配量/峰值内存预算和可注入 allocator；数组 rank 需要补齐标准上限的正向与
  越界测试，并保证所有临时动态数组都走统一的溢出检查和失败传播。
- [ ] 对深层表达式、嵌套构造、恶意格式串、NAMELIST、巨型维度和预处理输入增加 DoS 回归。
- [ ] 增加 clang-tidy/静态分析和覆盖编译器警告配置；对核心代码启用适当的整数、地址、未定义
  行为和内存 sanitizer 组合。
- [ ] GitHub Actions 第三方 action 使用完整提交 SHA 固定，并由自动更新工具维护；保留最小权限、
  provenance attestation 和 SHA-256 发布校验，不重新发布已明确移除的 SBOM 资产。
- [ ] 扩展 libFuzzer 字典、语料去重、覆盖趋势和长时轮换任务；评估 parser/format/NAMELIST 独立
  fuzz target 与 AFL++ 互补运行。

### P1-PORT-01 跨平台实际运行

- [ ] WebAssembly 不只编译，还要在 Node/browser runner 中执行公共 API、转译和编译后生成代码。
  无需文件系统的内部 I/O 已由 Emscripten `-sFILESYSTEM=0` 在 Node 中实际执行；公共 API、浏览器
  runner 及 WebAssembly 版 CLI 的端到端转译仍未完成。
- [ ] 增加 Android NDK 与 iOS/tvOS 模拟器或交叉编译门禁，验证静态库、CLI 可裁剪性和生成代码。
- [ ] 增加 Linux AArch64、musl、32 位目标和至少一种不同字节序的交叉编译/执行验证；审计
  `long`、`size_t`、对齐、字符符号性和二进制 I/O 假设。
- [ ] 明确各平台支持等级和未支持能力；在对应执行测试通过前不能把“能够交叉编译”描述为
  “完整支持移动端和 Web”。

### P1-DOC-01 文档与发布契约

- [ ] 为公共 C API、CLI、支持的 Fortran 标准/扩展、生成 ABI、诊断、线程安全和限制建立英文及
  中文文档，并保证两种语言的事实一致。
- [ ] 生成可追踪的“语言特性—测试—CI”支持矩阵，明确完整、部分、未支持和非目标特性。
- [ ] 发布前自动核对版本、CHANGELOG、支持矩阵、API/ABI 基线、数值清单和性能基线。
- [ ] 建立回归响应标准：正确性、崩溃、ABI、性能和安全问题的严重级别及修复时限。

## P2：Fortran 2003 及以后扩展

- [ ] 完成 `TYPE/CLASS` 动态类型、`SELECT TYPE/SELECT RANK`、继承、抽象类型、类型绑定与延迟
  过程、`PASS/NOPASS`、过程指针及过程指针组件的完整分派和接口检查。
- [ ] 完成标量和数组的所有 FINAL 过程选择、父子类型终结顺序、可分配/指针组件递归清理，以及
  正常返回、错误分支、重分配、赋值、构造和跨作用域跳转的全部终结时机。
- [ ] 完成可分配哑实参/函数结果、可分配派生组件、动态多态分配、`SOURCE/MOLD`、`MOVE_ALLOC`
  和深复制/移动的异常安全语义。
- [ ] 完成定义 I/O、完整派生类型 NAMELIST、`FLUSH`、异步 I/O、`WAIT`、stream access、
  `NEWUNIT` 和外部文件非前进 I/O 的全部边界语义。当前定义格式化/非格式化绑定已接入统一记录
  状态机，未定义 I/O 的静态组件派生对象可按声明顺序传输；动态组件必须使用适用绑定并在生成前
  验证，但完整继承分派、派生类型数组动态组件和跨实现二进制互操作仍未完成。
- [ ] 完成 `ASSOCIATE`、`BLOCK`、`DO CONCURRENT`、`FORALL`、子模块、C interoperability、IEEE
  模块和后续 transformational/inquiry intrinsic。
- [ ] 在确定产品标准范围后再规划 coarray、team、event、lock、atomic 等并行语言特性；不能将
  未实现的后续标准能力包含在“完整 Fortran 支持”声明中。

## 推荐实施顺序

1. `P0-FE-01` 与 `P1-ARCH-01`：先固定 token、源码范围、阶段边界和所有权，避免继续扩大文本
   扫描和超大文件。
2. `P0-AST-01`、`P0-SEM-01`、`P0-SEM-02`：完成类型化 IR、ABI、声明、接口和模块闭环。
3. `P0-STMT-01`、`P0-SEM-03`、`P0-IO-01`：补齐 F90 语句、intrinsic 和 I/O 标准矩阵。
4. `P0-CG-01`：清除代码生成阶段的解析/语义职责，建立生成质量和资源预算。
5. `P0-NUM-01`、`P0-PERF-01`：在语义稳定后收敛数值差异，并扩展多工具链/架构性能认证。
6. 并行推进 P1 API、测试、安全、跨平台和文档门禁；最后按明确版本范围逐项关闭 P2。
