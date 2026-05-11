# Project5 : B+ tree in Database
> 
>
> 本次的Readme数据库相关知识点的内容来自由TWL学长，并改编自其版本。

这是 "CS1966 (2025 - 2026 - 2) —— 程序设计与数据结构 II" 课程的第 4 个 project: 实现数据库中的 B+ 树。

在这次 project 中， 我们会为 bustub 关系型数据库编写 B+ 树索引。完成本次 project 不需要具备额外的数据库相关知识。

DDL : 第 16 周周日晚，`2026 年 6 月 21 日 23:59` 

# 基础知识

在开始这个 project 之前， 我们需要了解一些基础知识。 由于课上已学习过 B 树与 B+ 树，这里没有对 B 树与 B+ 树进行介绍， 如有需要请查阅相关课程 PPT。如果你对 B+ 树进行操作后的结构有疑惑， 请在 https://www.cs.usfca.edu/~galles/visualization/BPlusTree.html 网站上进行尝试。 此外， 这个博客的动图非常生动： https://zhuanlan.zhihu.com/p/149287061。

## 数据库的简单介绍(optional)

我们所说的数据库通常指数据库管理系统(DBMS, 即 Database Management System)。 我们可以简单理解为组织管理庞大数据的一个软件。

数据库可以分成很多类型， 这里我们所关心的 bustub 数据库是"关系型数据库"。 我们可以简单理解成， 这种数据库里存储的是一张张表格， 这些表格根据数据之间的关系建立起来。 例如， 下图就是一个表格示例。

<img src="https://notes.sjtu.edu.cn/uploads/upload_d74ed6ea471f51aa663eeb281bae90b9.png" width="500">

我们的表格通过遵守一定的格式存储在磁盘上。

对于数据库架构， 我从网络上找到了一个非常形象的图：(版权归"小林 coding"公众号所有， 该图为知名数据库 mysql 的架构示意图)

<img src="https://notes.sjtu.edu.cn/uploads/upload_2f43366c45fa12ba230efff3b21c3da4.png" width="800">

我们的数据库管理系统通常分为 Server 层和存储引擎层。 Server 层需要解决网络通信、 SQL 语句解析、 执行计划生成与优化等问题。 Server 层决定了用户输入的 SQL 查询语句是如何转化成优化后的执行计划。存储引擎层则负责数据的存储与提取。 不同存储引擎所使用的数据结构和实现方式可能并不相同。

如果你并不能看懂上图也没有关系。对于本次 project， 我们只需要关心 "存储引擎" 部分。下面我将介绍 bustub 数据库的存储引擎。

## 存储引擎与 B+ 树

我们的 bustub 数据库的存储引擎将数据存储在磁盘上， 实现数据的持久化。我们知道， 磁盘的一大特征便是空间大但访问速度非常慢， 因此， 我们希望能减少对于磁盘的交互访问 (以下称为磁盘 IO) 次数。

为什么我们采取 B+ 树作为这个存储引擎的数据结构？ 首先， 为了便于查询，我们需要给我们的表格建立 "目录"， 即选取表格中的某一列作为 "索引"。这样， 通过索引便可建立一个有序的数据结构(如二叉搜索树)。 我们查询时只需要先找到索引， 便可找到我们所需的数据行。 但二叉搜索树的深度太大， 导致对其进行查询（或插入删除）操作时磁盘 IO 次数太多。 因此， 我们可以考虑选取 B 树， B 树的深度往往远小于数据数量， 通常可维持在 3-4 层左右。 但 B 树每个结点都存储索引和数据行， 导致 B 树的单个节点所占空间太大。 另一方面， B 树不支持按照索引进行顺序查找。 因此， 我们可以将 B 树升级为 B+ 树， 只在叶子结点存储真正的数据行， 非叶子节点只存储索引。

<img src="https://notes.sjtu.edu.cn/uploads/upload_bfde29d73741b26103fce71094eae7e4.png" width="800">


实际上， 我们并不一定只创建唯一的一个 B+ 树。 试想我们需要存储每位同学的 `姓名、学号、性别、年龄、学院` 这些信息。 我们按照 `学号` 为索引构建了初始的 B+ 树， 叶子节点包含每位同学的记录信息。 但此时， 如果我们要频繁地对于 `年龄` 这一特征进行范围查找， 我们便希望所有数据按照 `年龄` 也是有序排列的。因此， 我们可以以 `年龄` 这一列为索引构建第二棵 B+ 树，这棵树的叶子节点存储的值为 `学号`(这样存储是因为数据记录只有一份)。 因此， 如果我们需要对 `年龄` 这一特征进行范围查找, 我们可以在第二棵树中进行搜索， 然后得到对应的 `学号` 值， 再回到第一棵树中搜索到对应的记录。

## 存储引擎与数据页(optional)

根据上方的表格图像，我们每行都存储了一条数据信息。但如果用户执行查询操作， 我们并不能以 "行" 为单位读取数据， 否则一次磁盘 IO 只能处理一行， 执行效率过低。 我们这里采取的策略是以 `page`（页）为单位进行磁盘 IO。 同时， 我们的索引结点也以 `page` 为基本单位进行存储， 即每个索引结点（中间结点）都对应一个 `page`。 我们可以简单地将一个 `page` 视为是固定大小的一块存储空间。 通过打包一系列数据进入同一个 page， 可以实现减少磁盘 IO 的效果。这里我们并不需要了解 `page` 的细节，与磁盘的 IO 操作已被封装为以下几个函数: `FetchPageRead`，`FetchPageWrite`。我会在之后详细说明这两个函数。 

Tips: 如果你听说过内存分页， 内存中的 `page` 和这里的 `page` 并不是同一概念， 请不要混淆。 另外， 如果你接触过文件系统， 我们的 bustub 数据库是建立于操作系统管理的文件系统之上的。 这里采取的策略是将数据表和对应的元数据存入一个或多个文件。

<img src="https://notes.sjtu.edu.cn/uploads/upload_5879015cd51b787ca781b64ac3e5e7b2.png" width="800">

### B+ 树加锁方法

B+ 树加锁采用 "螃蟹法则"。具体请见下图。 

<img src="https://notes.sjtu.edu.cn/uploads/upload_9c4c517643c2ab7eba276408e2233d8f.png" width="800">

<img src="https://notes.sjtu.edu.cn/uploads/upload_38c70fad05997aaf4ffb671539067d16.png" width="800">


这里我用大白话再解释一遍： 拿 `insert` 函数举例， 我们在搜索路径上每次都先拿 parent 结点的锁， 然后拿 child 结点的锁， 如果 child 结点是 "安全" 的， 就自上而下释放一路走下来所有 parent 结点的锁。（自上而下和自下而上都可以，但是自上而下能更快冲淡堵塞）

所谓的 "安全" 如何定义？ 只要 `child page` 插入时不满， 或者删除时至少半满，那就安全。 

进阶螃蟹法则（乐观锁）的意思就是， 对于写的线程仍然先拿读锁， 如果发现遇到了不安全的 `leaf page`, 可能引起上方的 `internal page` 也发生分裂， 那我立刻放弃继续执行乐观锁策略， 然后重新开始，依照普通的螃蟹法则进行写入的操作。

# 主体任务

请你修改 `src/include/storage/index/b_plus_tree.h` 和 `src/storage/index/b_plus_tree.cpp`, 实现 b+ 树的查找、插入和删除函数。 在此之后， 请你完善 b+ 树的查找、插入、删除函数， 使其线程安全。

# 熟悉项目代码
请跟着我一步一步熟悉本次 project 需要用到的代码文件。

## page_id、Page 和 PageGuard

普通内存 B+ 树里，节点之间可能直接保存 `Node *`，但本项目不是这样。数据库里的节点要放在磁盘页里，所以节点之间保存的是 `page_id`，而不是指针。

一个 B+ 树节点实际存放在 `Page::data_` 区域中。`Page` 本身还带有 `page_id_`、`pin_count_`、`is_dirty_` 和读写锁等信息。

想访问一个树节点，需要先拿到它的 page_id，然后用 BufferPoolManager fetch 这个 page 得到 PageGuard ，再用 As<T>() / AsMut<T>() 把 data_ 解释成具体 page 类型。

常用的 buffer pool 入口有三个：

```cpp
bpm_->FetchPageRead(page_id);
bpm_->FetchPageWrite(page_id);
bpm_->NewPageGuarded(&page_id);
```

读 page 时通常：

```cpp
auto guard = bpm_->FetchPageRead(page_id);
auto page = guard.As<BPlusTreePage>();
```

写 page 时通常：

```cpp
auto guard = bpm_->FetchPageWrite(page_id);
auto leaf = guard.AsMut<LeafPage>();
```

申请新 page 时通常：

```cpp
page_id_t new_page_id;
auto basic_guard = bpm_->NewPageGuarded(&new_page_id);
```

（`PageGuard` 的实例）guard 活着时，对应 page 会被正确地 pin 住并持有相应的锁；guard 析构或调用 `Drop()` 后，会释放锁和 pin。使用 `AsMut<T>()` 修改 page 时，page 会被标记为 dirty，后续由 buffer pool 负责写回。

## B+ 树相关的 page 类型

本项目中和 B+ 树直接相关的 page 类型主要有这些：

| 类型                  | 作用                 |
| ---                   | ---                  |
| `BPlusTreeHeaderPage` | 保存 `root_page_id_`，用来找到整棵树的根 |
| `BPlusTreePage` | internal page 和 leaf page 的公共头部，保存 page 类型、当前 size、max size |
| `BPlusTreeInternalPage` | 内部节点，key 用来导航，value 是 child 的 `page_id` |
| `BPlusTreeLeafPage` | 叶子节点，保存真正的 key/value，并通过 `next_page_id_` 串起叶子链表 |

## 任务

你主要需要实现 `src/storage/index/b_plus_tree.cpp` 中的函数。

- `GetValue(key)` （随机查找）

- `Insert(key, value)` （插入）

- `Remove(key)` （删除）

范围遍历依赖叶子层链表，插入和删除时要注意维护 `next_page_id_`。

## 重要文件

| 文件              | 主要内容 |
| --- | --- |
| `src/include/storage/index/b_plus_tree.h` | `BPlusTree` 的公开接口、成员变量和 `Context` |
| `src/storage/index/b_plus_tree.cpp` | `Insert`、`Remove`、`GetValue`、`Begin` 的主要实现位置 |
| `src/include/storage/page/b_plus_tree_header_page.h` | header page 定义|
| `src/include/storage/page/b_plus_tree_internal_page.h` | internal page 定义|
| `src/include/storage/page/b_plus_tree_leaf_page.h` | leaf page 定义|
| `src/include/storage/index/index_iterator.h` 和 `src/storage/index/index_iterator.cpp` | 范围扫描接口 |
| `src/include/storage/page/page_guard.h` | guard 管 page 访问生命周期 |
| `src/include/buffer/buffer_pool_manager.h` | fetch/new page 的入口（通常当黑盒使用） |

## Context 和并发

`Context` 定义在 `src/include/storage/index/b_plus_tree.h` 中，用来辅助记录一次操作过程中持有的 page guard。

它主要包含：

+ `header_page_`：插入或删除时可能需要持有 header page 的写 guard。
+ `root_page_id_`：记录当前 root，方便判断某个 page 是否是根。
+ `write_set_`：保存路径上的写 guard。
+ `read_set_`：如果你需要，也可以保存路径上的读 guard。

实现螃蟹锁时可以利用 Context。

## 测试内容

| 测试 | 主要覆盖 |
| --- | --- |
| `b_plus_tree_insert_test` | 插入、点查、基本的树结构增长 |
| `b_plus_tree_delete_test` | 删除后树结构仍然正确，root 和 page size 维护正确 |
| `b_plus_tree_concurrent_test` | 多线程读写下树结构不被破坏 |
| `b_plus_tree_contention_test` | 并发压力和耗时场景（不能单独代表完整正确性） |


# 推荐的攻略

推荐你采取这样的路径进行编写：

+ 简单的插入操作。 在不考虑分裂的情况下编写 `insert` 函数。

+ 查找操作。 

实现以上二者之后请测试二者是否正确。

+ 简单的分裂操作。 请你编写 `insert` 函数， 考虑只有一个结点被分裂的情况。 (即此时不会递归分裂)

+ 递归的分裂操作。 请你继续编写 `insert` 函数， 考虑递归分类的情况。

到这里， 你应该可以通过 insert test。

+ 简单的删除操作。 此时不会发生合并或者借用。

+ 简单的合并 / 借用操作。 

+ 复杂的合并 / 借用操作。 此时会递归发生合并 / 借用。

到这里， 你应该可以通过 delete test。

+ 为以上函数增添并发组件， 使其线程安全。

# 测试方法

## 本地测试

请到该项目的根目录执行以下命令（按需选择）。

```shell
# 完成 CMake 配置、编译 4 个测试，并运行 4 个测试
build_support/run-bpt-tests.sh

# 从干净的 `build` 目录重新构建
build_support/run-bpt-tests.sh --clean

# 编译 4 个测试
build_support/run-bpt-tests.sh --no-run
```

构建好之后单独运行测试的命令如下（每次修改后的测试之前都要重新编译）：

```shell
cd build #进入 build 目录， 如果已经在 build 目录请忽略
./test/b_plus_tree_insert_test
./test/b_plus_tree_delete_test
./test/b_plus_tree_contention_test
./test/b_plus_tree_concurrent_test
```

如果提示缺少 `cmake`、编译器或其他依赖，请先安装依赖：

```shell
sudo bash build_support/packages.sh -y # WSL / Ubuntu
bash build_support/packages.sh -y      # macOS
```

## 提交测试

由于本次项目过大， ACMOJ 不具备相关功能。 因此， 请通过本地测试后将代码压缩发给我， 我会尽快在我的本机上进行测试。

# 评分标准

``` python
./test/b_plus_tree_insert_test              45 分
./test/b_plus_tree_delete_test              45 分
./test/b_plus_tree_contention_test          25 分
./test/b_plus_tree_concurrent_test          25 分
Code Review                                 10 分
```

满分上限为 __120__ 分，加满为止。 溢出 __100__ 分的部分抵消之前大作业、小作业所扣分数。

如果你在 `Project3 : Set` 中正确完成了内存版本的 B+ 树， 可以选择不做本次 project 并通知助教, 本次 project 分数以 80 分计入。

# 试一试你自己的数据库！(optional)

当你完成了对 B+ 树的编写后， 我们的数据库已经可以编译出接收 SQL 语句的 shell。

```shell
cd build #进入 build 目录， 若已在 build 目录请忽略
make -j$(nproc) shell
./bin/bustub-shell
```

之后， 你可以运行 `\dt` 来查看存储在数据库中的所有表格

```shell
bustub> \dt
+-----+----------------------------+------------------------------------------------------------------------------------------+
| oid | name                       | cols                                                                                     |
+-----+----------------------------+------------------------------------------------------------------------------------------+
| 23  | test_2                     | (colA:INTEGER, colB:INTEGER, colC:INTEGER)                                               |
| 21  | test_simple_seq_2          | (col1:INTEGER, col2:INTEGER)                                                             |
...
| 12  | __mock_t1                  | (x:INTEGER, y:INTEGER, z:INTEGER)                                                        |
+-----+----------------------------+------------------------------------------------------------------------------------------+
```

你还可以编写各种 SQL 语句操作你的数据库

```shell
bustub> SELECT * FROM __mock_table_1;
+---------------------+---------------------+
| __mock_table_1.colA | __mock_table_1.colB |
+---------------------+---------------------+
| 0                   | 0                   |
| 1                   | 100                 |
| 2                   | 200                 |
| 3                   | 300                 |
...
| 98                  | 9800                |
| 99                  | 9900                |
+---------------------+---------------------+
```


如果你对数据库感兴趣， 强烈建议你在闲暇时刻学习 `CMU15445` 这门课， 并选择性阅读配套教材 "`Database Concept`"！


Acknowledgement : `CMU15445 Database System`. (https://15445.courses.cs.cmu.edu/spring2023/project2/).
