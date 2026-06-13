#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace spdlog {
namespace sinks {
class sink;
}
} // namespace spdlog

namespace ImApp {

/**
 * @brief A single captured log entry.
 */
struct LogEntry {
    int level; // spdlog::level::level_enum
    std::string logger_name;
    std::string message;
    std::string payload;
};

/**
 * @brief Singleton ring buffer that collects log entries from all spdlog
 * loggers via a custom sink.
 *
 * Add the sink returned by create_sink() to every logger whose output
 * should appear in the in-app Output widget.
 *
 * Thread-safe: the sink calls add_entry() under spdlog's internal mutex;
 * get_entries() and clear() use an independent mutex for the read side.
 */
class OutputCapture {
  public:
    static OutputCapture& instance();

    /**
     * @brief Create a spdlog sink that forwards every log message to
     * OutputCapture.
     *
     * The caller is responsible for adding the returned sink to the
     * desired logger's sink list (logger->sinks().push_back(sink)).
     */
    static std::shared_ptr<spdlog::sinks::sink> create_sink();

    /**
     * @brief Return new entries since the last call.
     *
     * Thread-safe.  May be called from the UI thread every frame.
     */
    std::vector<LogEntry> get_entries();

    /**
     * @brief Discard all stored entries.
     */
    void clear();

    /**
     * @brief Discard only entries belonging to a specific logger.
     *
     * @param logger_name  Exact logger name whose entries should be removed.
     */
    void clear(const std::string& logger_name);

    /**
     * @brief Maximum number of entries kept in the ring buffer.
     */
    static constexpr size_t g_max_entries = 10000;

    /**
     * @brief Called by the capture sink on the spdlog thread to store a
     * log entry in the ring buffer.  Public so that the sink (defined in
     * the .cpp file) can access it.
     */
    void add_entry(int level, std::string logger_name, std::string message,
                   std::string payload);

    OutputCapture(const OutputCapture&) = delete;
    OutputCapture& operator=(const OutputCapture&) = delete;

  private:
    OutputCapture() = default;

    std::mutex m_mutex;
    std::deque<LogEntry> m_entries;
    size_t m_read_index{0};
};

} // namespace ImApp
