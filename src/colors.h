#pragma once

// Shared ANSI color/style constants used across the UI.
namespace clr {
    constexpr auto reset  = "\033[0m";
    constexpr auto bold   = "\033[1m";
    constexpr auto dim    = "\033[2m";
    constexpr auto white  = "\033[97m";
    constexpr auto green  = "\033[32m";
    constexpr auto red    = "\033[31m";
    constexpr auto yellow = "\033[33m";
    constexpr auto cyan   = "\033[36m";
    constexpr auto orange = "\033[38;5;208m";
}
