#include "SearchModule.hpp"

#include <vector>

#include <chen/log/log.h>
#include <chen/util/time_util.h>

#include "manager/SearchManager.hpp"
#include "rpc/SearchRpc.hpp"

namespace search {

static chen::Logger::ptr logger = LOG_NAME("search");

SearchModule::SearchModule()
    : Module("SearchModule", "1.0.0", "libsearch.so")
{
}

uint64_t SearchModule::getTickIntervalMs()
{
    return 1000;
}

bool SearchModule::onLoad()
{
    INFO(logger) << "SearchModule::onLoad";
    return true;
}

bool SearchModule::onUnload()
{
    INFO(logger) << "SearchModule::onUnload";

    std::vector<chen::rpc::RpcServer::ptr> rpc_servers;
    getAllRpcServer(rpc_servers);
    for (auto& server : rpc_servers) {
        SearchRpcUninit(server);
    }

    SearchManagerMgr::GetInstance()->flush();
    return true;
}

bool SearchModule::onDeactivate()
{
    INFO(logger) << "SearchModule::onDeactivate";
    SearchManagerMgr::GetInstance()->flush();
    return true;
}

bool SearchModule::onServerReady()
{
    INFO(logger) << "SearchModule::onServerReady";

    SearchManagerMgr::GetInstance()->init();

    std::vector<chen::rpc::RpcServer::ptr> rpc_servers;
    getAllRpcServer(rpc_servers);
    if (rpc_servers.empty()) {
        ERROR(logger) << "No RPC server found, search service unavailable";
        return false;
    }

    for (auto& server : rpc_servers) {
        SearchRpcInit(server);
    }

    auto stats = SearchManagerMgr::GetInstance()->stats();
    INFO(logger) << "search service ready, ready=" << stats.Ready << " rebuilding=" << stats.Rebuilding
                 << " docs=" << stats.DocCount << " version=" << stats.IndexVersion;
    return true;
}

bool SearchModule::onServerUp()
{
    INFO(logger) << "SearchModule::onServerUp";
    return true;
}

void SearchModule::onTick()
{
    SearchManagerMgr::GetInstance()->onTick(chen::TimeUtil::GetCurrentMs());
}

} // namespace search


extern "C" {

chen::Module* CreateModule()
{
    chen::Module* module = new search::SearchModule;
    INFO(search::logger) << "SearchModule::CreateModule";
    return module;
}

void DestroyModule(const chen::Module* module)
{
    INFO(search::logger) << "SearchModule::DestroyModule";
    delete module;
}

}
