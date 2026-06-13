#include "im_app/output_capture.h"

#include <spdlog/details/log_msg.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

namespace ImApp {

// ---------------------------------------------------------------------------
// Custom spdlog sink that forwards every log message to OutputCapture
// ---------------------------------------------------------------------------

template <typename Mutex>
class OutputCaptureSink : public spdlog::sinks::base_sink<Mutex> {
  protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        // Format the message using the sink's formatter (includes timestamp,
        // level, logger name, etc. depending on the pattern).
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);
        std::string message(formatted.data(), formatted.size());

        OutputCapture::instance().add_entry(
            static_cast<int>(msg.level),
            std::string(msg.logger_name.data(), msg.logger_name.size()),
            std::move(message),
            std::string(msg.payload.data(), msg.payload.size()));
    }

    void flush_() override {}
};

using OutputCaptureSinkMt = OutputCaptureSink<std::mutex>;

// ---------------------------------------------------------------------------
// OutputCapture
// ---------------------------------------------------------------------------

OutputCapture& OutputCapture::instance() {
    static OutputCapture s_instance;
    return s_instance;
}

std::shared_ptr<spdlog::sinks::sink> OutputCapture::create_sink() {
    auto sink = std::make_shared<OutputCaptureSinkMt>();
    // Use the same pattern as the default logger so that the captured
    // text looks consistent with what appears on stdout.
    sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    return sink;
}

std::vector<LogEntry> OutputCapture::get_entries() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<LogEntry> new_entries;
    if (m_read_index < m_entries.size()) {
        new_entries.assign(m_entries.begin() +
                               static_cast<std::ptrdiff_t>(m_read_index),
                           m_entries.end());
        m_read_index = m_entries.size();
    }
    return new_entries;
}

void OutputCapture::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_read_index = 0;
}

void OutputCapture::clear(const std::string& logger_name) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Count how many matching entries lie before the current read cursor
    // so we can adjust m_read_index after erasure.
    size_t removed_before_read = 0;
    for (size_t i = 0; i < m_read_index && i < m_entries.size(); ++i) {
        if (m_entries[i].logger_name == logger_name) {
            ++removed_before_read;
        }
    }

    std::erase_if(m_entries, [&](const LogEntry& e) {
        return e.logger_name == logger_name;
    });

    if (m_read_index > removed_before_read) {
        m_read_index -= removed_before_read;
    } else {
        m_read_index = 0;
    }
}

void OutputCapture::add_entry(int level, std::string logger_name,
                              std::string message, std::string payload) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Remove trailing newline that spdlog formatters typically append.
    if (!message.empty() && message.back() == '\n') {
        message.pop_back();
    }
    if (!payload.empty() && payload.back() == '\n') {
        payload.pop_back();
    }

    m_entries.push_back({level, std::move(logger_name), std::move(message),
                         std::move(payload)});

    // Keep the buffer bounded.
    while (m_entries.size() > g_max_entries) {
        m_entries.pop_front();
        if (m_read_index > 0) {
            --m_read_index;
        }
    }
}

} // namespace ImApp
