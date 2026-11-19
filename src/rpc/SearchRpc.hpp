/**
 * @file SearchRpc.hpp
 * @brief 搜索服务 RPC 接口注册
 * @author Christins
 * @date 2026-11-19
 * @copyright Apache 2.0
 */
#pragma once

#include <chen/rpc/rpc_server.h>

namespace search {

void SearchRpcInit(chen::rpc::RpcServer::ptr server);

void SearchRpcUninit(chen::rpc::RpcServer::ptr server);

} // namespace search
