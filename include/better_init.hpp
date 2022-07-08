#pragma once

// better_init

// License: ZLIB

// Copyright (c) 2022 Egor Mikhailov
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#include <initializer_list>
#include <type_traits>

// This file is included by this header automatically, if it exists.
// Put your customizations here.
#ifndef BETTER_INIT_CONFIG
#define BETTER_INIT_CONFIG "better_init_config.hpp"
#endif

namespace better_init
{
    namespace detail
    {
        // Convertible to any `std::initializer_list<??>`.
        struct any_init_list
        {
            template <typename T>
            constexpr operator std::initializer_list<T>() const noexcept; // Not defined.
        };

        // Default implementation for `custom::range_traits`.
        // Override `custom::range_traits`, not this.
        template <typename T>
        struct basic_range_traits
        {
            // Whether to make the conversion operator of `init{...}` implicit.
            static constexpr bool implicit = std::is_constructible_v<T, detail::any_init_list>;

            // How to construct `T` from a pair of iterators. Defaults to `T(begin, end)`.
            template <typename Iter> requires std::is_constructible_v<T, Iter, Iter>
            static constexpr T construct(Iter begin, Iter end) noexcept(std::is_nothrow_constructible_v<T, Iter, Iter>)
            {
                // Don't want to include `<utility>` for `std::move`.
                return T(static_cast<Iter &&>(begin), static_cast<Iter &&>(end));
            }
        };
    }

    // Customization points.
    namespace custom
    {
        // Customizes the behavior of `init::to()` and of the implicit conversion to a container.
        template <typename T, typename = void>
        struct range_traits : detail::basic_range_traits<T> {};
    }
}

#if __has_include(BETTER_INIT_CONFIG)
#include BETTER_INIT_CONFIG
#endif

// Lets you change the identifier used for out initializer lists.
#ifndef BETTER_INIT_IDENTIFIER
#define BETTER_INIT_IDENTIFIER init
#endif

// Should stop the program.
#ifndef BETTER_INIT_ABORT
#ifdef _MSC_VER
#define BETTER_INIT_ABORT __debugbreak();
#else
#define BETTER_INIT_ABORT __builtin_trap();
#endif
#endif

namespace std
{
    struct random_access_iterator_tag; // Yes. Don't want to include `<iterator>`.
}

namespace better_init
{
    namespace detail
    {
        struct empty {};

        // Don't want to include extra headers, so I roll my own typedefs.
        using size_t = decltype(sizeof(int));
        using ptrdiff_t = decltype((int *)nullptr - (int *)nullptr);
        static_assert(sizeof(size_t) == sizeof(void *)); // We use it place of `std::uintptr_t` too.

        // Whether `T` is constructible from a pair of `Iter`s.
        template <typename T, typename Iter>
        concept constructible_from_iters = requires(Iter i)
        {
            custom::range_traits<T>::construct(static_cast<Iter &&>(i), static_cast<Iter &&>(i));
        };

        template <typename T, typename Iter>
        concept nothrow_constructible_from_iters = requires(Iter i)
        {
            { custom::range_traits<T>::construct(static_cast<Iter &&>(i), static_cast<Iter &&>(i)) } noexcept;
        };
    }

    template <typename ...P>
    class BETTER_INIT_IDENTIFIER
    {
      public:
        // Whether this list can be used to initialize a range of `T`s.
        template <typename T> static constexpr bool can_initialize_elem         = (std::is_constructible_v        <T, P &&> && ...);
        template <typename T> static constexpr bool can_nothrow_initialize_elem = (std::is_nothrow_constructible_v<T, P &&> && ...);

      private:
        class Reference
        {
            friend BETTER_INIT_IDENTIFIER;
            void *target = nullptr;
            detail::size_t index = 0;

            template <typename T>
            constexpr T to() const
            {
                if constexpr (sizeof...(P) == 0)
                {
                    // Note: This is intentionally not a SFINAE check nor a `static_assert`, to support init from empty lists.
                    BETTER_INIT_ABORT
                }
                else
                {
                    constexpr T (*lambdas[])(void *) = {
                        +[](void *ptr) -> T
                        {
                            // Don't want to include `<utility>` for `std::forward`.
                            return T(static_cast<P &&>(*reinterpret_cast<P *>(ptr)));
                        }...
                    };
                    return lambdas[index](target);
                }
            }

            constexpr Reference() {}

          public:
            // Non-copyable.
            // The list creates and owns all its references, and exposes actual references to them.
            // This is because pre-C++20 iterator requirements force us to return actual references from `*`, and more importantly `[]`.
            Reference(const Reference &) = delete;
            Reference &operator=(const Reference &) = delete;

            template <typename T> requires can_initialize_elem<T>
            constexpr operator T() const noexcept(can_nothrow_initialize_elem<T>)
            {
                return to<T>();
            }

            // Would also add `operator T &&`, but it confuses GCC (note, not libstdc++).
        };

        class Iterator
        {
            friend class BETTER_INIT_IDENTIFIER;
            const Reference *ref = nullptr;

          public:
            // Can't rely on C++20 auto-detection of those typedefs, because at least in libstdc++, it doesn't like the disabled `value_type`.
            using iterator_category = std::random_access_iterator_tag;
            using pointer = void;
            using difference_type = detail::ptrdiff_t;
            using reference = Reference;
            // Don't want elements to be copied out of the list. This is the smallest definition that doesn't fail the concepts.
            struct value_type {constexpr value_type(const Reference &) noexcept {}};

            constexpr Iterator() noexcept {}

            // `LegacyForwardIterator` requires us to return an actual reference here.
            constexpr const Reference &operator*() const noexcept {return *ref;}

            // No `operator->`. This causes C++20 `std::iterator_traits` to guess `pointer_type == void`, which sounds ok to me.

            // Don't want to rely on `<compare>`.
            friend constexpr bool operator==(Iterator a, Iterator b) noexcept
            {
                return a.ref == b.ref;
            }
            friend constexpr bool operator!=(Iterator a, Iterator b) noexcept
            {
                return !(a == b);
            }
            friend constexpr bool operator<(Iterator a, Iterator b) noexcept
            {
                // Don't want to include `<functional>` for `std::less`, so need to cast to an integer to avoid UB.
                return detail::size_t(a.ref) < detail::size_t(b.ref);
            }
            friend constexpr bool operator> (Iterator a, Iterator b) noexcept {return b < a;}
            friend constexpr bool operator<=(Iterator a, Iterator b) noexcept {return !(b < a);}
            friend constexpr bool operator>=(Iterator a, Iterator b) noexcept {return !(a < b);}

            constexpr Iterator &operator++() noexcept
            {
                ++ref;
                return *this;
            }
            constexpr Iterator &operator--() noexcept
            {
                --ref;
                return *this;
            }
            constexpr Iterator operator++(int) noexcept
            {
                Iterator ret = *this;
                ++*this;
                return ret;
            }
            constexpr Iterator operator--(int) noexcept
            {
                Iterator ret = *this;
                --*this;
                return ret;
            }
            constexpr friend Iterator operator+(Iterator it, detail::ptrdiff_t n) noexcept {it += n; return it;}
            constexpr friend Iterator operator+(detail::ptrdiff_t n, Iterator it) noexcept {it += n; return it;}
            constexpr friend Iterator operator-(Iterator it, detail::ptrdiff_t n) noexcept {it -= n; return it;}
            // There's no `number - iterator`.

            constexpr friend detail::ptrdiff_t operator-(Iterator a, Iterator b) noexcept {return a.ref - b.ref;}

            constexpr Iterator &operator+=(detail::ptrdiff_t n) noexcept {ref += n; return *this;}
            constexpr Iterator &operator-=(detail::ptrdiff_t n) noexcept {ref -= n; return *this;}

            constexpr const Reference &operator[](detail::ptrdiff_t i) const noexcept
            {
                return *(*this + i);
            }
        };

        // Could use `std::array`, but want to use less headers.
        // I know that `[[no_unique_address]]` is disabled in MSVC for now and they use a different attribute, but don't care because there are no other members here.
        // Must store `Reference`s here, because `std::random_access_iterator` requires `operator[]` to return the same type as `operator*`,
        // and `LegacyForwardIterator` requires `operator*` to return an actual reference. If we don't have those here, we don't have anything for the references to point to.
        [[no_unique_address]] std::conditional_t<sizeof...(P) == 0, detail::empty, Reference[sizeof...(P) + (sizeof...(P) == 0)]> elems;

      public:
        // The element-wise constructor.
        [[nodiscard]] constexpr BETTER_INIT_IDENTIFIER(P &&... params) noexcept
        {
            detail::size_t i = 0;
            ((elems[i].target = &params, elems[i].index = i, i++), ...);
        }

        // Iterators.
        // Those should be `&&`-qualified, but then we no longer satisfy `std::ranges::range`.
        [[nodiscard]] constexpr Iterator begin() const noexcept
        {
            Iterator ret;
            if constexpr (sizeof...(P) > 0)
                ret.ref = elems;
            return ret;
        }
        [[nodiscard]] constexpr Iterator end() const noexcept
        {
            Iterator ret;
            if constexpr (sizeof...(P) > 0)
                ret.ref = elems + sizeof...(P);
            return ret;
        }

        // Convert to a range.
        template <typename T> requires detail::constructible_from_iters<T, Iterator>
        [[nodiscard]] constexpr T to() const && noexcept(detail::nothrow_constructible_from_iters<T, Iterator>)
        {
            // Don't want to include `<utility>` for `std::move`.
            return custom::range_traits<T>::construct(begin(), end());
        }

        // Implicitly convert to a range.
        template <typename T> requires detail::constructible_from_iters<T, Iterator>
        [[nodiscard]] constexpr explicit(!custom::range_traits<T>::implicit) operator T() const && noexcept(detail::nothrow_constructible_from_iters<T, Iterator>)
        {
            // Don't want to include `<utility>` for `std::move`.
            return static_cast<const BETTER_INIT_IDENTIFIER &&>(*this).to<T>();
        }
    };

    template <typename ...P>
    BETTER_INIT_IDENTIFIER(P &&...) -> BETTER_INIT_IDENTIFIER<P...>;
}

using better_init::BETTER_INIT_IDENTIFIER;