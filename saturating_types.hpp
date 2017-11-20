/**@file
 * @brief Always saturating integer types.
 *
 * Some assumptions and notes:
 * - The operators are 'viral', adding a saturating type and any other returns another saturating type.
 * - Divide by zero clips the value to `min` or `max`
 * - Tries to avoid the normal promotion rules
 * - The separate `add`, `subtract`, etc functions can be used to define extra external operators
 *   returning saturated types.
 *
 * TODO: Further test and improve algorithms (type combination specific optimizations)
 * TODO: Add non-static functions
 * TODO: Add member `scale_to` function
 * TODO: See if there is any way to allow non integral limits for floating point based types
 * TODO: Verify floating point based types
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace {
    // Why isn't std::max variadic?
    template <typename T>
    constexpr T&& max(T&& val) {
        return std::forward<T>(val);
    }

    template <typename T0, typename T1, typename... Ts>
    constexpr decltype(auto) max(T0&& val1, T1&& val2, Ts&&... vs) {
        return (val1 < val2)
                    ? max(val1, std::forward<Ts>(vs)...)
                    : max(val2, std::forward<Ts>(vs)...);
    }
}

namespace {
    namespace {
        // These do the actual type selection
        template <size_t S, bool B> struct _next_up {};
        template <> struct _next_up<1, true>  { typedef int_fast16_t  type; };
        template <> struct _next_up<1, false> { typedef uint_fast16_t type; };
        template <> struct _next_up<2, true>  { typedef int_fast32_t  type; };
        template <> struct _next_up<2, false> { typedef uint_fast32_t type; };
        template <> struct _next_up<4, true>  { typedef int_fast64_t  type; };
        template <> struct _next_up<4, false> { typedef uint_fast64_t type; };
#ifdef __SIZEOF_INT128__
        template <> struct _next_up<8, true>  { typedef __int128_t  type; };
        template <> struct _next_up<8, false> { typedef __uint128_t type; };
#endif
        template <size_t S, bool B>
        using _next_up_t = typename _next_up<S, B>::type;
    }

    /**
     * Helper to convert small types to the next size up, skipping the non-performant smaller upgrades.
     * One or more types can be supplied, if any type is signed a signed type will be returned.
     */
    template <typename... T> struct next_up {
        // For floating point there's no need to increase the resolution, but we do need to fit the largest type.
        typedef std::conditional_t<(std::is_floating_point_v<T> || ...),
                                    std::common_type_t<T...>,
                                    _next_up_t<max(sizeof(std::decay_t<T>)...), (std::is_signed_v<std::decay_t<T>> || ...)>> type;
    };

    /**
     * Use `next_up_t<T>` to obtain an integral type that is similar but twice the size.
     * Use `next_up_t<T...>` to obtain a type larger than either type, signed if needed.
     */
    template <typename... T>
    using next_up_t = typename next_up<T...>::type;
}

/** Base template for a saturating integer or unsigned integer. */
template <typename T,
          std::enable_if_t<!std::is_const_v<T>,    std::conditional_t<std::is_integral_v<T>, std::decay_t<T>, int>> min = std::numeric_limits<T>::lowest(),
          std::enable_if_t<!std::is_volatile_v<T>, std::conditional_t<std::is_integral_v<T>, std::decay_t<T>, int>> max = std::numeric_limits<T>::max()>
class x_sat_t {
public:
    typedef std::decay_t<T> type;

    static constexpr type min_val = min;
    static constexpr type max_val = max;

    /** Create a new zero-initialized saturated type. */
    constexpr x_sat_t() noexcept : value{0} {}

    /**
     * Create a new saturating type based on a given value.
     * @param  val Initial value will *NOT* be clamped to fit `T` (use `from()`).
     */
    template <typename U>
    constexpr x_sat_t(const U& val) noexcept : value{ static_cast<type>(val) } {}
    ///TODO: Fix constructing with explicit
    // constexpr explicit x_sat_t(const U& val) noexcept : value{ static_cast<type>(val) } {}

    /** Conversion back to the base type */
    constexpr operator const T&() const noexcept { return value; }
    constexpr operator T&() noexcept { return value; }

    /**
     * Add a and b and store result in a new saturating type.
     * @param  a LHS
     * @param  b RHS
     * @return   New saturating type
     */
    template <typename UA, typename UB>
    static constexpr
    std::enable_if_t<std::is_arithmetic_v<UA> && std::is_arithmetic_v<UB>, x_sat_t>
    __attribute__((pure))
    add(const UA& a, const UB& b) noexcept {
        if constexpr (std::is_floating_point_v<UA> || std::is_floating_point_v<UB>) {
            if constexpr (std::is_floating_point_v<type>) {
                return {
                    clamp(static_cast<type>(a) + static_cast<type>(b))
                };
            } else {
                return {
                    clamp(static_cast<std::common_type_t<UA, UB>>(a) + static_cast<std::common_type_t<UA, UB>>(b))
                };
            }
        } else {
            if constexpr (min == std::numeric_limits<type>::lowest() &&
                          max == std::numeric_limits<type>::max() &&
                          std::is_same_v<type, std::common_type_t<type, std::decay_t<UA>, std::decay_t<UB>>> == true)
            {
                type temp = 0;
                return {
                    __builtin_add_overflow(static_cast<type>(a), static_cast<type>(b), &temp)
                        ? (b > 0 ? max : min)
                        : temp
                };
            } else {
                return {
                    clamp(static_cast<next_up_t<T, UA, UB>>(a) + b)
                };
            }
        }
    }

    /**
     * Subtract `b` from `a` and return a new saturating type.
     * @param  a LHS
     * @param  b RHS
     * @return   New saturating type
     */
    template <typename UA, typename UB>
    static constexpr
    std::enable_if_t<std::is_arithmetic_v<UA> && std::is_arithmetic_v<UB>, x_sat_t>
    __attribute__((pure))
    subtract(const UA& a, const UB& b) noexcept {
        if constexpr (std::is_floating_point_v<UA> || std::is_floating_point_v<UB>) {
            if constexpr (std::is_floating_point_v<type>) {
                return {
                    clamp(static_cast<type>(a) - static_cast<type>(b))
                };
            } else {
                return {
                    clamp(static_cast<std::common_type_t<UA, UB>>(a) - static_cast<std::common_type_t<UA, UB>>(b))
                };
            }
        } else {
            if constexpr (min == std::numeric_limits<type>::lowest() &&
                          max == std::numeric_limits<type>::max() &&
                          std::is_same_v<type, std::common_type_t<type, std::decay_t<UA>, std::decay_t<UB>>> == true)
            {
                type temp = 0;
                return {
                    __builtin_sub_overflow(static_cast<type>(a), static_cast<type>(b), &temp)
                        ? (b > a ? min : max)
                        : temp
                };
            } else {
                if constexpr (std::is_signed_v<type>) {
                    return {
                        clamp(static_cast<std::make_signed_t<next_up_t<UA, UB>>>(a) - b)
                    };
                } else {
                    return {
                        b > a
                            ? 0
                            : clamp(static_cast<next_up_t<UA, UB>>(a) - b)
                    };
                }
            }
        }
    }

    /**
     * Multiply `a` with `b` and return a new saturating type.
     * @param  a LHS
     * @param  b RHS
     * @return   New saturating type
     */
    template <typename UA, typename UB>
    static constexpr
    std::enable_if_t<std::is_arithmetic_v<UA> && std::is_arithmetic_v<UB>, x_sat_t>
    __attribute__((pure))
    multiply(const UA& a, const UB& b) noexcept {
        if constexpr (std::is_floating_point_v<UA> || std::is_floating_point_v<UB>) {
            if constexpr (std::is_floating_point_v<type>) {
                return {
                    clamp(static_cast<type>(a) * static_cast<type>(b))
                };
            } else {
                return {
                    clamp(static_cast<std::common_type_t<UA, UB>>(a) * static_cast<std::common_type_t<UA, UB>>(b))
                };
            }
        } else {
            if constexpr (min == std::numeric_limits<type>::lowest() &&
                          max == std::numeric_limits<type>::max() &&
                          std::is_same_v<type, std::common_type_t<type, std::decay_t<UA>, std::decay_t<UB>>> == true)
            {
                type temp = 0;
                return {
                    __builtin_mul_overflow(static_cast<type>(a), static_cast<type>(b), &temp)
                        ? (((a < 0) == (b < 0)) ? max : min)
                        : temp
                };
            } else {
                return { clamp(static_cast<next_up_t<std::decay_t<UA>, std::decay_t<UB>>>(a) * b) };
            }
        }
    }

    /**
     * Divide `a` by `b` and return a new saturating type.
     * @param  a LHS
     * @param  b RHS
     * @return   New saturating type
     */
    template <typename UA, typename UB>
    static constexpr
    std::enable_if_t<std::is_arithmetic_v<UA> && std::is_arithmetic_v<UB>, x_sat_t>
    __attribute__((pure))
    divide(const UA& a, const UB& b) noexcept {
        if constexpr (std::is_floating_point_v<UA> || std::is_floating_point_v<UB>) {
            if constexpr (std::is_floating_point_v<type>) {
                return {
                    clamp(static_cast<type>(a) / static_cast<type>(b))
                };
            } else {
                return {
                    clamp(static_cast<std::common_type_t<UA, UB>>(a) / static_cast<std::common_type_t<UA, UB>>(b))
                };
            }
        } else {
            if constexpr (sizeof(type) >= sizeof(UA)) {
                return { static_cast<type>(a / b) };
            } else {
                return { clamp(a / b) };
            }
        }
    }

    constexpr auto& operator++() noexcept {
        if (value < max - 1) ++value;
        return *this;
    }
    constexpr auto operator++(int) noexcept {
        x_sat_t<type, min, max> temp { value };
        if (value < max - 1) ++value;
        return temp;
    }

    constexpr auto& operator--() noexcept {
        if (value > min + 1) --value;
        return *this;
    }
    constexpr auto operator--(int) noexcept {
        x_sat_t<type, min, max> temp { value };
        if (value > min + 1) --value;
        return temp;
    }

    template <typename U> constexpr auto& operator= (const U& other) noexcept { value = clamp(other); return *this; }

    template <typename U> constexpr decltype(auto) __attribute__((pure)) operator+(const U& other) const noexcept { return add(value, other); }
    template <typename U> constexpr decltype(auto) __attribute__((pure)) operator-(const U& other) const noexcept { return subtract(value, other); }
    template <typename U> constexpr decltype(auto) __attribute__((pure)) operator*(const U& other) const noexcept { return multiply(value, other); }
    template <typename U> constexpr decltype(auto) __attribute__((pure)) operator/(const U& other) const noexcept { return divide(value, other); }

    template <typename U> constexpr x_sat_t __attribute__((pure)) operator%(const U& other) const noexcept { return value % other; }

    template <typename U> constexpr auto& operator+=(const U& other) noexcept { value = add(value, other); return *this; }
    template <typename U> constexpr auto& operator-=(const U& other) noexcept { value = subtract(value, other); return *this; }
    template <typename U> constexpr auto& operator*=(const U& other) noexcept { value = multiply(value, other); return *this; }
    template <typename U> constexpr auto& operator/=(const U& other) noexcept { value = divide(value, other); return *this; }
    template <typename U> constexpr auto& operator%=(const U& other) noexcept { value %= other; return *this; }

    /**
     * Clamp value `val` to the base type limits. With float rounding.
     */
    template <typename U>
    static constexpr type __attribute__((pure)) clamp(const U& val) noexcept {
        if constexpr (std::is_floating_point_v<U>) {
            auto temp = val;
            if (temp > 0) {
                temp += static_cast<U>(0.5);
            } else  if (temp < 0) {
                temp -= static_cast<U>(0.5);
            }
            return (temp < min)
                    ? min
                    : (temp > max
                        ? max
                        : static_cast<type>(temp));
        } else {
            if constexpr (std::numeric_limits<U>::lowest() >= min &&
                          std::numeric_limits<U>::max()    <= max)
            {
                return static_cast<type>(val);
            } else {
                if constexpr (min > max) {
                    return (val < max)
                            ? max
                            : (val > min
                                ? min
                                : static_cast<type>(val));
                } else {
                    if constexpr (std::is_unsigned_v<U> && min <= 0) {
                        return val > max
                                ? max
                                : static_cast<type>(val);
                    } else {
                        return (val < min) // This isn't working properly
                                ? min
                                : (val > max
                                    ? max
                                    : static_cast<type>(val));
                    }
                }
            }
        }
    }

    /**
     * Create a new instance of this type, it's value being `val` clamped to fit `min` and `max`.
     * @param  val Initial value
     * @return     Saturating type with initial value clamped.
     */
    template <typename U>
    static constexpr x_sat_t __attribute__((pure)) from(const U& val) noexcept {
        return { clamp(val) };
    }

    /**
     * Scale the value of another saturating type to this one.
     * @param  val Saturating type
     * @return     Reference to this instance
     */
    template <typename U>
    constexpr auto& scale_from(const U& val) noexcept { value = x_sat_t::scale_from(val); return *this; }

    /**
     * Convert one saturating type to another, scaling the value.
     * @param  val Saturating type
     * @return     New saturating type
     */
    template <typename U,
              std::conditional_t<std::is_floating_point_v<U>, int, std::decay_t<U>> in_min,
              std::conditional_t<std::is_floating_point_v<U>, int, std::decay_t<U>> in_max,
              typename DISCARD = void>
    static constexpr x_sat_t __attribute__((pure))
    scale_from(const x_sat_t<U, in_min, in_max>& val) noexcept {
        if constexpr (static_cast<std::common_type_t<type, std::decay_t<U>>>(min) == static_cast<std::common_type_t<type, std::decay_t<U>>>(in_min)) {
            if constexpr (static_cast<std::common_type_t<type, std::decay_t<U>>>(max) == static_cast<std::common_type_t<type, std::decay_t<U>>>(in_max)) {
                return { static_cast<type>(val) };
            } else {
                //TODO: Check overflow
                return { static_cast<type>(((static_cast<next_up_t<T>>(val) - min) * in_max / max) + min) };
            }
        } else {
            if constexpr (static_cast<next_up_t<T>>(max) - min == static_cast<next_up_t<U>>(in_max) - in_min) {
                // Range shift only
                return { static_cast<type>(val + (static_cast<next_up_t<U>>(in_min) - min)) };
            }
            if constexpr (static_cast<std::common_type_t<type, std::decay_t<U>>>(max) == static_cast<std::common_type_t<type, std::decay_t<U>>>(in_max)) {
                return { static_cast<type>((val * (static_cast<next_up_t<T>>(max) - min))/(max - in_min)) };
            } else {
                auto temp = (static_cast<next_up_t<T, U>>(val) - in_min) *
                            (static_cast<next_up_t<T, U>>(max) - min) /
                            (static_cast<next_up_t<T, U>>(in_max) - in_min) + min + 1; // 1 for integer rounding
                return { static_cast<type>(temp) };
            }
        }
    }

    template <typename U, typename V>
    static constexpr std::enable_if_t<std::is_floating_point_v<U> & std::is_floating_point_v<V>, x_sat_t>
    __attribute__((pure))
    scale_from(const U& val,
               const V& in_min,
               const V& in_max) noexcept
    {
        auto temp = (val - in_min) *
                    (static_cast<next_up_t<T>>(max) - min) /
                    (in_max - in_min) + min;
        return { static_cast<type>(temp + 0.5f) };
    }

    // template <typename U, typename V = int>
    // static constexpr std::enable_if_t<std::is_floating_point_v<U> && std::is_integral_v<V>, x_sat_t>
    // __attribute__((pure)) /**TODO: Starting the range for unsigned values from 0 instead of -1 feels optimal, at the cost of complex default behaviour */
    // scale_from(const U& val,
    //            const V& in_min = std::is_signed_v<type> ? -1 : 0,
    //            const V& in_max = 1) noexcept
    // {
    //     auto temp = (val - in_min) *
    //                 (static_cast<next_up_t<T>>(max) - min) /
    //                 (static_cast<next_up_t<T>>(in_max) - in_min) + min;
    //     return { static_cast<type>(temp + 0.5f) };
    // }

private:
    T value;
};

namespace std {
    // Extend the standard type traits to handle the new sat types.
    template <typename T, T _min, T _max>
    class decay<x_sat_t<T, _min, _max>> {
    public:
        typedef typename decay<T>::type type;
    };

    template <typename T, T _min, T _max>
    struct is_unsigned<x_sat_t<T, _min, _max>> {
        static constexpr decltype(auto) value = is_unsigned_v<std::decay_t<T>>;
    };

    template <typename T, T _min, T _max>
    struct is_signed<x_sat_t<T, _min, _max>> {
        static constexpr decltype(auto) value = is_signed_v<std::decay_t<T>>;
    };

    template <typename T, T _min, T _max>
    struct is_integral<x_sat_t<T, _min, _max>> {
        static constexpr decltype(auto) value = is_integral_v<std::decay_t<T>>;
    };

    template <typename T, T _min, T _max>
    struct is_floating_point<x_sat_t<T, _min, _max>> {
        static constexpr decltype(auto) value = is_floating_point_v<std::decay_t<T>>;
    };

    template <typename T, T _min, T _max>
    struct is_arithmetic<x_sat_t<T, _min, _max>> {
        static constexpr decltype(auto) value = is_arithmetic_v<std::decay_t<T>>;
    };

    template <typename T, T _min, T _max>
    class numeric_limits<x_sat_t<T, _min, _max>> {
        // TODO: implement the rest of: http://en.cppreference.com/w/cpp/types/numeric_limits
    public:
        static constexpr std::decay_t<T> min()    noexcept { return _min; }
        static constexpr std::decay_t<T> lowest() noexcept { return _min; }
        static constexpr std::decay_t<T> max()    noexcept { return _max; }
    };
}

typedef x_sat_t<int8_t>   int_sat8_t;
typedef x_sat_t<uint8_t>  uint_sat8_t;

typedef x_sat_t<int16_t>  int_sat16_t;
typedef x_sat_t<uint16_t> uint_sat16_t;

typedef x_sat_t<int32_t>  int_sat32_t;
typedef x_sat_t<uint32_t> uint_sat32_t;

typedef x_sat_t<int64_t>  int_sat64_t;
typedef x_sat_t<uint64_t> uint_sat64_t;

#ifdef __SIZEOF_INT128__
typedef x_sat_t<__int128_t>  int_sat128_t;
typedef x_sat_t<__uint128_t> uint_sat128_t;
#endif

typedef x_sat_t<float,  -1, 1> float_sat_t;
typedef x_sat_t<double, -1, 1> double_sat_t;
