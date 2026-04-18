#ifndef ZOBRIST_HASHING_H
#define ZOBRIST_HASHING_H

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "board.h"

namespace ZobristData {

using PieceSquareTable = std::array<std::array<uint64_t, 64>, 12>;

void init();
int pieceIndex(Piece p);
uint64_t pieceSquare(Piece p, int sq);
uint64_t sideToMove(bool whiteToMove);
uint64_t castlingRights(int rights);
uint64_t enPassantFile(int file);

} // namespace ZobristData

class ZobristHashing {
public:
    ZobristHashing();

    uint64_t computeHash(const board& b) const;
    void updateHash(uint64_t& hashValue, const Move& move, const board& b) const;
};

struct Node {
    uint64_t key;
    std::vector<Move> value;
    Node* next;
    Node* prev;

    Node(uint64_t k, const std::vector<Move>& v)
        : key(k), value(v), next(nullptr), prev(nullptr) {
    }
};

class LRUCache
{
public:
    int capacity;
    std::unordered_map<uint64_t, Node*> cacheMap;
    Node* head;
    Node* tail;

    explicit LRUCache(int capacity)
        : capacity(capacity), head(new Node(0, {})), tail(new Node(0, {})) {
        head->next = tail;
        tail->prev = head;
    }

    std::vector<Move> get(uint64_t key) {
        auto it = cacheMap.find(key);
        if (it == cacheMap.end()) {
            return {};
        }

        Node* node = it->second;
        remove(node);
        add(node);
        return node->value;
    }

    void put(uint64_t key, const std::vector<Move>& value) {
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            Node* oldNode = it->second;
            remove(oldNode);
            delete oldNode;
        }

        Node* node = new Node(key, value);
        cacheMap[key] = node;
        add(node);

        if (static_cast<int>(cacheMap.size()) > capacity) {
            Node* nodeToDelete = tail->prev;
            remove(nodeToDelete);
            cacheMap.erase(nodeToDelete->key);
            delete nodeToDelete;
        }
    }

private:
    void add(Node* node) {
        Node* nextNode = head->next;
        head->next = node;
        node->prev = head;
        node->next = nextNode;
        nextNode->prev = node;
    }

    void remove(Node* node) {
        Node* prevNode = node->prev;
        Node* nextNode = node->next;
        prevNode->next = nextNode;
        nextNode->prev = prevNode;
    }
};

#endif // ZOBRIST_HASHING_H
