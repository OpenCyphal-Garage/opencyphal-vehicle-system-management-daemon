//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_CLI_FMT_HELPERS_HPP_INCLUDED
#define OCVSMD_CLI_FMT_HELPERS_HPP_INCLUDED

#include <cetl/pf17/cetlpf.hpp>

#include <uavcan/_register/Value_1_0.hpp>

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>

#include <string>

template <>
struct fmt::formatter<uavcan::_register::Value_1_0> : formatter<std::string>
{
private:
    using TypeOf = uavcan::_register::Value_1_0::_traits_::TypeOf;

    static auto format_to(const TypeOf::empty&, format_context& ctx)
    {
        return fmt::format_to(ctx.out(), "empty");
    }

    static auto format_to(const TypeOf::string& str, format_context& ctx)
    {
        return fmt::format_to(ctx.out(), "'{}'", std::string{str.value.begin(), str.value.end()});
    }

    template <typename T>
    static auto format_to(const T& arr, format_context& ctx)
    {
        return fmt::format_to(ctx.out(), "{}", arr.value);
    }

public:
    static auto format(const uavcan::_register::Value_1_0& value, format_context& ctx)
    {
        return cetl::visit([&ctx](const auto& val) { return format_to(val, ctx); }, value.union_value);
    }
};

#endif  // OCVSMD_CLI_FMT_HELPERS_HPP_INCLUDED
