/**
 * @file SearchModule.hpp
 * @brief InkStackSearchServer 模块文件
 * @author Christins (chen.christins@qq.com)
 * @date 2026-11-19
 * @copyright Apache 2.0
 */
#pragma once

#include <chen/module/module.h>

namespace search {

class SearchModule : public chen::Module {
public:
    /**
     * @brief 构造函数
     */
    SearchModule();

    /**
     * @brief 模块加载时调用
     * @return bool 是否成功
     */
    bool onLoad() override;

    /**
     * @brief 模块卸载时调用
     * @return bool 是否成功
     */
    bool onUnload() override;

    /**
     * @brief 服务器准备就绪时调用
     * @return bool 是否成功
     */
    bool onServerReady() override;

    /**
     * @brief 服务器启动完成时调用
     * @return bool 是否成功
     */
    bool onServerUp() override;

    /**
     * @brief 模块停用：旧模块被替换时调用（热重载）
     * @details 新模块已接管，旧模块停止接收新请求。
     *          用于关闭 WebSocket 连接等长连接。默认实现返回 true。
     * @return bool
     */
    bool onDeactivate() override;

    /**
     * @brief 模块每个 Tick 调用一次
     */
    void onTick() override;

    /**
     * @brief 获取 Tick 间隔时间（毫秒）默认 0 表示不使用 Tick
     * @return uint64_t
     */
    uint64_t getTickIntervalMs() override;
};

} // namespace search