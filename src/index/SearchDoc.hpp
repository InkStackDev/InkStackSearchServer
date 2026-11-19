/**
 * @file SearchDoc.hpp
 * @brief 搜索索引内部文档结构
 * @author Christins
 * @date 2026-11-19
 * @copyright Apache 2.0
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace search {

/**
 * @brief 索引内部文档
 * @details 与 RPC 协议结构解耦，避免索引层依赖协议头文件。
 */
struct SearchDoc {
    uint64_t article_id = 0;
    int64_t user_id = 0;
    int32_t state = 0;
    int64_t channel = 0;
    int64_t publish_time = 0;
    int64_t weight = 0;
    int64_t search_update_time = 0;
    std::string title;
    std::string content;
    std::vector<uint64_t> category_ids;
    std::vector<uint64_t> label_ids;
};

} // namespace search
