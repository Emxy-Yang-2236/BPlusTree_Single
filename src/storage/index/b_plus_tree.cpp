#include "storage/index/b_plus_tree.h"

#include <sstream>
#include <string>

#include "buffer/lru_k_replacer.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/logger.h"
#include "common/macros.h"
#include "common/rid.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_header_page.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"
#include "storage/page/page_guard.h"

namespace bustub
{

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id,
                          BufferPoolManager* buffer_pool_manager,
                          const KeyComparator& comparator, int leaf_max_size,
                          int internal_max_size)
    : index_name_(std::move(name)),
      bpm_(buffer_pool_manager),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id)
{
  WritePageGuard guard = bpm_ -> FetchPageWrite(header_page_id_);
  // In the original bpt, I fetch the header page
  // thus there's at least one page now
  auto root_header_page = guard.template AsMut<BPlusTreeHeaderPage>();
  // reinterprete the data of the page into "HeaderPage"
  root_header_page -> root_page_id_ = INVALID_PAGE_ID;
  // set the root_id to INVALID
}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const  ->  bool
{
  ReadPageGuard guard = bpm_ -> FetchPageRead(header_page_id_);
  auto root_header_page = guard.template As<BPlusTreeHeaderPage>();
  bool is_empty = root_header_page -> root_page_id_ == INVALID_PAGE_ID;
  // Just check if the root_page_id is INVALID
  // usage to fetch a page:
  // fetch the page guard   ->   call the "As" function of the page guard
  // to reinterprete the data of the page as "BPlusTreePage"
  return is_empty;
}
/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType& key,
                              std::vector<ValueType>* result, Transaction* txn)
     ->  bool
{
  //TODO : Your code here
  (void)txn;

  auto FindLeafPage = [this](const KeyType &key) -> page_id_t {
    ReadPageGuard header_guard = bpm_ -> FetchPageRead(header_page_id_);
    auto root_header_page = header_guard.template As<BPlusTreeHeaderPage>();
    auto current = root_header_page -> root_page_id_;

    if (current == INVALID_PAGE_ID) return INVALID_PAGE_ID;

    while (true)
    {
      auto guard = bpm_->FetchPageRead(current);
      if (const auto page = guard.As<BPlusTreePage>(); page->IsLeafPage())
      {
        return current;
      }

      auto internal = guard.As<InternalPage>();
      current = internal->ValueAt(BinaryFind(internal, key));
    }
  };

  auto leaf_page_id = FindLeafPage(key);
  if (leaf_page_id == INVALID_PAGE_ID)
  {
    return false;
  }

  auto leaf_guard = bpm_->FetchPageRead(leaf_page_id);
  auto leaf_page = leaf_guard.template As<LeafPage>();

  if (int index = BinaryFind(leaf_page, key);
      index >= 0 && comparator_(leaf_page->KeyAt(index), key) == 0)
  {
    result->push_back(leaf_page->ValueAt(index));
    return true;
  }
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/

/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType& key, const ValueType& value,
                            Transaction* txn)  ->  bool
{
  // TODO : Your code here
  (void)txn;

  Context ctx;

  ctx.header_page_ = bpm_->FetchPageWrite(header_page_id_);
  auto header = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
  ctx.root_page_id_ = header->root_page_id_;

  if (ctx.root_page_id_ == INVALID_PAGE_ID)
  {
    StartNewTree(key, value, header);
    return true;
  }

  page_id_t leaf_page_id = FindLeafForInsert(key, &ctx);
  // auto leaf_page_guard = bpm_->FetchPageWrite(leaf_page_id);
  // auto leaf = leaf_page_guard.template AsMut<LeafPage>();
  auto leaf = ctx.write_set_.back().template AsMut<LeafPage>();

  int index = BinaryFind(leaf, key);
  if (index >= 0 && comparator_(leaf->KeyAt(index), key) == 0)
  {
    return false;
  }

  if (leaf->GetSize() < leaf->GetMaxSize())
  {
    return InsertIntoLeafPage(leaf, key, value);
  }

  auto split_result = SplitLeafAndInsert(leaf_page_id, leaf, key, value);

  InsertIntoParent(&ctx,
                   leaf_page_id,
                   split_result.second,
                   split_result.first);

  return true;
}


INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::StartNewTree(const KeyType &key, const ValueType &value, BPlusTreeHeaderPage *header)
{
  page_id_t new_page_id;
  auto new_page_guard = bpm_->NewPageGuarded(&new_page_id);

  auto leaf_page = new_page_guard.template AsMut<LeafPage>();
  leaf_page->Init(leaf_max_size_);
  leaf_page->SetKeyAt(0, key);
  leaf_page->SetValueAt(0,value);
  leaf_page->SetSize(1);

  header -> root_page_id_ = new_page_id;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafForInsert(const KeyType &key, Context *ctx) ->page_id_t
{
  auto current = ctx->root_page_id_;
  while (true)
  {
    auto guard = bpm_->FetchPageWrite(current);
    ctx->write_set_.push_back(std::move(guard));
    auto page = ctx->write_set_.back().template AsMut<BPlusTreePage>();

    if (page->IsLeafPage()) {
      return current;
    }

    auto internal = ctx->write_set_.back().template AsMut<InternalPage>();
    current = internal->ValueAt(BinaryFind(internal, key));
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertIntoLeafPage(LeafPage *leaf_page, const KeyType &key, const ValueType &value) -> bool
{
  int size = leaf_page->GetSize();
  int index = BinaryFind(leaf_page, key);
  // if (index >= 0 && comparator_(leaf_page->KeyAt(index), key) == 0)
  // {
  //   return false;
  // }

  int pos = index + 1;
  for (int i = size - 1; i >= pos; i--)
  {
    leaf_page->SetAt(i + 1, leaf_page->KeyAt(i), leaf_page->ValueAt(i));
  }

  leaf_page->SetAt(pos, key, value);
  leaf_page->SetSize(size + 1);

  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::SplitLeafAndInsert(page_id_t old_leaf_page_id,
                          LeafPage *old_leaf_page,
                          const KeyType &key,
                          const ValueType &value) -> SplitResult
{
  std::vector<MappingType> temp;

  int old_size = old_leaf_page->GetSize();
  temp.reserve(old_size + 1);

  for (int i = 0; i < old_size; i++)
  {
    temp.emplace_back(old_leaf_page->KeyAt(i), old_leaf_page->ValueAt(i));
  }
  auto it = std::lower_bound(
      temp.begin(), temp.end(), key,
      [this](const MappingType &mapping, const KeyType &k) {
        return comparator_(mapping.first, k) < 0;
      });
  temp.insert(it, {key, value});

  page_id_t new_page_id;
  auto new_page_guard = bpm_->NewPageGuarded(&new_page_id);
  auto new_leaf_page = new_page_guard.template AsMut<LeafPage>();
  new_leaf_page->Init(leaf_max_size_);

  int total = static_cast<int>(temp.size());
  int left_size = total/2, right_size = total - left_size;

  for (int i = 0; i < left_size; ++i)
  {
    old_leaf_page->SetAt(i, temp[i].first, temp[i].second);
  }
  old_leaf_page->SetSize(left_size);
  for (int i = 0; i < right_size; ++i)
  {
    new_leaf_page->SetAt(i, temp[left_size+i].first, temp[left_size+i].second);
  }
  new_leaf_page->SetSize(right_size);

  new_leaf_page->SetNextPageId(old_leaf_page->GetNextPageId());
  old_leaf_page->SetNextPageId(new_page_id);

  return {new_page_id, new_leaf_page->KeyAt(0)};
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertIntoParent(Context *ctx,
                        page_id_t old_child_page_id,
                        const KeyType &separator_key,
                        page_id_t new_child_page_id)->void
{
  if (ctx->IsRootPage(old_child_page_id))
  {
    page_id_t new_page_id;
    auto new_root_guard = bpm_->NewPageGuarded(&new_page_id);
    auto new_root_page = new_root_guard.template AsMut<InternalPage>();
    new_root_page->Init(internal_max_size_);

    new_root_page->SetValueAt(0, old_child_page_id);
    new_root_page->SetKeyAt(1, separator_key);
    new_root_page->SetValueAt(1, new_child_page_id);
    new_root_page->SetSize(2);

    auto header = ctx->header_page_->template AsMut<BPlusTreeHeaderPage>();
    header->root_page_id_ = new_page_id;
    ctx->root_page_id_ = new_page_id;
    return;
  }

  if (ctx->write_set_.size() < 2)
  {
    throw Exception("InsertIntoParent: parent not found");
  }

  auto &parent_guard = ctx->write_set_[ctx->write_set_.size() - 2];
  auto parent_page_id = parent_guard.PageId();
  auto parent_page = parent_guard.template AsMut<InternalPage>();

  int old_idx = parent_page->ValueIndex(old_child_page_id);
  int insert_pos = old_idx + 1;

  int size = parent_page->GetSize();
  //若大小不超过限制，直接插入
  if (size < parent_page->GetMaxSize())
  {
    for (int i = size-1; i >= insert_pos; --i)
    {
      parent_page->SetKeyAt(i + 1, parent_page->KeyAt(i));
      parent_page->SetValueAt(i + 1, parent_page->ValueAt(i));
    }
    parent_page->SetKeyAt(insert_pos, separator_key);
    parent_page->SetValueAt(insert_pos, new_child_page_id);
    parent_page->SetSize(size + 1);
    return;
  }

  //若大小超过限制，则类似leaf节点分裂
  page_id_t new_page_id;
  auto new_page_guard = bpm_->NewPageGuarded(&new_page_id);
  auto new_page = new_page_guard.template AsMut<InternalPage>();
  new_page->Init(internal_max_size_);

  using InternalMappingType = std::pair<KeyType, page_id_t>;
  std::vector<InternalMappingType> temp;
  temp.reserve(size + 1);
  for (int i = 0; i < size; ++i)
  {
    temp.emplace_back(parent_page->KeyAt(i), parent_page->ValueAt(i));
  }
  temp.insert(temp.begin() + insert_pos, {separator_key, new_child_page_id});

  int total = static_cast<int>(temp.size());
  int left_size = total/2, right_size = total - left_size;

  for (int i = 0; i < left_size; ++i)
  {
    parent_page->SetKeyAt(i,temp[i].first);
    parent_page->SetValueAt(i,temp[i].second);
  }
  parent_page->SetSize(left_size);
  for (int i = 0; i < right_size; ++i)
  {
    if (i != 0)
    {
      new_page->SetKeyAt(i,temp[left_size+i].first);
    }
    new_page->SetValueAt(i,temp[left_size+i].second);
  }
  new_page->SetSize(right_size);

  ctx->write_set_.pop_back();
  InsertIntoParent(ctx, parent_page_id, temp[left_size].first, new_page_id);
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType& key, Transaction* txn)
{
  //TODO : Your code here
  (void)txn;
  Context ctx;

  ctx.header_page_ = bpm_->FetchPageWrite(header_page_id_);
  auto header = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
  ctx.root_page_id_ = header->root_page_id_;

  if (ctx.root_page_id_ == INVALID_PAGE_ID)
  {
    return;
  }

  page_id_t leaf_page_id = FindLeafForRemove(key, &ctx);
  auto leaf_page = ctx.write_set_.back().template AsMut<LeafPage>();

  int index = BinaryFind(leaf_page, key);
  if (index < 0 || comparator_(leaf_page->KeyAt(index), key) != 0)
  {
    return;
  }

  int leaf_size = leaf_page->GetSize();
  for (int i = index + 1; i < leaf_size; ++i)
  {
    leaf_page->SetAt(i-1, leaf_page->KeyAt(i), leaf_page->ValueAt(i));
  }
  leaf_page->SetSize(--leaf_size);

  if (leaf_size < leaf_page->GetMinSize())
  {
    HandleUnderflow(&ctx, leaf_page_id);
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafForRemove(const KeyType &key, Context *ctx) -> page_id_t
{
  auto current = ctx->root_page_id_;
  while (true)
  {
    auto guard = bpm_->FetchPageWrite(current);
    ctx->write_set_.push_back(std::move(guard));
    auto page = ctx->write_set_.back().template AsMut<BPlusTreePage>();

    if (page->IsLeafPage()) {
      return current;
    }

    auto internal = ctx->write_set_.back().template AsMut<InternalPage>();
    current = internal->ValueAt(BinaryFind(internal, key));
  }
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::HandleUnderflow(Context *ctx, page_id_t current_page_id)
{
  if (ctx->write_set_.back().PageId() != current_page_id)
  {
    throw Exception("Unmatch context and page_id in HandleUnderFlow");
  }

  //root特判
  if (ctx->IsRootPage(current_page_id))
  {
    auto root_page = ctx->write_set_.back().template AsMut<BPlusTreePage>();
    auto header = ctx->header_page_->template AsMut<BPlusTreeHeaderPage>();

    if (root_page->IsLeafPage()) {
      auto root_leaf = ctx->write_set_.back().template AsMut<LeafPage>();

      if (root_leaf->GetSize() == 0) {
        header->root_page_id_ = INVALID_PAGE_ID;
        ctx->root_page_id_ = INVALID_PAGE_ID;
      }
      return;
    }

    auto root_internal = ctx->write_set_.back().template AsMut<InternalPage>();
    if (root_internal->GetSize() == 1) {
      page_id_t new_root_id = root_internal->ValueAt(0);
      header->root_page_id_ = new_root_id;
      ctx->root_page_id_ = new_root_id;
    }

    return;
  }

  //if is leaf_page
  if (ctx->write_set_.back().As<BPlusTreePage>()->IsLeafPage())
  {
    auto leaf_page = ctx->write_set_.back().AsMut<LeafPage>();
    auto &parent_guard = ctx->write_set_[ctx->write_set_.size()-2];
    auto parent_page = parent_guard.AsMut<InternalPage>();
    auto parent_page_id = parent_guard.PageId();

    int child_idx = 0;
    for (; child_idx < parent_page->GetSize(); ++child_idx)
    {
      if (parent_page->ValueAt(child_idx) == current_page_id) break;
    }

    //try to borrow from right
    if (child_idx + 1 < parent_page->GetSize())
    {
      auto right_leaf_page_id = parent_page->ValueAt(child_idx + 1);
      auto right_leaf_page_guard = bpm_->FetchPageWrite(right_leaf_page_id);
      auto right_leaf_page = right_leaf_page_guard.template AsMut<LeafPage>();

      if (right_leaf_page->GetSize() > right_leaf_page->GetMinSize())
      {
        auto [borrow_key, borrow_val] =
          std::make_pair(right_leaf_page->KeyAt(0), right_leaf_page->ValueAt(0));
        parent_page->SetKeyAt(child_idx + 1, right_leaf_page->KeyAt(1));
        leaf_page->SetAt(leaf_page->GetSize(), borrow_key, borrow_val);

        for (int i = 1; i < right_leaf_page->GetSize(); ++i)
        {
          right_leaf_page->SetAt(i-1, right_leaf_page->KeyAt(i), right_leaf_page->ValueAt(i));
        }

        right_leaf_page->SetSize(right_leaf_page->GetSize()-1);
        leaf_page->SetSize(leaf_page->GetSize()+1);

        return;
      }
    }

    //try to borrow from left
    if (child_idx - 1 >= 0)
    {
      auto left_leaf_page_id = parent_page->ValueAt(child_idx - 1);
      auto left_leaf_page_guard = bpm_->FetchPageWrite(left_leaf_page_id);
      auto left_leaf_page = left_leaf_page_guard.template AsMut<LeafPage>();

      if (left_leaf_page->GetSize() > left_leaf_page->GetMinSize())
      {
        int left_size = left_leaf_page->GetSize(), leaf_size = leaf_page->GetSize();
        auto [borrow_key, borrow_val] = std::make_pair(left_leaf_page->KeyAt(left_size - 1), left_leaf_page->ValueAt(left_size - 1));
        parent_page->SetKeyAt(child_idx, borrow_key);
        left_leaf_page->SetSize(left_size - 1);

        for (int i = leaf_size - 1; i >= 0; --i) {
          leaf_page->SetAt(i + 1, leaf_page->KeyAt(i), leaf_page->ValueAt(i));
        }
        leaf_page->SetAt(0, borrow_key, borrow_val);

        leaf_page->SetSize(leaf_page->GetSize()+1);

        return;
      }
    }

    //try to merge from left
    if (child_idx - 1 >= 0)
    {
      auto left_leaf_page_id = parent_page->ValueAt(child_idx - 1);
      auto left_leaf_page_guard = bpm_->FetchPageWrite(left_leaf_page_id);
      auto left_leaf_page = left_leaf_page_guard.template AsMut<LeafPage>();

      int left_size = left_leaf_page->GetSize(), size = leaf_page->GetSize();
      for (int i = 0; i < size; ++i)
      {
        left_leaf_page->SetAt(left_size + i, leaf_page->KeyAt(i), leaf_page->ValueAt(i));
      }
      left_leaf_page->SetNextPageId(leaf_page->GetNextPageId());
      left_leaf_page->SetSize(left_size + size);
      leaf_page->SetSize(0);

      for (int i = child_idx; i < parent_page->GetSize()-1; ++i)
      {
        parent_page->SetKeyAt(i, parent_page->KeyAt(i+1));
        parent_page->SetValueAt(i, parent_page->ValueAt(i+1));
      }
      parent_page->SetSize(parent_page->GetSize() - 1);

      if (ctx->IsRootPage(parent_page_id) || parent_page->GetSize() < parent_page->GetMinSize()) {
        ctx->write_set_.pop_back();
        HandleUnderflow(ctx, parent_page_id);
      }

      return;
    }

    //try to merge from right
    if (child_idx + 1 < parent_page->GetSize())
    {
      auto right_leaf_page_id = parent_page->ValueAt(child_idx + 1);
      auto right_leaf_page_guard = bpm_->FetchPageWrite(right_leaf_page_id);
      auto right_leaf_page = right_leaf_page_guard.template AsMut<LeafPage>();

      int right_size = right_leaf_page->GetSize(), size = leaf_page->GetSize();
      for (int i = 0; i < right_size; ++i)
      {
        leaf_page->SetAt(size + i, right_leaf_page->KeyAt(i), right_leaf_page->ValueAt(i));
      }
      leaf_page->SetNextPageId(right_leaf_page->GetNextPageId());

      leaf_page->SetSize(right_size + size);
      right_leaf_page->SetSize(0);

      for (int i = child_idx+1; i < parent_page->GetSize()-1; ++i)
      {
        parent_page->SetKeyAt(i, parent_page->KeyAt(i+1));
        parent_page->SetValueAt(i, parent_page->ValueAt(i+1));
      }
      parent_page->SetSize(parent_page->GetSize() - 1);

      if (ctx->IsRootPage(parent_page_id) || parent_page->GetSize() < parent_page->GetMinSize()) {
        ctx->write_set_.pop_back();
        HandleUnderflow(ctx, parent_page_id);
      }

      return;
    }
  }

  //if is internal page
  auto internal_page = ctx->write_set_.back().AsMut<InternalPage>();
  auto &parent_guard = ctx->write_set_[ctx->write_set_.size()-2];
  auto parent_page = parent_guard.AsMut<InternalPage>();

  int child_idx = 0;
  for (; child_idx < parent_page->GetSize(); ++child_idx)
  {
    if (parent_page->ValueAt(child_idx) == current_page_id) break;
  }

  auto parent_page_id = parent_guard.PageId();

  //try to borrow from right
  if (child_idx + 1 < parent_page->GetSize())
  {
    auto right_internal_page_id = parent_page->ValueAt(child_idx + 1);
    auto right_internal_page_guard = bpm_->FetchPageWrite(right_internal_page_id);
    auto right_internal_page = right_internal_page_guard.template AsMut<InternalPage>();

    if (right_internal_page->GetSize() > right_internal_page->GetMinSize())
    {
      auto [borrow_key, borrow_val] =
        std::make_pair(parent_page->KeyAt(child_idx+1), right_internal_page->ValueAt(0));
      parent_page->SetKeyAt(child_idx+1, right_internal_page->KeyAt(1));

      int size = internal_page->GetSize();
      internal_page->SetValueAt(size,borrow_val);
      internal_page->SetKeyAt(size,borrow_key);

      right_internal_page->SetValueAt(0,right_internal_page->ValueAt(1));
      for (int i = 1; i < right_internal_page->GetSize()-1; ++i)
      {
        right_internal_page->SetValueAt(i, right_internal_page->ValueAt(i+1));
        right_internal_page->SetKeyAt(i, right_internal_page->KeyAt(i+1));
      }

      right_internal_page->SetSize(right_internal_page->GetSize()-1);
      internal_page->SetSize(internal_page->GetSize()+1);

      return;
    }
  }

  // 2. try to borrow from left internal sibling
  if (child_idx - 1 >= 0) {
    page_id_t left_internal_page_id = parent_page->ValueAt(child_idx - 1);
    auto left_internal_page_guard = bpm_->FetchPageWrite(left_internal_page_id);
    auto left_internal_page = left_internal_page_guard.template AsMut<InternalPage>();

    if (left_internal_page->GetSize() > left_internal_page->GetMinSize()) {
      int left_size = left_internal_page->GetSize();
      int cur_size = internal_page->GetSize();

      // left 最后一个 child 借给 current，变成 current 的最左 child
      page_id_t borrowed_child = left_internal_page->ValueAt(left_size - 1);

      // left 最后一个有效 key 上升到 parent
      KeyType new_parent_key = left_internal_page->KeyAt(left_size - 1);

      // parent 原 separator 下放到 current
      KeyType down_key = parent_page->KeyAt(child_idx);

      // current 整体右移一格。
      for (int i = cur_size - 1; i >= 1; --i) {
        internal_page->SetKeyAt(i + 1, internal_page->KeyAt(i));
        internal_page->SetValueAt(i + 1, internal_page->ValueAt(i));
      }

      internal_page->SetKeyAt(1, down_key);
      internal_page->SetValueAt(1, internal_page->ValueAt(0));
      internal_page->SetValueAt(0, borrowed_child);
      internal_page->SetSize(cur_size + 1);

      left_internal_page->SetSize(left_size - 1);
      parent_page->SetKeyAt(child_idx, new_parent_key);

      return;
    }
  }

  // =========================
  // 3. merge current into left internal sibling if left exists
  // =========================
  if (child_idx - 1 >= 0) {
    page_id_t left_internal_page_id = parent_page->ValueAt(child_idx - 1);
    auto left_internal_page_guard = bpm_->FetchPageWrite(left_internal_page_id);
    auto left_internal_page = left_internal_page_guard.template AsMut<InternalPage>();

    int left_size = left_internal_page->GetSize();
    int cur_size = internal_page->GetSize();

    // parent separator 下沉到 left，连接 current 的 ValueAt(0)
    KeyType down_key = parent_page->KeyAt(child_idx);

    left_internal_page->SetKeyAt(left_size, down_key);
    left_internal_page->SetValueAt(left_size, internal_page->ValueAt(0));

    // current 的剩余有效 key/value 追加到 left 后面
    for (int i = 1; i < cur_size; ++i) {
      left_internal_page->SetKeyAt(left_size + i, internal_page->KeyAt(i));
      left_internal_page->SetValueAt(left_size + i, internal_page->ValueAt(i));
    }

    left_internal_page->SetSize(left_size + cur_size);
    internal_page->SetSize(0);

    // 从 parent 删除 current 这个 child entry，也就是 child_idx
    int parent_size = parent_page->GetSize();
    for (int i = child_idx + 1; i < parent_size; ++i) {
      if (i - 1 != 0) {
        parent_page->SetKeyAt(i - 1, parent_page->KeyAt(i));
      }
      parent_page->SetValueAt(i - 1, parent_page->ValueAt(i));
    }
    parent_page->SetSize(parent_size - 1);

    // merge 后 parent 可能下溢；如果 parent 是 root，也要让 root 特判执行，从而降低树高
    if (ctx->IsRootPage(parent_page_id) || parent_page->GetSize() < parent_page->GetMinSize()) {
      ctx->write_set_.pop_back();
      HandleUnderflow(ctx, parent_page_id);
    }

    return;
  }

  // =========================
  // 4. otherwise merge right internal sibling into current
  // =========================
  if (child_idx + 1 < parent_page->GetSize()) {
    page_id_t right_internal_page_id = parent_page->ValueAt(child_idx + 1);
    auto right_internal_page_guard = bpm_->FetchPageWrite(right_internal_page_id);
    auto right_internal_page = right_internal_page_guard.template AsMut<InternalPage>();

    int cur_size = internal_page->GetSize();
    int right_size = right_internal_page->GetSize();

    // parent separator 下沉到 current，连接 right 的 ValueAt(0)
    KeyType down_key = parent_page->KeyAt(child_idx + 1);

    internal_page->SetKeyAt(cur_size, down_key);
    internal_page->SetValueAt(cur_size, right_internal_page->ValueAt(0));

    // right 的剩余有效 key/value 追加到 current 后面
    for (int i = 1; i < right_size; ++i) {
      internal_page->SetKeyAt(cur_size + i, right_internal_page->KeyAt(i));
      internal_page->SetValueAt(cur_size + i, right_internal_page->ValueAt(i));
    }

    internal_page->SetSize(cur_size + right_size);
    right_internal_page->SetSize(0);

    // 从 parent 删除 right 这个 child entry，也就是 child_idx + 1
    int parent_size = parent_page->GetSize();
    for (int i = child_idx + 2; i < parent_size; ++i) {
      if (i - 1 != 0) {
        parent_page->SetKeyAt(i - 1, parent_page->KeyAt(i));
      }
      parent_page->SetValueAt(i - 1, parent_page->ValueAt(i));
    }
    parent_page->SetSize(parent_size - 1);

    if (ctx->IsRootPage(parent_page_id) || parent_page->GetSize() < parent_page->GetMinSize()) {
      ctx->write_set_.pop_back();
      HandleUnderflow(ctx, parent_page_id);
    }

    return;
  }

  throw Exception("HandleUnderflow: internal page has neither left nor right sibling");
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/


INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BinaryFind(const LeafPage* leaf_page, const KeyType& key)
     ->  int
{
  int l = 0;
  int r = leaf_page -> GetSize() - 1;
  while (l < r)
  {
    int mid = (l + r + 1) >> 1;
    if (comparator_(leaf_page -> KeyAt(mid), key) != 1)
    {
      l = mid;
    }
    else
    {
      r = mid - 1;
    }
  }

  if (r >= 0 && comparator_(leaf_page -> KeyAt(r), key) == 1)
  {
    r = -1;
  }

  return r;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BinaryFind(const InternalPage* internal_page,
                                const KeyType& key)  ->  int
{
  int l = 1;
  int r = internal_page -> GetSize() - 1;
  while (l < r)
  {
    int mid = (l + r + 1) >> 1;
    if (comparator_(internal_page -> KeyAt(mid), key) != 1)
    {
      l = mid;
    }
    else
    {
      r = mid - 1;
    }
  }

  if (r == -1 || comparator_(internal_page -> KeyAt(r), key) == 1)
  {
    r = 0;
  }

  return r;
}

/*
 * Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin()  ->  INDEXITERATOR_TYPE
//Just go left forever
{
  ReadPageGuard head_guard = bpm_ -> FetchPageRead(header_page_id_);
  if (head_guard.template As<BPlusTreeHeaderPage>() -> root_page_id_ == INVALID_PAGE_ID)
  {
    return End();
  }
  ReadPageGuard guard = bpm_ -> FetchPageRead(head_guard.As<BPlusTreeHeaderPage>() -> root_page_id_);
  head_guard.Drop();

  auto tmp_page = guard.template As<BPlusTreePage>();
  while (!tmp_page -> IsLeafPage())
  {
    int slot_num = 0;
    guard = bpm_ -> FetchPageRead(reinterpret_cast<const InternalPage*>(tmp_page) -> ValueAt(slot_num));
    tmp_page = guard.template As<BPlusTreePage>();
  }
  int slot_num = 0;
  if (slot_num != -1)
  {
    return INDEXITERATOR_TYPE(bpm_, guard.PageId(), 0);
  }
  return End();
}


/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType& key)  ->  INDEXITERATOR_TYPE
{
  ReadPageGuard head_guard = bpm_ -> FetchPageRead(header_page_id_);

  if (head_guard.template As<BPlusTreeHeaderPage>() -> root_page_id_ == INVALID_PAGE_ID)
  {
    return End();
  }
  ReadPageGuard guard = bpm_ -> FetchPageRead(head_guard.As<BPlusTreeHeaderPage>() -> root_page_id_);
  head_guard.Drop();
  auto tmp_page = guard.template As<BPlusTreePage>();
  while (!tmp_page -> IsLeafPage())
  {
    auto internal = reinterpret_cast<const InternalPage*>(tmp_page);
    int slot_num = BinaryFind(internal, key);
    if (slot_num == -1)
    {
      return End();
    }
    guard = bpm_ -> FetchPageRead(reinterpret_cast<const InternalPage*>(tmp_page) -> ValueAt(slot_num));
    tmp_page = guard.template As<BPlusTreePage>();
  }
  auto* leaf_page = reinterpret_cast<const LeafPage*>(tmp_page);

  //initial ver
  // int slot_num = BinaryFind(leaf_page, key);
  // if (slot_num != -1)
  // {
  //   return INDEXITERATOR_TYPE(bpm_, guard.PageId(), slot_num);
  // }
  // return End();

  int pred = BinaryFind(leaf_page, key);

  int slot_num;
  if (pred >= 0 && comparator_(leaf_page->KeyAt(pred), key) == 0) {
    slot_num = pred;
  } else {
    slot_num = pred + 1;
  }

  // 如果当前 leaf 中没有 >= key 的元素，就跳到 next leaf
  while (slot_num >= leaf_page->GetSize()) {
    page_id_t next_page_id = leaf_page->GetNextPageId();

    if (next_page_id == INVALID_PAGE_ID) {
      return End();
    }

    guard = bpm_->FetchPageRead(next_page_id);
    leaf_page = guard.template As<LeafPage>();
    slot_num = 0;

    if (leaf_page->GetSize() > 0) {
      break;
    }
  }

  return INDEXITERATOR_TYPE(bpm_, guard.PageId(), slot_num);
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End()  ->  INDEXITERATOR_TYPE
{
  return INDEXITERATOR_TYPE(bpm_, -1, -1);
}

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId()  ->  page_id_t
{
  ReadPageGuard guard = bpm_ -> FetchPageRead(header_page_id_);
  auto root_header_page = guard.template As<BPlusTreeHeaderPage>();
  page_id_t root_page_id = root_header_page -> root_page_id_;
  return root_page_id;
}

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/

/*
 * This method is used for test only
 * Read data from file and insert one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertFromFile(const std::string& file_name,
                                    Transaction* txn)
{
  int64_t key;
  std::ifstream input(file_name);
  while (input >> key)
  {
    KeyType index_key;
    index_key.SetFromInteger(key);
    RID rid(key);
    Insert(index_key, rid, txn);
  }
}
/*
 * This method is used for test only
 * Read data from file and remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromFile(const std::string& file_name,
                                    Transaction* txn)
{
  int64_t key;
  std::ifstream input(file_name);
  while (input >> key)
  {
    KeyType index_key;
    index_key.SetFromInteger(key);
    Remove(index_key, txn);
  }
}

/*
 * This method is used for test only
 * Read data from file and insert/remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::BatchOpsFromFile(const std::string& file_name,
                                      Transaction* txn)
{
  int64_t key;
  char instruction;
  std::ifstream input(file_name);
  while (input)
  {
    input >> instruction >> key;
    RID rid(key);
    KeyType index_key;
    index_key.SetFromInteger(key);
    switch (instruction)
    {
      case 'i':
        Insert(index_key, rid, txn);
        break;
      case 'd':
        Remove(index_key, txn);
        break;
      default:
        break;
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Print(BufferPoolManager* bpm)
{
  auto root_page_id = GetRootPageId();
  auto guard = bpm -> FetchPageBasic(root_page_id);
  PrintTree(guard.PageId(), guard.template As<BPlusTreePage>());
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::PrintTree(page_id_t page_id, const BPlusTreePage* page)
{
  if (page -> IsLeafPage())
  {
    auto* leaf = reinterpret_cast<const LeafPage*>(page);
    std::cout << "Leaf Page: " << page_id << "\tNext: " << leaf -> GetNextPageId() << std::endl;

    // Print the contents of the leaf page.
    std::cout << "Contents: ";
    for (int i = 0; i < leaf -> GetSize(); i++)
    {
      std::cout << leaf -> KeyAt(i);
      if ((i + 1) < leaf -> GetSize())
      {
        std::cout << ", ";
      }
    }
    std::cout << std::endl;
    std::cout << std::endl;
  }
  else
  {
    auto* internal = reinterpret_cast<const InternalPage*>(page);
    std::cout << "Internal Page: " << page_id << std::endl;

    // Print the contents of the internal page.
    std::cout << "Contents: ";
    for (int i = 0; i < internal -> GetSize(); i++)
    {
      std::cout << internal -> KeyAt(i) << ": " << internal -> ValueAt(i);
      if ((i + 1) < internal -> GetSize())
      {
        std::cout << ", ";
      }
    }
    std::cout << std::endl;
    std::cout << std::endl;
    for (int i = 0; i < internal -> GetSize(); i++)
    {
      auto guard = bpm_ -> FetchPageBasic(internal -> ValueAt(i));
      PrintTree(guard.PageId(), guard.template As<BPlusTreePage>());
    }
  }
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Draw(BufferPoolManager* bpm, const std::string& outf)
{
  if (IsEmpty())
  {
    LOG_WARN("Drawing an empty tree");
    return;
  }

  std::ofstream out(outf);
  out << "digraph G {" << std::endl;
  auto root_page_id = GetRootPageId();
  auto guard = bpm -> FetchPageBasic(root_page_id);
  ToGraph(guard.PageId(), guard.template As<BPlusTreePage>(), out);
  out << "}" << std::endl;
  out.close();
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToGraph(page_id_t page_id, const BPlusTreePage* page,
                             std::ofstream& out)
{
  std::string leaf_prefix("LEAF_");
  std::string internal_prefix("INT_");
  if (page -> IsLeafPage())
  {
    auto* leaf = reinterpret_cast<const LeafPage*>(page);
    // Print node name
    out << leaf_prefix << page_id;
    // Print node properties
    out << "[shape=plain color=green ";
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" "
           "CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << leaf -> GetSize() << "\">P=" << page_id
        << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << leaf -> GetSize() << "\">"
        << "max_size=" << leaf -> GetMaxSize()
        << ",min_size=" << leaf -> GetMinSize() << ",size=" << leaf -> GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < leaf -> GetSize(); i++)
    {
      out << "<TD>" << leaf -> KeyAt(i) << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Leaf node link if there is a next page
    if (leaf -> GetNextPageId() != INVALID_PAGE_ID)
    {
      out << leaf_prefix << page_id << "   ->   " << leaf_prefix
          << leaf -> GetNextPageId() << ";\n";
      out << "{rank=same " << leaf_prefix << page_id << " " << leaf_prefix
          << leaf -> GetNextPageId() << "};\n";
    }
  }
  else
  {
    auto* inner = reinterpret_cast<const InternalPage*>(page);
    // Print node name
    out << internal_prefix << page_id;
    // Print node properties
    out << "[shape=plain color=pink ";  // why not?
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" "
           "CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << inner -> GetSize() << "\">P=" << page_id
        << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << inner -> GetSize() << "\">"
        << "max_size=" << inner -> GetMaxSize()
        << ",min_size=" << inner -> GetMinSize() << ",size=" << inner -> GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < inner -> GetSize(); i++)
    {
      out << "<TD PORT=\"p" << inner -> ValueAt(i) << "\">";
      // if (i > 0) {
      out << inner -> KeyAt(i) << "  " << inner -> ValueAt(i);
      // } else {
      // out << inner  ->  ValueAt(0);
      // }
      out << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print leaves
    for (int i = 0; i < inner -> GetSize(); i++)
    {
      auto child_guard = bpm_ -> FetchPageBasic(inner -> ValueAt(i));
      auto child_page = child_guard.template As<BPlusTreePage>();
      ToGraph(child_guard.PageId(), child_page, out);
      if (i > 0)
      {
        auto sibling_guard = bpm_ -> FetchPageBasic(inner -> ValueAt(i - 1));
        auto sibling_page = sibling_guard.template As<BPlusTreePage>();
        if (!sibling_page -> IsLeafPage() && !child_page -> IsLeafPage())
        {
          out << "{rank=same " << internal_prefix << sibling_guard.PageId()
              << " " << internal_prefix << child_guard.PageId() << "};\n";
        }
      }
      out << internal_prefix << page_id << ":p" << child_guard.PageId()
          << "   ->   ";
      if (child_page -> IsLeafPage())
      {
        out << leaf_prefix << child_guard.PageId() << ";\n";
      }
      else
      {
        out << internal_prefix << child_guard.PageId() << ";\n";
      }
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::DrawBPlusTree()  ->  std::string
{
  if (IsEmpty())
  {
    return "()";
  }

  PrintableBPlusTree p_root = ToPrintableBPlusTree(GetRootPageId());
  std::ostringstream out_buf;
  p_root.Print(out_buf);

  return out_buf.str();
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::ToPrintableBPlusTree(page_id_t root_id)
     ->  PrintableBPlusTree
{
  auto root_page_guard = bpm_ -> FetchPageBasic(root_id);
  auto root_page = root_page_guard.template As<BPlusTreePage>();
  PrintableBPlusTree proot;

  if (root_page -> IsLeafPage())
  {
    auto leaf_page = root_page_guard.template As<LeafPage>();
    proot.keys_ = leaf_page -> ToString();
    proot.size_ = proot.keys_.size() + 4;  // 4 more spaces for indent

    return proot;
  }

  // draw internal page
  auto internal_page = root_page_guard.template As<InternalPage>();
  proot.keys_ = internal_page -> ToString();
  proot.size_ = 0;
  for (int i = 0; i < internal_page -> GetSize(); i++)
  {
    page_id_t child_id = internal_page -> ValueAt(i);
    PrintableBPlusTree child_node = ToPrintableBPlusTree(child_id);
    proot.size_ += child_node.size_;
    proot.children_.push_back(child_node);
  }

  return proot;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub