#ifndef LRU_CACHE_HPP
#define LRU_CACHE_HPP

#include <unordered_map>
#include <list>
#include <string>
#include <mutex>

// Thread-safe LRU cache for string keys and string values.
class LruCache {
public:
    explicit LruCache(size_t capacity_bytes)
        : capacityBytes(capacity_bytes), currentBytes(0) {}

    bool get(const std::string& key, std::string& value) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = keyToIterator.find(key);
        if (it == keyToIterator.end()) {
            return false;
        }
        items.splice(items.begin(), items, it->second);
        value = it->second->value;
        return true;
    }

    void put(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = keyToIterator.find(key);
        size_t valueSize = value.size() + key.size();
        if (it != keyToIterator.end()) {
            currentBytes -= (it->second->value.size() + it->second->key.size());
            it->second->value = value;
            items.splice(items.begin(), items, it->second);
            currentBytes += valueSize;
            evictIfNeeded();
            return;
        }
        items.emplace_front(Node{key, value});
        keyToIterator[key] = items.begin();
        currentBytes += valueSize;
        evictIfNeeded();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        items.clear();
        keyToIterator.clear();
        currentBytes = 0;
    }

private:
    struct Node {
        std::string key;
        std::string value;
    };

    void evictIfNeeded() {
        while (currentBytes > capacityBytes && !items.empty()) {
            Node& tail = items.back();
            currentBytes -= (tail.value.size() + tail.key.size());
            keyToIterator.erase(tail.key);
            items.pop_back();
        }
    }

    size_t capacityBytes;
    size_t currentBytes;
    std::list<Node> items;
    std::unordered_map<std::string, std::list<Node>::iterator> keyToIterator;
    std::mutex mutex;
};

#endif // LRU_CACHE_HPP


