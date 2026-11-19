#include "SearchManager.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include <chen/config/config.h>
#include <chen/log/log.h>
#include <chen/util/time_util.h>

namespace search {

static chen::Logger::ptr logger = LOG_NAME("search");

static chen::ConfigVar<std::string>::ptr g_index_path =
    chen::Config::Lookup("search.index_path", std::string(""), "search index file path");
static chen::ConfigVar<std::string>::ptr g_jieba_dict_path =
    chen::Config::Lookup("search.jieba_dict_path", std::string(""), "jieba dict directory path");
static chen::ConfigVar<bool>::ptr g_rebuild_on_start =
    chen::Config::Lookup("search.rebuild_on_start", true, "discard index file on start and wait for snapshot");
static chen::ConfigVar<uint64_t>::ptr g_save_interval_ms =
    chen::Config::Lookup("search.save_interval_ms", (uint64_t)30000, "index save interval ms");
static chen::ConfigVar<uint64_t>::ptr g_rebuild_timeout_ms =
    chen::Config::Lookup("search.rebuild_timeout_ms", (uint64_t)600000, "rebuild staging timeout ms");
static chen::ConfigVar<int32_t>::ptr g_max_page_size =
    chen::Config::Lookup("search.max_page_size", (int32_t)100, "max search page size");

bool SearchManager::init()
{
    uint64_t now_ms = chen::TimeUtil::GetCurrentMs();
    std::string dict_path = g_jieba_dict_path->getValue();
    if (!SearchIndex::InitJieba(dict_path)) {
        WARN(logger) << "jieba unavailable, search will only match non-word filters";
    }

    auto index = std::make_shared<SearchIndex>();
    std::string index_path = g_index_path->getValue();

    bool loaded = false;
    if (!g_rebuild_on_start->getValue() && !index_path.empty()) {
        loaded = index->load(index_path);
        if (loaded) {
            index->setReady(true);
        }
    }

    std::unique_lock lock(m_mutex);
    m_index = index;
    m_staging.reset();
    m_pending.clear();
    m_snapshot_id = 0;
    m_snapshot_start_ms = 0;
    m_last_save_ms = now_ms;
    m_dirty = false;

    if (loaded) {
        INFO(logger) << "search index ready from file, docs=" << m_index->getDocCount()
                     << ", last_sync=" << m_index->getLastSyncTime();
    } else if (g_rebuild_on_start->getValue()) {
        INFO(logger) << "search index rebuild_on_start=true, waiting for InkStackServer snapshot";
    } else {
        WARN(logger) << "search index not loaded, waiting for InkStackServer snapshot, path=" << index_path;
    }
    return true;
}

void SearchManager::onTick(uint64_t now_ms)
{
    bool need_save = false;
    {
        std::unique_lock lock(m_mutex);
        if (m_staging && m_snapshot_start_ms > 0 && now_ms > m_snapshot_start_ms + g_rebuild_timeout_ms->getValue()) {
            WARN(logger) << "rebuild staging timeout, snapshot_id=" << m_snapshot_id;
            m_staging.reset();
            m_pending.clear();
            m_snapshot_id = 0;
            m_snapshot_start_ms = 0;
        }
        uint64_t interval = g_save_interval_ms->getValue();
        if (m_dirty && (m_last_save_ms == 0 || now_ms > m_last_save_ms + interval)) {
            need_save = true;
        }
    }
    if (need_save) {
        flush();
    }
}

bool SearchManager::flush()
{
    std::lock_guard save_lock(m_save_mutex);
    std::shared_lock lock(m_mutex);
    return saveLocked();
}

bool SearchManager::saveLocked()
{
    if (!m_index || !m_index->isReady() || !m_dirty) {
        return true;
    }
    std::string path = g_index_path->getValue();
    if (path.empty()) {
        WARN(logger) << "search.index_path is empty, skip save";
        return false;
    }
    if (!m_index->save(path)) {
        return false;
    }
    m_dirty = false;
    m_last_save_ms = chen::TimeUtil::GetCurrentMs();
    return true;
}

SearchDoc SearchManager::ToInternal(const tagSearchDoc& doc)
{
    SearchDoc out;
    out.article_id = doc.ArticleId;
    out.user_id = doc.UserId;
    out.state = doc.State;
    out.channel = doc.Channel;
    out.publish_time = doc.PublishTime;
    out.weight = doc.Weight;
    out.search_update_time = doc.SearchUpdateTime;
    out.title = doc.Title;
    out.content = doc.Content;
    out.category_ids = doc.CategoryIds;
    out.label_ids = doc.LabelIds;
    return out;
}

tagSearchResult SearchManager::query(const tagSearchQuery& query)
{
    tagSearchResult result;
    result.Code = SEARCH_OK;
    result.Total = 0;

    std::vector<uint64_t> ids;
    std::map<uint64_t, std::set<uint64_t>> params;
    for (auto id : query.CategoryIds) {
        params[static_cast<uint64_t>(IndexType::CAT_ID)].insert(id);
    }
    for (auto id : query.LabelIds) {
        params[static_cast<uint64_t>(IndexType::LABEL_ID)].insert(id);
    }
    if (query.State > 0) {
        params[static_cast<uint64_t>(IndexType::STATE)].insert(static_cast<uint64_t>(query.State));
    }
    if (query.Channel != 0) {
        params[static_cast<uint64_t>(IndexType::CHANNEL)].insert(static_cast<uint64_t>(query.Channel));
    }
    if (query.UserId != 0) {
        params[static_cast<uint64_t>(IndexType::USER_ID)].insert(static_cast<uint64_t>(query.UserId));
    }
    if (!query.YearMonth.empty()) {
        params[static_cast<uint64_t>(IndexType::YEAR_MON)].insert(SearchIndex::StrHash(query.YearMonth));
    }
    if (!query.Keyword.empty()) {
        std::vector<std::string> words;
        SearchIndex::CutWord(query.Keyword, words);
        auto& word_keys = params[static_cast<uint64_t>(IndexType::WORD)];
        for (auto& word : words) {
            if (word.size() >= 2) {
                word_keys.insert(SearchIndex::StrHash(word));
            }
        }
        if (word_keys.empty()) {
            return result;
        }
    }

    int32_t page = query.Page < 1 ? 1 : query.Page;
    int32_t page_size = query.PageSize < 1 ? 20 : query.PageSize;
    int32_t max_page_size = g_max_page_size->getValue();
    if (max_page_size > 0 && page_size > max_page_size) {
        page_size = max_page_size;
    }

    std::shared_lock lock(m_mutex);
    if (!m_index || !m_index->isReady()) {
        result.Code = SEARCH_NOT_READY;
        return result;
    }

    if (params.empty()) {
        return result;
    }

    int32_t total = 0;
    int32_t rt = m_index->search(ids, params, page, page_size, total);
    if (rt != 0) {
        result.Code = SEARCH_NOT_READY;
        return result;
    }

    result.Total = total;
    result.Ids = std::move(ids);
    return result;
}

int32_t SearchManager::upsert(const tagSearchDoc& doc)
{
    SearchDoc internal = ToInternal(doc);
    if (internal.article_id == 0) {
        return SEARCH_INVALID_QUERY;
    }

    std::unique_lock lock(m_mutex);
    if (!m_index) {
        return SEARCH_INTERNAL;
    }

    if (m_staging) {
        m_index->upsert(internal);
        PendingOp& pending = m_pending[internal.article_id];
        pending.op = 1;
        pending.doc = internal;
        pending.update_time = internal.search_update_time;
        ++m_revision;
        m_dirty = true;
        return SEARCH_OK;
    }

    if (!m_index->isReady()) {
        return SEARCH_NOT_READY;
    }

    int32_t rt = m_index->upsert(internal);
    if (rt >= 0) {
        ++m_revision;
        m_dirty = true;
    }
    return SEARCH_OK;
}

int32_t SearchManager::remove(uint64_t article_id, int64_t update_time)
{
    if (article_id == 0) {
        return SEARCH_INVALID_QUERY;
    }

    std::unique_lock lock(m_mutex);
    if (!m_index) {
        return SEARCH_INTERNAL;
    }

    if (m_staging) {
        m_index->remove(article_id, update_time);
        PendingOp& pending = m_pending[article_id];
        pending.op = 2;
        pending.doc = SearchDoc();
        pending.update_time = update_time;
        ++m_revision;
        m_dirty = true;
        return SEARCH_OK;
    }

    if (!m_index->isReady()) {
        return SEARCH_NOT_READY;
    }

    if (m_index->remove(article_id, update_time)) {
        ++m_revision;
        m_dirty = true;
    }
    return SEARCH_OK;
}

int32_t SearchManager::beginRebuild(uint64_t snapshot_id, int64_t started_at_ms)
{
    if (snapshot_id == 0) {
        return SEARCH_INVALID_QUERY;
    }

    auto staging = std::make_shared<SearchIndex>();
    std::unique_lock lock(m_mutex);
    if (m_staging) {
        WARN(logger) << "replacing existing staging index, old_snapshot_id=" << m_snapshot_id
                     << ", new_snapshot_id=" << snapshot_id;
    }
    m_staging = staging;
    m_snapshot_id = snapshot_id;
    // 使用本机时间做 staging 超时判断，避免客户端时钟偏差
    m_snapshot_start_ms = chen::TimeUtil::GetCurrentMs();
    (void)started_at_ms;
    m_pending.clear();
    INFO(logger) << "rebuild begin, snapshot_id=" << snapshot_id;
    return SEARCH_OK;
}

int32_t SearchManager::rebuildDocs(uint64_t snapshot_id, const std::vector<tagSearchDoc>& docs)
{
    std::unique_lock lock(m_mutex);
    if (!m_staging || m_snapshot_id != snapshot_id) {
        return SEARCH_BAD_SNAPSHOT;
    }

    for (auto& doc : docs) {
        SearchDoc internal = ToInternal(doc);
        if (internal.article_id == 0) {
            continue;
        }
        m_staging->upsert(internal);
    }
    return SEARCH_OK;
}

void SearchManager::applyPending(SearchIndex& index)
{
    for (auto& [article_id, pending] : m_pending) {
        if (pending.op == 1) {
            index.upsert(pending.doc);
        } else if (pending.op == 2) {
            index.remove(article_id, pending.update_time);
        }
    }
}

int32_t SearchManager::commitRebuild(uint64_t snapshot_id, int64_t finished_at_ms)
{
    {
        std::unique_lock lock(m_mutex);
        if (!m_staging || m_snapshot_id != snapshot_id) {
            return SEARCH_BAD_SNAPSHOT;
        }

        applyPending(*m_staging);
        m_staging->setLastSyncTime(finished_at_ms > 0 ? finished_at_ms : static_cast<int64_t>(chen::TimeUtil::GetCurrentMs()));
        m_staging->setReady(true);

        m_index = m_staging;
        m_staging.reset();
        m_pending.clear();
        m_snapshot_id = 0;
        m_snapshot_start_ms = 0;
        ++m_revision;
        m_dirty = true;

        INFO(logger) << "rebuild committed, docs=" << m_index->getDocCount()
                     << ", last_sync=" << m_index->getLastSyncTime();
    }

    // 不在 RPC 调用内同步落盘，避免大索引保存超过客户端超时；由 onTick 定时保存
    return SEARCH_OK;
}

tagSearchStats SearchManager::stats()
{
    tagSearchStats result;
    result.Code = SEARCH_OK;
    result.IndexVersion = SS_SEARCH_INDEX_VERSION;

    std::shared_lock lock(m_mutex);
    result.Ready = m_index && m_index->isReady();
    result.Rebuilding = m_staging != nullptr;
    result.DocCount = m_index ? m_index->getDocCount() : 0;
    result.LastSyncTime = m_index ? m_index->getLastSyncTime() : 0;
    return result;
}

} // namespace search
