#include "SearchModule.hpp"

#include <chen/log/log.h>

namespace search {

static chen::Logger::ptr logger = LOG_NAME("search");

SearchModule::SearchModule()
    : Module("SearchModule", "1.0.0", "libsearch.so")
{
}

uint64_t SearchModule::getTickIntervalMs()
{
    return 0;
}

bool SearchModule::onLoad()
{
    return true;
}

bool SearchModule::onUnload()
{
    return true;
}

bool SearchModule::onDeactivate()
{
    return true;
}

bool SearchModule::onServerReady()
{
    return true;
}

bool SearchModule::onServerUp()
{
    return true;
}

void SearchModule::onTick()
{

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
