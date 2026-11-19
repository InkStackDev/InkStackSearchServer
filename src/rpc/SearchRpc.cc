#include "SearchRpc.hpp"

#include <vector>

#include <chen/log/log.h>

#include "../manager/SearchManager.hpp"

namespace search {

static chen::Logger::ptr logger = LOG_NAME("search");

static tagSearchResult HandleSearchArticles(const tagSearchQuery& query)
{
    return SearchManagerMgr::GetInstance()->query(query);
}

static int32_t HandleUpsertDoc(const tagSearchDoc& doc)
{
    int32_t rt = SearchManagerMgr::GetInstance()->upsert(doc);
    if (rt != SEARCH_OK) {
        WARN(logger) << "upsert doc failed, article_id=" << doc.ArticleId << ", code=" << rt;
    }
    return rt;
}

static int32_t HandleRemoveArticle(uint64_t article_id, int64_t update_time)
{
    return SearchManagerMgr::GetInstance()->remove(article_id, update_time);
}

static int32_t HandleRebuildBegin(uint64_t snapshot_id, int64_t started_at_ms)
{
    return SearchManagerMgr::GetInstance()->beginRebuild(snapshot_id, started_at_ms);
}

static int32_t HandleRebuildDocs(uint64_t snapshot_id, const std::vector<tagSearchDoc>& docs)
{
    int32_t rt = SearchManagerMgr::GetInstance()->rebuildDocs(snapshot_id, docs);
    if (rt == SEARCH_BAD_SNAPSHOT) {
        WARN(logger) << "rebuild docs rejected, snapshot_id=" << snapshot_id << ", docs=" << docs.size();
    }
    return rt;
}

static int32_t HandleRebuildCommit(uint64_t snapshot_id, int64_t finished_at_ms)
{
    return SearchManagerMgr::GetInstance()->commitRebuild(snapshot_id, finished_at_ms);
}

static tagSearchStats HandleGetStats()
{
    return SearchManagerMgr::GetInstance()->stats();
}

void SearchRpcInit(chen::rpc::RpcServer::ptr server)
{
    if (!server) {
        return;
    }
    server->registerMethod(SS_CMD_SEARCH_ARTICLES, HandleSearchArticles);
    server->registerMethod(SS_CMD_SEARCH_UPSERT_DOC, HandleUpsertDoc);
    server->registerMethod(SS_CMD_SEARCH_REMOVE_ARTICLE, HandleRemoveArticle);
    server->registerMethod(SS_CMD_SEARCH_REBUILD_BEGIN, HandleRebuildBegin);
    server->registerMethod(SS_CMD_SEARCH_REBUILD_DOCS, HandleRebuildDocs);
    server->registerMethod(SS_CMD_SEARCH_REBUILD_COMMIT, HandleRebuildCommit);
    server->registerMethod(SS_CMD_SEARCH_GET_STATS, HandleGetStats);
    INFO(logger) << "search RPC registered";
}

void SearchRpcUninit(chen::rpc::RpcServer::ptr server)
{
    if (!server) {
        return;
    }
    server->unregisterMethod(SS_CMD_SEARCH_ARTICLES);
    server->unregisterMethod(SS_CMD_SEARCH_UPSERT_DOC);
    server->unregisterMethod(SS_CMD_SEARCH_REMOVE_ARTICLE);
    server->unregisterMethod(SS_CMD_SEARCH_REBUILD_BEGIN);
    server->unregisterMethod(SS_CMD_SEARCH_REBUILD_DOCS);
    server->unregisterMethod(SS_CMD_SEARCH_REBUILD_COMMIT);
    server->unregisterMethod(SS_CMD_SEARCH_GET_STATS);
    INFO(logger) << "search RPC unregistered";
}

} // namespace search
