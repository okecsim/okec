#ifndef OKEC_RANDOM_HPP_
#define OKEC_RANDOM_HPP_

#include <concepts>
#include <random>
#include <type_traits>
#include <fmt/core.h>
#include <torch/torch.h>

namespace okec
{

template <class T>
struct rand_range_impl {
    auto operator()(T low, T high) -> T {
        return torch::rand({1}).uniform_(low, high).template item<T>();
    }
};

template <>
struct rand_range_impl<int> {
    auto operator()(int low, int high) -> int {
        return torch::randint(low, high, {1}).template item<int>();
    }
};

// template <class T>
// auto rand_range(T low, T high) -> T {
//     return rand_range_impl<T>()(low, high);
// }

template <class T>
struct rand_range {
    rand_range(T low, T high)
        : val{ rand_range_impl<T>()(low, high) } {}

    operator T() const {
        return val;
    }

    auto to_string(int precision = 2) -> std::string {
        if constexpr (std::is_floating_point_v<T>) {
            return fmt::format("{:.{}f}", val, precision);
        }
        
        return fmt::format("{}", val);
    }

private:
    T val;
};

template <typename T>
auto rand_value_impl() -> T {
    std::random_device rd;
    std::mt19937 gen(rd());
    if constexpr (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<T> dis;
        return dis(gen);
    } else {
        std::uniform_int_distribution<T> dis;
        return dis(gen);
    }
}

template <class T>
requires std::is_floating_point_v<std::remove_cvref_t<T>>
    || std::is_integral_v<std::remove_cvref_t<T>>
class rand_value {
    using value_type = std::remove_cvref_t<T>;

public:
    rand_value()
        : val{ rand_value_impl<value_type>() } {}

    operator value_type() const {
        return val;
    }

    auto to_string(int precision = 2) -> std::string {
        if constexpr (std::is_floating_point_v<value_type>) {
            return fmt::format("{:.{}f}", val, precision);
        }
        
        return fmt::format("{}", val);
    }

private:
    value_type val;
};

} // namespace okec

#endif // OKEC_RANDOM_HPP_