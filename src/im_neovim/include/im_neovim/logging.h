#pragma once
#include <spdlog/spdlog.h>

#define IM_NVIM_LOGGER_NAME "ImNeoVim"

#define LOG_DEBUG(...) ::spdlog::get(IM_NVIM_LOGGER_NAME)->debug(__VA_ARGS__)
#define LOG_INFO(...) ::spdlog::get(IM_NVIM_LOGGER_NAME)->info(__VA_ARGS__)
#define LOG_WARN(...) ::spdlog::get(IM_NVIM_LOGGER_NAME)->warn(__VA_ARGS__)
#define LOG_ERROR(...) ::spdlog::get(IM_NVIM_LOGGER_NAME)->error(__VA_ARGS__)
#define LOG_CRITICAL(...)                                                      \
    ::spdlog::get(IM_NVIM_LOGGER_NAME)->critical(__VA_ARGS__)
