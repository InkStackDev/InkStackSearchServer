#include "SearchIndex.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <mutex>

#include <chen/log/log.h>
#include <chen/util/fs_util.h>
#include <chen/util/type_util.h>
#include <chen/util/util.h>
#include <cppjieba/MixSegment.hpp>

namespace search {

static chen::Logger::ptr logger = LOG_NAME("search");

/// 索引文件版本。数据结构/内容口径变化时递增，旧文件会被丢弃并重建
static const uint32_t kIndexFileVersion = 3;
static const uint32_t kIndexFileMagic = 0x42494458; // 'BIDX'

static std::mutex g_jiebaMutex;
static std::unique_ptr<cppjieba::MixSegment> g_jieba;
static std::atomic<bool> g_jiebaReady{false};

SearchIndex::SearchIndex() = default;

SearchIndex::~SearchIndex() = default;

bool SearchIndex::InitJieba(const std::string& dict_path)
{
    std::lock_guard<std::mutex> lock(g_jiebaMutex);
    if (g_jieba) {
        return true;
    }
    if (dict_path.empty()) {
        WARN(logger) << "jieba dict path is empty";
        return false;
    }

    try {
        g_jieba.reset(new cppjieba::MixSegment(
            dict_path + "/jieba.dict.utf8",
            dict_path + "/hmm_model.utf8",
            dict_path + "/user.dict.utf8"));
        g_jiebaReady.store(true);
        INFO(logger) << "jieba initialized, dict_path=" << dict_path;
        return true;
    } catch (const std::exception& e) {
        ERROR(logger) << "jieba init failed: " << e.what() << ", dict_path=" << dict_path;
        g_jieba.reset();
        g_jiebaReady.store(false);
        return false;
    }
}

bool SearchIndex::HasJieba()
{
    return g_jiebaReady.load();
}

void SearchIndex::CutWord(const std::string& str, std::vector<std::string>& words)
{
    if (str.empty() || !g_jiebaReady.load() || !g_jieba) {
        return;
    }
    g_jieba->Cut(str, words, true);
}

void SearchIndex::clear()
{
    m_metas.clear();
    m_docMap.clear();
    m_free.clear();
    m_indexs.clear();
    m_strings.clear();
    m_lastSyncTime = 0;
    m_ready.store(false);
}

bool SearchIndex::set(uint64_t type, uint64_t key, uint32_t idx, bool v)
{
    auto& key_map = m_indexs[type];
    auto it = key_map.find(key);
    chen::ds::Bitmap::ptr bitmap;
    if (it == key_map.end()) {
        bitmap.reset(new chen::ds::Bitmap(static_cast<uint32_t>(m_metas.size())));
        key_map[key] = bitmap;
    } else {
        bitmap = it->second;
    }
    if (bitmap->getSize() < m_metas.size()) {
        bitmap->resize(static_cast<uint32_t>(m_metas.size()), false);
    }
    bitmap->set(idx, v);
    return true;
}

chen::ds::Bitmap::ptr SearchIndex::get(uint64_t type, uint64_t key)
{
    auto it = m_indexs.find(type);
    if (it == m_indexs.end()) {
        return nullptr;
    }
    auto itt = it->second.find(key);
    return itt == it->second.end() ? nullptr : itt->second;
}

void SearchIndex::ensureSize(size_t size)
{
    for (auto& [type, key_map] : m_indexs) {
        for (auto& [key, bitmap] : key_map) {
            if (bitmap->getSize() < size) {
                bitmap->resize(static_cast<uint32_t>(size), false);
            }
        }
    }
}

void SearchIndex::clearSlot(uint32_t idx)
{
    for (auto& [type, key_map] : m_indexs) {
        for (auto& [key, bitmap] : key_map) {
            bitmap->set(idx, false);
        }
    }
}

int64_t SearchIndex::getIdx(uint64_t article_id) const
{
    auto it = m_docMap.find(article_id);
    return it == m_docMap.end() ? -1 : static_cast<int64_t>(it->second);
}

int32_t SearchIndex::upsert(const SearchDoc& doc)
{
    if (doc.article_id == 0) {
        return -1;
    }

    uint32_t idx = 0;
    auto it = m_docMap.find(doc.article_id);
    if (it != m_docMap.end()) {
        idx = it->second;
        if (m_metas[idx].update_time > doc.search_update_time) {
            return 0;
        }
        clearSlot(idx);
    } else if (!m_free.empty()) {
        idx = m_free.back();
        m_free.pop_back();
        clearSlot(idx);
    } else {
        idx = static_cast<uint32_t>(m_metas.size());
        m_metas.emplace_back();
        ensureSize(m_metas.size());
    }

    DocMeta& meta = m_metas[idx];
    meta.article_id = doc.article_id;
    meta.weight = doc.weight;
    meta.publish_time = doc.publish_time;
    meta.update_time = doc.search_update_time;
    m_docMap[doc.article_id] = idx;

    buildIdx(doc, idx);
    return 1;
}

bool SearchIndex::remove(uint64_t article_id, int64_t update_time)
{
    int64_t idx = getIdx(article_id);
    if (idx < 0) {
        return false;
    }
    if (m_metas[idx].update_time > update_time) {
        return false;
    }

    clearSlot(static_cast<uint32_t>(idx));
    m_docMap.erase(article_id);
    m_metas[idx] = DocMeta();
    m_free.push_back(static_cast<uint32_t>(idx));
    return true;
}

bool SearchIndex::has(uint64_t article_id) const
{
    return m_docMap.find(article_id) != m_docMap.end();
}

int64_t SearchIndex::getUpdateTime(uint64_t article_id) const
{
    int64_t idx = getIdx(article_id);
    return idx < 0 ? 0 : m_metas[idx].update_time;
}

void SearchIndex::buildIdx(const SearchDoc& doc, uint32_t idx)
{
    set(static_cast<uint64_t>(IndexType::USER_ID), static_cast<uint64_t>(doc.user_id), idx, true);
    set(static_cast<uint64_t>(IndexType::STATE), static_cast<uint64_t>(doc.state), idx, true);
    set(static_cast<uint64_t>(IndexType::YEAR_MON),
        hash(chen::TimeUtil::Time2StrSec(doc.publish_time, "%Y年%m月"), true),
        idx,
        true);
    set(static_cast<uint64_t>(IndexType::CHANNEL), static_cast<uint64_t>(doc.channel), idx, true);

    buildWordIdx(doc.title, idx);
    buildWordIdx(doc.content, idx);

    for (auto category_id : doc.category_ids) {
        set(static_cast<uint64_t>(IndexType::CAT_ID), category_id, idx, true);
    }
    for (auto label_id : doc.label_ids) {
        set(static_cast<uint64_t>(IndexType::LABEL_ID), label_id, idx, true);
    }
}

void SearchIndex::buildWordIdx(const std::string& str, uint32_t idx)
{
    if (str.empty() || !HasJieba()) {
        return;
    }

    std::vector<std::string> words;
    CutWord(str, words);
    std::set<uint64_t> seen;
    for (auto& word : words) {
        if (word.size() < 2) {
            continue;
        }
        uint64_t h = hash(word, true);
        if (seen.insert(h).second) {
            set(static_cast<uint64_t>(IndexType::WORD), h, idx, true);
        }
    }
}

int32_t SearchIndex::search(
    std::vector<uint64_t>& ids,
    const std::map<uint64_t, std::set<uint64_t>>& params,
    int32_t page,
    int32_t page_size,
    int32_t& total)
{
    ids.clear();
    total = 0;

    if (!isReady()) {
        return -1;
    }
    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 20;
    }

    auto bitmap = query(params);
    if (!bitmap) {
        return 0;
    }

    uint32_t count = bitmap->getCount();
    if (count == 0) {
        return 0;
    }
    total = count > static_cast<uint32_t>(INT32_MAX) ? INT32_MAX : static_cast<int32_t>(count);

    std::vector<Candidate> candidates;
    candidates.reserve(count);
    for (auto it = bitmap->begin_new(); !*it; it->next()) {
        uint32_t idx = static_cast<uint32_t>(**it);
        if (idx >= m_metas.size()) {
            continue;
        }
        const DocMeta& meta = m_metas[idx];
        if (meta.article_id == 0) {
            continue;
        }
        Candidate candidate;
        candidate.weight = meta.weight;
        candidate.article_id = meta.article_id;
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.weight != b.weight) {
            return a.weight > b.weight;
        }
        return a.article_id > b.article_id;
    });

    int64_t offset = static_cast<int64_t>(page - 1) * page_size;
    if (offset >= static_cast<int64_t>(candidates.size())) {
        return 0;
    }

    size_t begin = static_cast<size_t>(offset);
    size_t end = std::min(candidates.size(), begin + static_cast<size_t>(page_size));
    ids.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) {
        ids.push_back(candidates[i].article_id);
    }
    return 0;
}

chen::ds::Bitmap::ptr SearchIndex::query(const std::map<uint64_t, std::set<uint64_t>>& params)
{
    if (params.empty()) {
        return nullptr;
    }

    chen::ds::Bitmap::ptr result;
    for (auto& [type, keys] : params) {
        if (keys.empty()) {
            continue;
        }

        chen::ds::Bitmap::ptr type_result;
        for (auto key : keys) {
            auto bitmap = get(type, key);
            if (!bitmap) {
                continue;
            }
            if (!type_result) {
                type_result.reset(new chen::ds::Bitmap(*bitmap));
            } else {
                *type_result |= *bitmap;
            }
        }

        if (!type_result) {
            return nullptr;
        }

        if (!result) {
            result = type_result;
        } else {
            *result &= *type_result;
            if (!result->any()) {
                return nullptr;
            }
        }
    }
    return result;
}

uint64_t SearchIndex::StrHash(const std::string& str)
{
    return chen::EncryptorUtil::Murmur3_64(chen::StringUtil::ToLower(str).c_str());
}

uint64_t SearchIndex::hash(const std::string& str, bool save)
{
    uint64_t h = StrHash(str);
    if (save) {
        m_strings.emplace(h, str);
    }
    return h;
}

bool SearchIndex::save(const std::string& path)
{
    if (!isReady()) {
        ERROR(logger) << "index not ready, skip save";
        return false;
    }
    if (path.empty()) {
        ERROR(logger) << "index path is empty";
        return false;
    }

    std::string dir = chen::FSUtil::Dirname(path);
    if (!dir.empty() && !chen::FSUtil::Exists(dir)) {
        chen::FSUtil::Mkdir(dir);
    }

    chen::ByteArray::ptr ba(new chen::ByteArray);
    ba->writeUint32(kIndexFileMagic);
    ba->writeUint32(kIndexFileVersion);
    ba->writeInt64(m_lastSyncTime);
    ba->writeInt64(static_cast<int64_t>(m_metas.size()));
    for (auto& meta : m_metas) {
        ba->writeInt64(static_cast<int64_t>(meta.article_id));
        ba->writeInt64(meta.weight);
        ba->writeInt64(meta.publish_time);
        ba->writeInt64(meta.update_time);
    }

    ba->writeUint32(static_cast<uint32_t>(m_docMap.size()));
    for (auto& [article_id, pos] : m_docMap) {
        ba->writeStringF16(std::to_string(article_id));
        ba->writeUint32(pos);
    }

    ba->writeUint32(static_cast<uint32_t>(m_strings.size()));
    for (auto& [h, str] : m_strings) {
        ba->writeInt64(static_cast<int64_t>(h));
        ba->writeStringF16(str);
    }

    ba->writeUint32(static_cast<uint32_t>(m_indexs.size()));
    for (auto& [type, key_map] : m_indexs) {
        ba->writeInt64(static_cast<int64_t>(type));
        ba->writeUint32(static_cast<uint32_t>(key_map.size()));
        for (auto& [key, bitmap] : key_map) {
            ba->writeInt64(static_cast<int64_t>(key));
            size_t pos_before = ba->getPosition();
            ba->writeUint32(0); // placeholder
            bitmap->writeTo(ba);
            size_t pos_after = ba->getPosition();
            ba->setPosition(pos_before);
            ba->writeUint32(static_cast<uint32_t>(pos_after - pos_before - 4));
            ba->setPosition(pos_after);
        }
    }

    ba->setPosition(0);
    std::string tmp_path = path + ".tmp";
    if (!ba->writeToFile(tmp_path)) {
        ERROR(logger) << "failed to write index tmp file: " << tmp_path;
        chen::FSUtil::Unlink(tmp_path, true);
        return false;
    }
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        ERROR(logger) << "failed to rename index file: " << tmp_path << " -> " << path << ", errno=" << errno;
        chen::FSUtil::Unlink(tmp_path, true);
        return false;
    }

    INFO(logger) << "index saved to " << path << ", docs=" << m_docMap.size() << ", slots=" << m_metas.size();
    return true;
}

bool SearchIndex::load(const std::string& path)
{
    if (path.empty() || !chen::FSUtil::Exists(path)) {
        INFO(logger) << "no index file at " << path;
        return false;
    }

    chen::ByteArray::ptr ba(new chen::ByteArray);
    if (!ba->readFromFile(path)) {
        ERROR(logger) << "failed to read index file: " << path;
        return false;
    }
    ba->setPosition(0);
    if (ba->getSize() < 16) {
        return false;
    }

    uint32_t magic = ba->readUint32();
    if (magic != kIndexFileMagic) {
        ERROR(logger) << "invalid index file magic";
        return false;
    }
    uint32_t version = ba->readUint32();
    if (version != kIndexFileVersion) {
        WARN(logger) << "index file version mismatch, file=" << version << " expect=" << kIndexFileVersion;
        return false;
    }

    clear();

    m_lastSyncTime = ba->readInt64();
    int64_t slot_count = ba->readInt64();
    if (slot_count < 0 || slot_count > 100000000) {
        ERROR(logger) << "invalid index slot count: " << slot_count;
        clear();
        return false;
    }
    m_metas.resize(static_cast<size_t>(slot_count));
    for (auto& meta : m_metas) {
        meta.article_id = static_cast<uint64_t>(ba->readInt64());
        meta.weight = ba->readInt64();
        meta.publish_time = ba->readInt64();
        meta.update_time = ba->readInt64();
    }

    uint32_t docmap_size = ba->readUint32();
    for (uint32_t i = 0; i < docmap_size; ++i) {
        std::string key_str = ba->readStringF16();
        int64_t article_id = chen::TypeUtil::Atoi(key_str);
        uint32_t pos = ba->readUint32();
        if (pos < m_metas.size()) {
            m_docMap[static_cast<uint64_t>(article_id)] = pos;
        }
    }

    uint32_t string_count = ba->readUint32();
    for (uint32_t i = 0; i < string_count; ++i) {
        int64_t h = ba->readInt64();
        std::string str = ba->readStringF16();
        m_strings[static_cast<uint64_t>(h)] = str;
    }

    uint32_t type_count = ba->readUint32();
    for (uint32_t ti = 0; ti < type_count; ++ti) {
        uint64_t type = static_cast<uint64_t>(ba->readInt64());
        uint32_t key_count = ba->readUint32();
        for (uint32_t ki = 0; ki < key_count; ++ki) {
            uint64_t key = static_cast<uint64_t>(ba->readInt64());
            ba->readUint32(); // bitmap size, readFrom will consume it
            auto bitmap = std::make_shared<chen::ds::Bitmap>(0);
            if (!bitmap->readFrom(ba)) {
                ERROR(logger) << "failed to read bitmap type=" << type << " key=" << key;
                clear();
                return false;
            }
            m_indexs[type][key] = bitmap;
        }
    }

    for (uint32_t i = 0; i < m_metas.size(); ++i) {
        if (m_metas[i].article_id == 0) {
            m_free.push_back(i);
        }
    }
    ensureSize(m_metas.size());

    INFO(logger) << "index loaded from " << path << ", docs=" << m_docMap.size() << ", slots=" << m_metas.size();
    return true;
}

} // namespace search
