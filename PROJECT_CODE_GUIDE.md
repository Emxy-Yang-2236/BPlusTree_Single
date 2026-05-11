# B+ 树大作业项目导览

这份文档先写给助教看，目的不是重新讲一遍 B+ 树算法，而是帮你快速恢复对这个 project 的整体认识。等你觉得这条线清楚了，README 里的“熟悉项目代码”部分就可以按类似结构改写给学生。

这个大作业的核心可以先用一句话概括：

> 学生要实现的不是一棵普通内存 B+ 树，而是一棵放在数据库 page 里的 B+ tree index。

所以同学真正容易卡住的地方，往往不是“B+ 树该怎么查找”，而是下面这些工程问题：

```text
节点为什么不是 Node *，而是 page_id？
为什么每次访问节点都要找 BufferPoolManager？
PageGuard 到底和锁、pin、写回有什么关系？
HeaderPage、InternalPage、LeafPage 分别放在哪里？
测试程序到底从哪里开始调用学生代码？
```

下面按这个顺序讲项目。

## 1. 先看整个项目的调用链

学生运行的测试并不是直接调用某个 page 类。测试一般会先搭好数据库存储环境，然后创建一棵 `BPlusTree`，最后通过 `Insert`、`GetValue`、`Remove`、`Begin` 这些公开接口验证结果。

可以把测试到学生代码的路径理解成这样：

```text
test/storage/b_plus_tree_*.cpp
        |
        v
DiskManager + BufferPoolManager
        |
        v
BPlusTree<KeyType, ValueType, KeyComparator>
        |
        v
header page 记录 root_page_id
        |
        v
root_page_id 指向 internal page 或 leaf page
        |
        v
internal page 继续指向 child page_id
leaf page 保存真正的 key/value
```

这里最重要的是分清责任边界。

`DiskManager` 和 `BufferPoolManager` 已经给好，学生通常不需要改。它们负责 page 的分配、读取、缓存、替换、pin/unpin 和脏页写回。

学生主要实现的是 `BPlusTree` 这一层。也就是说，学生要决定“该访问哪个 page_id、把 page 内容解释成什么类型、怎样维护树结构”。至于 page 怎么从磁盘或 buffer pool 里来，交给 `BufferPoolManager`。

## 2. 这份代码里没有裸节点指针

普通 B+ 树教材里，节点之间可能长这样：

```cpp
struct Node {
  std::vector<Key> keys;
  std::vector<Node *> children;
};
```

这个 project 里不能这么想。数据库里的节点要能落在磁盘页上，所以树节点存放在 `Page::data_` 这块字节区域里，节点之间也不直接保存 C++ 指针，而是保存 `page_id`。

更接近本项目的视角是：

```text
HeaderPage
  root_page_id = 3

Page 3
  data_ 被解释成 BPlusTreeInternalPage
  values = [7, 8, 9]        // child page ids

Page 8
  data_ 被解释成 BPlusTreeLeafPage
  array = [(key, rid), ...]
```

因此，学生写代码时脑子里始终要有一个转换：

```text
想访问一个树节点
  -> 先拿到它的 page_id
  -> 用 BufferPoolManager fetch 这个 page
  -> 得到 PageGuard
  -> 用 As<T>() / AsMut<T>() 把 data_ 解释成具体 page 类型
```

这就是这个 project 和普通 B+ 树作业最大的差别。

## 3. 四类对象分别负责什么

理解项目时，可以先把核心对象分成四层。

| 层次 | 代表对象 | 作用 |
| --- | --- | --- |
| 存储环境 | `DiskManager`、`BufferPoolManager` | 管 page 的创建、读取、缓存和回收 |
| page 外壳 | `Page`、`PageGuard` | 一个固定大小的物理页，以及访问它时的生命周期管理 |
| B+ 树页格式 | `BPlusTreeHeaderPage`、`BPlusTreeInternalPage`、`BPlusTreeLeafPage` | 规定 `Page::data_` 里应该怎么解释 |
| 索引接口 | `BPlusTree`、`IndexIterator` | 学生真正实现的树操作和遍历接口 |

这张表比逐个贴头文件更适合放进 README。学生不需要一开始知道每个成员函数，只要先知道每层在项目里负责什么。

`BufferPoolManager` 对学生来说主要暴露三个入口：

```cpp
FetchPageRead(page_id)
FetchPageWrite(page_id)
NewPageGuarded(&page_id)
```

`FetchPageRead` 用于只读访问，`FetchPageWrite` 用于修改 page，`NewPageGuarded` 用于申请新 page 并拿到新 `page_id`。

`PageGuard` 可以理解成“我正在持有这个 page 的凭证”。这个凭证活着时，page 处于被访问状态；凭证析构或 `Drop()` 后，相关的锁和 pin 会释放。写 guard 通过 `AsMut<T>()` 修改 page 时，还会让 page 变脏，后续由 buffer pool 负责写回。

这也是为什么 README 不应该把 PageGuard 的完整实现大段贴出来。学生需要先知道它的使用规则，而不是先读它的所有构造、移动、析构代码。

## 4. 三种 B+ 树 page 的项目含义

这个项目里和 B+ 树直接相关的 page 类型主要有三种。

`BPlusTreeHeaderPage` 很小，只记录根节点在哪里：

```text
root_page_id_
```

所以空树和非空树的区别，首先体现在 header page 里。空树时 `root_page_id_` 是 `INVALID_PAGE_ID`；创建第一个叶子节点后，需要把 header page 里的 root 改成新叶子的 `page_id`。

`BPlusTreeInternalPage` 用于导航。它保存 key 和 child page id。这里的 value 不是记录本身，而是下一层孩子的 `page_id`。学生查找时在 internal page 中比较 key，然后决定继续 fetch 哪个 child page。

`BPlusTreeLeafPage` 保存真正的索引项，也就是测试里关心的 key 到 `RID` 的映射。叶子页之间还通过 `next_page_id_` 连接，所以 `Begin()` 和 iterator 不只是看单个 leaf，还依赖叶子链表是否维护正确。

还有一个共同父类 `BPlusTreePage`，它保存 page 类型、当前 size、max size 等公共信息。代码里经常先把 page 解释成 `BPlusTreePage`，判断它是 internal 还是 leaf，再进一步解释成具体类型。

## 5. 一次操作在工程上发生什么

这里不展开 B+ 树算法，只看 project 里的数据流。

`GetValue(key)` 的工程流程是：

```text
读 header page，拿 root_page_id
如果 root 无效，说明空树
fetch root page
如果是 internal page，比较 key，选择 child page_id，继续 fetch
如果是 leaf page，在 leaf 中找 key
找到就把 value 放入 result
```

这段流程要让学生理解的是：查找不是沿着指针走，而是不断把 `page_id` 交给 `BufferPoolManager`，再把拿到的 page data 解释成正确类型。

`Insert(key, value)` 的工程流程是：

```text
需要修改 header 或树节点，所以使用写 guard
如果是空树，申请一个新的 leaf page，并把 header.root_page_id_ 指向它
如果不是空树，先定位到目标 leaf page
在 leaf 中插入 key/value
如果 page 容量不够，就会产生新的 page，并且可能需要更新 parent 和 root
```

这里 README 不必详细教 split 怎么分数组。更重要的是提醒学生：只要结构发生变化，通常不只是当前 leaf 改了，parent 的 child 指向、root page id、叶子链表也可能一起变化。

`Remove(key)` 的工程流程是：

```text
先定位到目标 leaf page
如果 key 不存在，直接结束
如果 key 存在，从 leaf 中移除
如果删除导致结构需要调整，可能要更新相邻 leaf、parent 或 root
```

删除的算法细节可以留给课程内容或作业提示。README 里更应该强调“删除后哪些项目状态必须仍然一致”：root 是否正确、parent 是否还能导航、leaf 链表是否连续、size 是否可信。

`Begin()` 和 `Begin(key)` 的工程流程是：

```text
找到最左 leaf，或找到第一个可能包含 key 的 leaf
构造 IndexIterator
iterator 在当前 leaf 内移动
当前 leaf 结束后，通过 next_page_id_ fetch 下一个 leaf
```

这能解释为什么只让 `GetValue` 过还不够。点查正确不代表范围遍历正确，因为 iterator 依赖叶子层链表。

## 6. 学生主要看哪些文件

给学生的 README 里可以用文件地图替代大段代码粘贴。

| 文件 | 学生应该知道什么 |
| --- | --- |
| `src/include/storage/index/b_plus_tree.h` | `BPlusTree` 的公开接口、`Context`、成员变量 |
| `src/storage/index/b_plus_tree.cpp` | `Insert`、`Remove`、`GetValue`、`Begin` 的主要实现位置 |
| `src/include/storage/page/b_plus_tree_header_page.h` | header page 只负责保存 root page id |
| `src/include/storage/page/b_plus_tree_internal_page.h` | internal page 的 value 是 child page id |
| `src/include/storage/page/b_plus_tree_leaf_page.h` | leaf page 保存 key/value，并维护 next leaf |
| `src/include/storage/index/index_iterator.h` 和 `src/storage/index/index_iterator.cpp` | 范围扫描接口，依赖 leaf page 和 `next_page_id_` |
| `src/include/storage/page/page_guard.h` | guard 管 page 访问生命周期，重点看怎么用 |
| `src/include/buffer/buffer_pool_manager.h` | fetch/new page 的入口，通常当黑盒使用 |

这张表的重点是告诉学生“从哪里开始读”。不是所有文件都同等重要，也不是所有 BusTub 组件都要求学生理解内部实现。

## 7. 四个测试大致在看什么

README 里的测试部分也可以顺手解释每个测试的意义，这比只列命令更能帮助学生定位问题。

| 测试 | 主要覆盖 |
| --- | --- |
| `b_plus_tree_insert_test` | 插入、点查、基本树结构增长 |
| `b_plus_tree_delete_test` | 删除后结构仍然正确，root 和 page size 维护正确 |
| `b_plus_tree_concurrent_test` | 多线程读写下结构不被破坏 |
| `b_plus_tree_contention_test` | 并发压力和耗时场景，不能单独代表完整正确性 |

学生调试时可以按这个顺序理解：先让单线程插入和查找可靠，再处理删除，再考虑并发。并发测试失败时，不一定是比较逻辑错，也可能是 guard 生命周期、锁释放时机或结构调整期间的 page 访问顺序有问题。

## 8. 后续 README 可以怎样改

README 的“熟悉项目代码”部分建议按“项目地图 -> 使用规则 -> 操作流程 -> 文件表”的顺序写，而不是按头文件顺序贴实现。

比较适合学生的讲法是：

```text
第一步：告诉他们这个 project 是 page-based B+ tree，不是 Node* B+ tree。
第二步：解释 page_id、BufferPoolManager、PageGuard、As/AsMut 的关系。
第三步：解释 Header/Internal/Leaf 三种 page 在树里分别负责什么。
第四步：用 GetValue 讲一遍从 root 到 leaf 的工程流程。
第五步：用 Insert/Remove 提醒结构变化时要维护 root、parent、child、leaf link。
第六步：给文件地图和测试地图。
```

这样写的 README 会比原来更短，也更接近学生真正需要建立的心智模型。代码片段只保留最必要的三类：

```cpp
auto guard = bpm_->FetchPageRead(page_id);
auto page = guard.As<BPlusTreePage>();
```

```cpp
auto guard = bpm_->FetchPageWrite(page_id);
auto leaf = guard.AsMut<LeafPage>();
```

```cpp
auto new_guard = bpm_->NewPageGuarded(&new_page_id);
```

这几段足够说明学生如何从 `page_id` 到 typed page。其他长代码可以删掉，改成文字解释和表格。

## 9. 给助教自己的快速读法

如果你很久没看代码，又要尽快能给学生讲，可以按这个顺序重新读：

```text
先看 test/storage/b_plus_tree_insert_test.cpp
  观察测试怎样创建 BPlusTree，怎样调用 Insert/GetValue

再看 b_plus_tree.h
  只看公开接口、Context、成员变量

再看三个 b_plus_tree_*_page.h
  确认 header/internal/leaf 分别存什么

再看 page_guard.h
  确认 As、AsMut、Drop、读写 guard 的使用方式

最后看 b_plus_tree.cpp
  把每个函数放回上面的调用链里理解
```

读 `b_plus_tree.cpp` 时不要一开始陷进 split 或 merge 的细节。先问每个函数在工程上做了哪几件事：

```text
它从哪个 page_id 开始？
它拿的是读 guard 还是写 guard？
它把 page data 解释成哪种类型？
它修改了哪些跨 page 的关系？
它最后依赖 guard 自动释放哪些资源？
```

这五个问题比逐行读代码更能帮你恢复项目整体。

## 10. 一句话收束

这个 project 的主线不是“重新发明 B+ 树”，而是：

> 在 BusTub 的 page-based 存储模型里，实现一棵能被测试、遍历、删除和并发访问的 B+ tree index。

给学生讲 README 时，重点应该放在这套存储模型和代码入口上。B+ 树算法当然要实现，但那不是“熟悉项目代码”这一节最应该承担的内容。
