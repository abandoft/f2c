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
  构建基线已经建立；本轮严格 Release CTest 为 30/30，架构边界检查作为独立测试运行。
- [x] 固定 Reference LAPACK 3.12.1 提交
  `6ec7f2bc4ecf4c4a93496aa2fa519575bc0e39ca`；3,535 个 Fortran 文件和 155 个 BLAS 文件
  已有严格 C17 编译门禁。
- [x] 固定官方 LIN、EIG、RFP、BLAS 驱动及统一数值审计已经进入 CI；当前记录中生成端新增
  官方阈值失败为 0。
- [x] 71 项生成 C 与原生 Fortran 性能矩阵已经进入 CI，当前规则为任何单项不得慢于 5%。

以上内容是“当前已通过的基线”，不是完整 Fortran 标准支持或商业化完成声明。

## P0：编译器核心正确性

### P0-FE-01 统一源码映射和词法流

- [x] 自由格式、固定格式、续行和分号拆分现在使用紧凑的连续区间源码映射；token、表达式 AST、
  语句 AST 及结构化词法诊断均保留原始文件的物理起止行列。自由/固定续行、分号拆句及续行声明
  诊断已有回归，映射不会按输入字符分配独立位置对象。
- [ ] 旧式标号 `DO` 的结构改写目前只能把合成 token 回指到原语句起点，尚未提供逐片段精确映射。
  预处理对象宏已经保留 expansion/spelling 双范围和重映射源码名，文本与结构化诊断均可同时呈现；
  仍需让所有 parser/semantic 诊断直接消费 token/AST span，并增加完整宏/include 栈 related location，
  才能满足“任意诊断均有精确范围”的验收标准。
- [ ] 让声明、程序单元头、`USE`、`NAMELIST`、旧式语句和 I/O 全部消费统一 token 流，删除
  `f2c_identifier`、`f2c_split_*`、`f2c_starts_word`、括号/引号手工扫描等生产解析路径。表达式和
  语句入口现已接受预先生成的 canonical token 流；程序单元、模块/接口/派生类型边界、过程引用、
  `USE`、`PROCEDURE`、`NAMELIST`、`COMMON` 及 `EQUIVALENCE` 的外层结构和实体设计子已经迁移；
  类型声明的 type/kind/length 选择器、属性、实体、维度和初始化，以及程序单元头和 `IMPLICIT`
  映射/符号发现均消费 canonical token，并把规格表达式范围保存到语义模型。`EQUIVALENCE` 下标
  直接构建并常量求值 token AST；属性关键字会结合顶层赋值 token 判别，`DIMENSION`、`EXTERNAL`、
  `PARAMETER`、`SAVE` 和 `EQUIVALENCE` 作为合法变量名时不再被误判为声明。`DATA` 的组、重复因子、
  设计子和嵌套隐式 DO 已完全消费 canonical token range；旧式控制语句、I/O 和少量字符串入口仍有
  文本扫描。源码归一化层的注释识别、分号拆句和代码大小写处理已经
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
- [ ] 将当前面向 LAPACK 的简化预处理逻辑替换为明确的预处理契约：要么完整支持所承诺的
  `#define/#if/#include/#line` 子集及宏来源，要么要求调用方预处理并对未支持指令给出硬错误。
  固定 16 层嵌套数组和 `USE_ISNAN` 特例判断已经删除；独立预处理模块现使用动态条件栈、
  大小写敏感的输入局部定义表、带正常优先级和短路规则的整数条件表达式，并通过 API/CLI 的
  `F2cPreprocessorDefinition` 与 `-D/-U` 显式接收初始环境。对象宏递归正文展开、`#include`、标准
  Fortran `INCLUDE`、`#line`/数字行标记、API resolver/release、CLI `-I`、跨 include 宏环境、循环与
  深度/数量/总字节预算，以及 expansion/spelling 双范围均已有独立正负向测试。函数式宏、反斜杠
  续行、完整 C 预处理整数类型/字符常量与宏化 include operand 尚未实现，故本任务保持未关闭。

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
  rank/shape/kind、关键字关联、常量 `DIM/KIND` 约束及溢出安全的 C17 降级，仍需补齐其余规格
  intrinsic 和所有允许出现位置。
- [x] 移除 `f2c_emit_cached_expression` 和 `f2c_translate_expression` 原始文本降级路径；模块实体、
  派生类型组件、声明初始化、字符长度和数组边界均在语义阶段建立表达式 AST。`codegen/` 不得
  调用表达式解析器，该边界由 `architecture_boundaries` 测试强制检查。
- [ ] 保证 Fortran 未指定求值顺序不会被错误固化，具有副作用的实参和下标只求值一次；数组
  重叠、函数结果和临时对象具有可证明的生命周期。
- [x] 语句函数已建立真实的类型化定义 AST；直接和嵌套调用使用每次调用独立的 C17 实参临时值，
  不再通过字符串宏式替换重复求值。带副作用外部函数实参的严格编译、执行回归会验证只调用一次。

验收标准：生产代码生成路径不存在表达式重新解析；不同 kind/rank、动态 shape、向量下标和
重叠赋值均有 gfortran 差分及 sanitizer 回归。

### P0-SEM-01 类型、kind 和 ABI

- [ ] 建立目标 ABI 数据模型，覆盖所承诺的 INTEGER、REAL、COMPLEX、LOGICAL 和 CHARACTER
  kind，不再把 kind 仅当作附加整数元数据。
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
  冲突类型和非法指针初始化会硬失败；仍需把所有属性组合和跨声明一致性集中到独立语义阶段。
- [ ] 补齐 `PROGRAM`、`MODULE`、`BLOCK DATA`、`SUBROUTINE`、`FUNCTION`、`ENTRY`、内部过程
  和任意层宿主关联；验证 `RECURSIVE`、`PURE`、`ELEMENTAL` 等过程属性。用户定义
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
  `EQUIVALENCE` 传播到实际根存储，重复调用执行回归和固定 BLAS/LAPACK 全量差分均已覆盖。
- [ ] 完成命名及空白 `COMMON`、`EQUIVALENCE`、`DATA` 和 `SAVE` 的布局、初始化顺序、重叠与
  跨程序单元一致性。目前空白 `COMMON` 会产生硬错误；模块 `DATA`、`BLOCK DATA`、异类型
  `EQUIVALENCE` 存储单元以及跨语句的任意重叠区间仍需纳入统一项目级存储布局，因此本项不关闭。
- [ ] 完成模块 `PUBLIC/PRIVATE`、`ONLY`、重命名、泛型接口、运算符/赋值泛型和模块过程，移除
  `LA_CONSTANTS` 特例及“派生类型不能重命名”的限制。
- [ ] 对缺失的非 intrinsic 模块建立明确策略；不能把无法解析的 `USE` 静默当成普通外部过程。

验收标准：以标准小程序覆盖每类程序单元和关联规则；跨文件接口不一致必须在生成 C 之前报告；
Reference LAPACK 继续全量严格编译且源码中不再存在模块名称硬编码。

### P0-SEM-03 Intrinsic 注册与降级

- [ ] 用单一声明式注册表描述每个 intrinsic 的标准版本、泛型候选、参数关联、kind/rank/shape
  规则、常量折叠和 C17 降级，删除语义分析与代码生成之间的重复分派。
- [ ] 完成 F90 全部 intrinsic 及项目承诺的旧式 intrinsic；重点补齐位操作、字符处理、数值模型、
  kind 选择、数组 inquiry 和随机数语义。数组 inquiry 子集已覆盖非默认下界、零 extent 的标准
  `LBOUND=1/UBOUND=0`、动态 `DIM`、`KIND=1/2/4/8`、切片/构造器/elemental 数组表达式、可分配
  结果和假定形状哑实参；独立负向语义测试、严格 C17 执行及 gfortran 差分已进入 CI。
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
  因其他合法 `DO`/旧式控制形式未完成而保持未关闭。
- [ ] 将标签、计算/赋值 `GOTO`、算术 IF、旧式标号 DO、`RETURN`、`STOP`、`CYCLE` 和 `EXIT`
  建模为显式控制流图，统一验证不可达目标、非法跨构造跳转和清理边。
- [ ] 对任何离开作用域的边执行正确的临时量释放、可分配对象清理和派生对象终结；异常 I/O 分支
  也必须走同一生命周期模型。
- [ ] 完成语句级错误恢复，在单个输入中报告多个独立错误，同时保证错误结果不生成半成品 C。

### P0-IO-01 F90 外部与内部 I/O

- [ ] 完成 F90 `OPEN`/`CLOSE` 全部控制项，以及 `INQUIRE`、`BACKSPACE` 和 `ENDFILE`。目前
  `BACKSPACE` 被明确作为未支持语句测试，`OPEN` 仅处理少量控制项。
- [ ] 完成顺序/直接访问、格式化/非格式化记录、`REC`、`RECL`、`ACCESS`、`ACTION`、`STATUS`、
  `BLANK`、`PAD`、`DELIM`、`POSITION` 和所有对应 `IOSTAT/IOMSG/ERR/END/EOR` 状态。
- [ ] 让 `PRINT`、`READ` 和 `WRITE` 共享同一格式 AST 和执行引擎；验证显式 FORMAT、运行时格式、
  列表导向、格式回转、嵌套重复组、冒号和空数据列表。
- [ ] 补齐 `I/B/O/Z/F/E/EN/ES/D/G/L/A` 的宽度、精度、指数位数、舍入、符号、比例因子和
  星号溢出规则，并逐字段对比不同原生编译器。
- [ ] 用内存记录引擎实现内部文件，不再通过 `tmpfile()` 模拟；覆盖字符标量/数组、多记录、
  非前进状态、PAD 和文件位置，确保 Web/mobile 无文件系统环境可运行。
- [ ] 完成 F90 NAMELIST 的大小写、重复值、子串/数组段、派生对象扩展和错误恢复；动态分配必须
  先验证完整输入再原子提交，失败不得破坏原对象。
- [ ] 统一文件单元生命周期、预连接单元、并发访问和错误映射，明确线程安全策略。

验收标准：建立按语句、控制项、描述符和数据类型组合生成的 I/O 测试矩阵；生成 C 与至少两个
原生编译器逐记录差分，并在原生、Windows 和 WebAssembly 环境实际执行。

### P0-CG-01 C17 代码生成正确性和质量

- [ ] 代码生成只接受已验证的不可变 IR；消除 `codegen` 中的源码字符串识别、语义诊断和重新
  解析。
- [ ] 系统审计严格别名、整数溢出、移位、浮点收缩、复数、求值顺序和指针算术，保证生成代码
  不依赖未定义行为或编译器扩展。
- [ ] 对辅助函数做基于 IR 使用信息的可达性生成，控制单文件输出的体积、C 编译时间和链接重复；
  不得以引入新的独立运行时库解决该问题。
- [ ] 为数组描述符、I/O、格式、NAMELIST、派生类型复制/终结等生成逻辑建立结构化 emitter，
  替换难以审查的大段 C 字符串模板。
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
  C/H 文件均少于 1,000 行。数组 inquiry、数组关系归约、transform inquiry、`SELECT TYPE`
  guard、矩阵 transformational intrinsic 及关系归约生成支持分别位于独立模块，避免重新堆回通用
  call/statement/transpile 文件。
- [x] 将 `src/internal/f2c.h` 从 730 行全局定义缩减为轻量跨域聚合头；基础设施、token、type、
  expression IR、statement IR、symbol/model、context、semantic 与 codegen 分别使用私有头文件。
- [x] 用 `F2cCompilationPhase`、`F2cUnitPhase` 和 `F2cIrState` 明确 source → token/syntax AST →
  typed IR → emitter 的阶段契约；typed IR 构建集中在 `frontend/pipeline.c`，代码生成入口会拒绝
  未完成语义阶段的对象。
- [x] 将误放在 `frontend/modules.c` 中的模块 emitter 拆到 `codegen/module.c`；生产 C/H 文件
  必须少于或等于 1,000 行，且 CMake、生产源码和公共头文件不得引用 `netlib-f2c`，这些约束均
  由跨平台 CMake 测试持续检查。
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
  测试目标，其他模块仍需继续拆分。
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

- [ ] WebAssembly 不只编译，还要在 Node/browser runner 中执行公共 API、转译、编译后生成代码
  和无需文件系统的 I/O 测试。
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
  `NEWUNIT` 和非前进内部文件语义。
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
