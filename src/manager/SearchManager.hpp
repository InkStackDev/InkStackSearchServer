/**
 * @file SearchManager.hpp
 * @brief 搜索索引管理器
 * @author Christins
 * @date 2026-11-19
 * @copyright Apache 2.0
 */
#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <chen/util/singleton.h>

#include "../index/SearchIndex.hpp"
#include "protocol_ss_search.h" // IWYU pragma: keep

namespace search {

/**
 * @brief 搜索索引管理器
 * @details 负责索引生命周期、查询、增量更新、全量重建与持久化。
 */
class SearchManager {
public:
    typedef std::shared_ptr<SearchManager> ptr;

    SearchManager() = default;

    /**
     * @brief 初始化索引与分词器
     */
    bool init();

    /**
     * @brief 每个 tick 调用：定时保存、清理超时 staging
     */
    void onTick(uint64_t now_ms);

    /**
     * @brief 保存 dirty 索引
     */
    bool flush();

    /**
     * @brief 查询文章
     */
    tagSearchResult query(const tagSearchQuery& query);

    /**
     * @brief 新增或更新文档
     */
    int32_t upsert(const tagSearchDoc& doc);

    /**
     * @brief 移除文档
     */
    int32_t remove(uint64_t article_id, int64_t update_time);

    /**
     * @brief 开始全量重建
     */
    int32_t beginRebuild(uint64_t snapshot_id, int64_t started_at_ms);

    /**
     * @brief 推送全量重建文档
     */
    int32_t rebuildDocs(uint64_t snapshot_id, const std::vector<tagSearchDoc>& docs);

    /**
     * @brief 提交全量重建
     */
    int32_t commitRebuild(uint64_t snapshot_id, int64_t finished_at_ms);

    /**
     * @brief 获取状态
     */
    tagSearchStats stats();

private:
    struct PendingOp {
        int32_t op = 0; // 1 upsert, 2 remove
        SearchDoc doc;
        int64_t update_time = 0;
    };

    static SearchDoc ToInternal(const tagSearchDoc& doc);

    bool saveLocked();

    void applyPending(SearchIndex& index);

private:
    std::mutex m_save_mutex;
    mutable std::shared_mutex m_mutex;
    SearchIndex::ptr m_index;
    SearchIndex::ptr m_staging;
    uint64_t m_snapshot_id = 0;
    uint64_t m_snapshot_start_ms = 0;
    std::unordered_map<uint64_t, PendingOp> m_pending;
    uint64_t m_last_save_ms = 0;
    uint64_t m_revision = 0;
    bool m_dirty = false;
};

typedef chen::LeakySingleton<SearchManager> SearchManagerMgr;

} // namespace search
