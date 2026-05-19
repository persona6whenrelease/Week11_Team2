#pragma once
#include <stdint.h>
#include <cassert>
#include <cstddef>
#include <vector>
#include <list>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <array>
#include <string>
#include <utility>

using int8 = __int8;
using int16 = __int16;
using int32 = __int32;
using int64 = __int64;

using uint8 = unsigned __int8;
using uint16 = unsigned __int16;
using uint32 = unsigned __int32;
using uint64 = unsigned __int64;

using FString = std::string;

template <typename T>
using TArray = std::vector<T>;

template <typename T>
using TDoubleLinkedList = std::list<T>;

template <typename T>
using TLinkedList = std::list<T>;

template <typename T, size_t N>
using TStaticArray = std::array<T, N>;

// ---------------------------------------------------------------------------
// GetTypeHash — UE 스타일 자유 함수 규약.
// 기본 타입은 여기서 제공하고, 커스텀 타입(USTRUCT 등)은
// .generated.cpp 또는 사용자 헤더에서 오버로딩한다.
// ---------------------------------------------------------------------------
inline size_t HashCombine(size_t Seed, size_t Hash)
{
    return Seed ^ (Hash + 0x9e3779b9ull + (Seed << 6) + (Seed >> 2));
}

inline size_t GetTypeHash(bool     V) { return std::hash<bool>{}(V); }
inline size_t GetTypeHash(int8     V) { return std::hash<int8>{}(V); }
inline size_t GetTypeHash(int16    V) { return std::hash<int16>{}(V); }
inline size_t GetTypeHash(int32    V) { return std::hash<int32>{}(V); }
inline size_t GetTypeHash(int64    V) { return std::hash<int64>{}(V); }
inline size_t GetTypeHash(uint8    V) { return std::hash<uint8>{}(V); }
inline size_t GetTypeHash(uint16   V) { return std::hash<uint16>{}(V); }
inline size_t GetTypeHash(uint32   V) { return std::hash<uint32>{}(V); }
inline size_t GetTypeHash(uint64   V) { return std::hash<uint64>{}(V); }
inline size_t GetTypeHash(float    V) { return std::hash<float>{}(V); }
inline size_t GetTypeHash(double   V) { return std::hash<double>{}(V); }
inline size_t GetTypeHash(const std::string& V) { return std::hash<std::string>{}(V); }

// DefaultHasher: GetTypeHash() 자유 함수를 std::unordered_* 의 Hasher로 연결
struct FDefaultHasher
{
    template<typename T>
    size_t operator()(const T& V) const { return GetTypeHash(V); }
};

template <typename T>
using TSet = std::unordered_set<T, FDefaultHasher>;

template <typename KeyType, typename ValueType>
using TMap = std::unordered_map<KeyType, ValueType, FDefaultHasher>;

template <typename T1, typename T2>
using TPair = std::pair<T1, T2>;


template <typename T>
using TQueue = std::queue<T>;

// ===== Assert =====
#ifdef _DEBUG
#define check(expr)       assert(expr)
#define checkf(expr, msg) assert((expr) && (msg))
#else
#define check(expr)       ((void)0)
#define checkf(expr, msg) ((void)0)
#endif