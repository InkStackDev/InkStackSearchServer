/**
 * @file SearchIndex.hpp
 * @brief 基于位图的文章搜索索引
 * @author Christins
 * @date 2026-11-19
 * @copyright Apache 2.0
 */
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <chen/ds/bitmap.h>

#include "SearchDoc.hpp"

namespace search {

/**
 * @brief 索引类型
 */
enum class IndexType : uint64_t {
    USER_ID = 1,
    CAT_ID = 2,
    LABEL_ID = 3,
    STATE = 6,
    YEAR_MON = 7,
    CHANNEL = 8,
    WORD = 100
};

/**
 * @brief 内存位图索引
 * @details 本类不做加锁，调用方（SearchManager）负责并发控制。
 */
class SearchIndex {
public:
    typedef std::shared_ptr<SearchIndex> ptr;

    SearchIndex();
    ~SearchIndex();

    /**
     * @brief 初始化全局 jieba 分词器（进程内只初始化一次）
     * @param dict_path 词典目录
     * @return bool 是否成功
     */
    static bool InitJieba(const std::string& dict_path);

    /**
     * @brief 分词器是否已就绪
     */
    static bool HasJieba();

    /**
     * @brief 对输入做中文分词
     */
    static void CutWord(const std::string& str, std::vector<std::string>& words);

    /**
     * @brief 新增或更新文档
     * @return 1 已应用，0 忽略（旧版本），-1 参数错误
     */
    int32_t upsert(const SearchDoc& doc);

    /**
     * @brief 移除文档
     * @param article_id 文章 ID
     * @param update_time 本次操作时间（毫秒），用于拒绝过期删除
     * @return bool 是否实际移除
     */
    bool remove(uint64_t article_id, int64_t update_time);

    /**
     * @brief 文档是否存在
     */
    bool has(uint64_t article_id) const;

    /**
     * @brief 获取文档当前更新时间
     */
    int64_t getUpdateTime(uint64_t article_id) const;

    /**
     * @brief 多条件搜索
     * @param ids 返回当前页文章 ID
     * @param params 过滤条件：类型 -> 值集合
     * @param page 页码，从 1 开始
     * @param page_size 每页数量
     * @param total 命中总数
     * @return 0 成功，-1 索引未就绪
     */
    int32_t search(
        std::vector<uint64_t>& ids,
        const std::map<uint64_t, std::set<uint64_t>>& params,
        int32_t page,
        int32_t page_size,
        int32_t& total);

    /**
     * @brief 持久化到文件
     */
    bool save(const std::string& path);

    /**
     * @brief 从文件加载
     */
    bool load(const std::string& path);

    /**
     * @brief 清空索引
     */
    void clear();

    bool isReady() const { return m_ready.load(); }
    void setReady(bool v) { m_ready.store(v); }

    int64_t getLastSyncTime() const { return m_lastSyncTime; }
    void setLastSyncTime(int64_t v) { m_lastSyncTime = v; }

    size_t getDocCount() const { return m_docMap.size(); }

    static uint64_t StrHash(const std::string& str);

private:
    struct DocMeta {
        uint64_t article_id = 0;
        int64_t weight = 0;
        int64_t publish_time = 0;
        int64_t update_time = 0;
    };

    struct Candidate {
        int64_t weight = 0;
        uint64_t article_id = 0;
    };

    bool set(uint64_t type, uint64_t key, uint32_t idx, bool v);

    chen::ds::Bitmap::ptr get(uint64_t type, uint64_t key);

    chen::ds::Bitmap::ptr query(const std::map<uint64_t, std::set<uint64_t>>& params);

    void buildIdx(const SearchDoc& doc, uint32_t idx);

    void buildWordIdx(const std::string& str, uint32_t idx);

    void ensureSize(size_t size);

    void clearSlot(uint32_t idx);

    int64_t getIdx(uint64_t article_id) const;

    uint64_t hash(const std::string& str, bool save);

private:
    std::vector<DocMeta> m_metas;
    std::unordered_map<uint64_t, uint32_t> m_docMap;
    std::vector<uint32_t> m_free;
    std::map<uint64_t, std::map<uint64_t, chen::ds::Bitmap::ptr>> m_indexs;
    std::unordered_map<uint64_t, std::string> m_strings;
    int64_t m_lastSyncTime = 0;
    std::atomic<bool> m_ready{false};
};

} // namespace search
