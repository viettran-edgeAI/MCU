#pragma once

#include <stdexcept>
#include <type_traits>
#include <cassert>
#include <utility>

// #include <cstring>
// #include <iostream>

template<typename T1, typename T2>
struct pair {
    T1 first;
    T2 second;

    // trivial default, copy and move
    constexpr pair() noexcept = default;
    constexpr pair(const T1& a, const T2& b) noexcept
    : first(a), second(b) {}
    constexpr pair(T1&& a, T2&& b) noexcept
    : first(std::move(a)), second(std::move(b)) {}
    constexpr pair(const pair&) noexcept = default;
    constexpr pair(pair&&) noexcept = default;

    constexpr pair& operator=(const pair&) noexcept = default;
    constexpr pair& operator=(pair&&) noexcept = default;

    // comparisons
    constexpr bool operator==(const pair& o) const noexcept {
        return first == o.first && second == o.second;
    }
    constexpr bool operator!=(const pair& o) const noexcept {
        return !(*this == o);
    }
    static constexpr pair<T1,T2> make_pair(const T1& a, const T2& b) noexcept {
        return pair<T1,T2>(a, b);
    }
    static constexpr pair<T1,T2> make_pair(T1&& a, T2&& b) noexcept {
        return pair<T1,T2>(std::move(a), std::move(b));
    }
};
template<typename T1, typename T2>
constexpr pair<std::decay_t<T1>, std::decay_t<T2>> make_pair(T1&& a, T2&& b) noexcept {
    return pair<std::decay_t<T1>, std::decay_t<T2>>(std::forward<T1>(a), std::forward<T2>(b));
}