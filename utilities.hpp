#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <cmath>

#include <arithmetic_type_tools/arithmetic_type_tools.hpp>

namespace saturating {
    using namespace arithmetic_type_tools;

    // In case of mixed types don't screw up the comparison
    template <typename U1, typename U2,
              typename UC = fit_all_t<U1, U2>>
    constexpr UC __attribute__((const))
    max(const U1 val1, const U2 val2) noexcept {
        return static_cast<UC>(val1) > static_cast<UC>(val2)
                        ? static_cast<UC>(val1)
                        : static_cast<UC>(val2);
    }

    // In case of mixed types don't screw up the comparison
    template <typename U1, typename U2,
              typename UC = fit_all_t<U1, U2>>
    constexpr UC __attribute__((const))
    min(const U1 val1, const U2 val2) noexcept {
        return static_cast<UC>(val1) < static_cast<UC>(val2)
                        ? static_cast<UC>(val1)
                        : static_cast<UC>(val2);
    }

    template <typename U1, typename U2, typename U3>
    constexpr decltype(auto)
    minmax(const U1& _min, const U2& val, const U3& _max) noexcept {
        return max(_min, min(val, _max));
    }


    template <typename Tout, typename Tin>
    constexpr decltype(auto) __attribute__((const))
    round(const Tin& val) {
        // Yup, the return types differ, but it seems the `constexpr` okays this for GCC at least
        if constexpr (sizeof(Tout) > sizeof(long)) {
            return std::llround(val);
        } else {
            return std::lround(val);
        }
    }

    /**
     * Test for equality, accounting for floating point rounding differences
     */
    template <typename TA, typename TB>
    constexpr bool __attribute__((const))
    fp_safe_equals(const TA& a, const TB& b) noexcept {
        using A = std::decay_t<TA>;
        using B = std::decay_t<TB>;
        if constexpr (std::is_floating_point_v<A>) {
            if constexpr (std::is_floating_point_v<B>) {
                if constexpr (sizeof(double) > sizeof(A) || sizeof(double) > sizeof(B)) {
                    return std::fabs(a - b) < std::numeric_limits<float>::epsilon();
                } else {
                    return std::fabs(a - b) < std::numeric_limits<double>::epsilon();
                }
            } else {
                return std::fabs(static_cast<A>(a) - static_cast<A>(b)) < std::numeric_limits<A>::epsilon();
            }
        } else {
            if constexpr (std::is_floating_point_v<B>) {
                return std::fabs(static_cast<B>(a) - static_cast<B>(b)) < std::numeric_limits<B>::epsilon();
            } else {
                return a == b;
            }
        }
    }
} // namespace saturating
