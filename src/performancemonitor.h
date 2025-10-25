#ifndef PERFORMANCEMONITOR_H
#define PERFORMANCEMONITOR_H

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <fstream>
#include <QObject>

/**
 * Performance measurement data structure
 */
struct PerformanceMeasurement {
    std::string name;
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point endTime;
    double durationMs = 0.0;
    std::string category;
    std::unordered_map<std::string, std::string> metadata;

    PerformanceMeasurement() = default;
    PerformanceMeasurement(const std::string& measurementName, const std::string& measurementCategory = "")
        : name(measurementName), category(measurementCategory) {
        startTime = std::chrono::high_resolution_clock::now();
    }

    void finish() {
        endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        durationMs = duration.count() / 1000.0;
    }

    void addMetadata(const std::string& key, const std::string& value) {
        metadata[key] = value;
    }
};

/**
 * Performance statistics for a specific measurement type
 */
struct PerformanceStats {
    std::string name;
    std::string category;
    int count = 0;
    double totalTimeMs = 0.0;
    double averageTimeMs = 0.0;
    double minTimeMs = std::numeric_limits<double>::max();
    double maxTimeMs = 0.0;
    double standardDeviation = 0.0;
    std::vector<double> recentMeasurements;

    void addMeasurement(double timeMs) {
        count++;
        totalTimeMs += timeMs;
        averageTimeMs = totalTimeMs / count;
        minTimeMs = std::min(minTimeMs, timeMs);
        maxTimeMs = std::max(maxTimeMs, timeMs);

        // Keep only recent measurements for standard deviation calculation
        recentMeasurements.push_back(timeMs);
        if (recentMeasurements.size() > 100) {
            recentMeasurements.erase(recentMeasurements.begin());
        }

        // Calculate standard deviation
        if (recentMeasurements.size() > 1) {
            double sum = 0.0;
            double mean = 0.0;
            for (double measurement : recentMeasurements) {
                mean += measurement;
            }
            mean /= recentMeasurements.size();

            for (double measurement : recentMeasurements) {
                sum += (measurement - mean) * (measurement - mean);
            }
            standardDeviation = std::sqrt(sum / recentMeasurements.size());
        }
    }
};

/**
 * RAII performance timer for automatic measurement
 */
class PerformanceTimer {
public:
    PerformanceTimer(const std::string& name, const std::string& category = "");
    ~PerformanceTimer();

    void addMetadata(const std::string& key, const std::string& value);
    void finish();

private:
    std::unique_ptr<PerformanceMeasurement> m_measurement;
    bool m_finished = false;
};

/**
 * Performance logging levels
 */
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

/**
 * Performance log entry
 */
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string category;
    std::string message;
    std::string threadId;
    std::unordered_map<std::string, std::string> context;
};

/**
 * Comprehensive performance monitoring and logging system
 *
 * This class provides:
 * - Performance measurement and timing
 * - Statistical analysis of performance data
 * - Structured logging with categories and levels
 * - Export capabilities for analysis
 * - Thread-safe operations
 */
class PerformanceMonitor : public QObject {
    Q_OBJECT

public:
    /**
     * Get the singleton instance of PerformanceMonitor
     * @return Reference to the singleton instance
     */
    static PerformanceMonitor& getInstance();

    /**
     * Start a performance measurement
     * @param name Measurement name
     * @param category Optional category for grouping
     * @return Unique measurement ID
     */
    int startMeasurement(const std::string& name, const std::string& category = "");

    /**
     * Finish a performance measurement
     * @param measurementId ID returned by startMeasurement
     */
    void finishMeasurement(int measurementId);

    /**
     * Add a completed measurement directly
     * @param measurement Completed measurement data
     */
    void addMeasurement(const PerformanceMeasurement& measurement);

    /**
     * Get performance statistics for a specific measurement name
     * @param name Measurement name
     * @return Performance statistics, or nullptr if not found
     */
    const PerformanceStats* getStats(const std::string& name) const;

    /**
     * Get all performance statistics
     * @return Map of measurement name to statistics
     */
    const std::unordered_map<std::string, PerformanceStats>& getAllStats() const;

    /**
     * Get performance statistics by category
     * @param category Category name
     * @return Vector of statistics for the category
     */
    std::vector<const PerformanceStats*> getStatsByCategory(const std::string& category) const;

    /**
     * Log a message with specified level
     * @param level Log level
     * @param category Log category
     * @param message Log message
     * @param context Optional context data
     */
    void log(LogLevel level, const std::string& category, const std::string& message,
             const std::unordered_map<std::string, std::string>& context = {});

    /**
     * Convenience logging methods
     */
    void logDebug(const std::string& category, const std::string& message,
                  const std::unordered_map<std::string, std::string>& context = {});
    void logInfo(const std::string& category, const std::string& message,
                 const std::unordered_map<std::string, std::string>& context = {});
    void logWarning(const std::string& category, const std::string& message,
                    const std::unordered_map<std::string, std::string>& context = {});
    void logError(const std::string& category, const std::string& message,
                  const std::unordered_map<std::string, std::string>& context = {});
    void logCritical(const std::string& category, const std::string& message,
                     const std::unordered_map<std::string, std::string>& context = {});

    /**
     * Enable or disable performance monitoring
     * @param enabled Whether to enable monitoring
     */
    void setMonitoringEnabled(bool enabled) { m_monitoringEnabled = enabled; }

    /**
     * Check if monitoring is enabled
     * @return true if monitoring is enabled
     */
    bool isMonitoringEnabled() const { return m_monitoringEnabled; }

    /**
     * Enable or disable logging
     * @param enabled Whether to enable logging
     */
    void setLoggingEnabled(bool enabled) { m_loggingEnabled = enabled; }

    /**
     * Check if logging is enabled
     * @return true if logging is enabled
     */
    bool isLoggingEnabled() const { return m_loggingEnabled; }

    /**
     * Set minimum log level
     * @param level Minimum level to log
     */
    void setMinLogLevel(LogLevel level) { m_minLogLevel = level; }

    /**
     * Get minimum log level
     * @return Current minimum log level
     */
    LogLevel getMinLogLevel() const { return m_minLogLevel; }

    /**
     * Enable or disable file logging
     * @param enabled Whether to enable file logging
     * @param filePath Path to log file (empty for default)
     */
    void setFileLoggingEnabled(bool enabled, const std::string& filePath = "");

    /**
     * Clear all performance data
     */
    void clearPerformanceData();

    /**
     * Clear all log entries
     */
    void clearLogEntries();

    /**
     * Export performance data to JSON
     * @param filePath Path to export file
     * @return true if export was successful
     */
    bool exportPerformanceData(const std::string& filePath) const;

    /**
     * Export log entries to JSON
     * @param filePath Path to export file
     * @param maxEntries Maximum number of entries to export (0 = all)
     * @return true if export was successful
     */
    bool exportLogEntries(const std::string& filePath, int maxEntries = 0) const;

    /**
     * Generate performance report
     * @param includeDetails Whether to include detailed measurements
     * @return String containing performance report
     */
    std::string generatePerformanceReport(bool includeDetails = false) const;

    /**
     * Get recent log entries
     * @param maxEntries Maximum number of entries to return
     * @param minLevel Minimum log level to include
     * @return Vector of recent log entries
     */
    std::vector<LogEntry> getRecentLogEntries(int maxEntries = 100, LogLevel minLevel = LogLevel::Debug) const;

    /**
     * Convert log level to string
     * @param level Log level
     * @return String representation
     */
    static std::string logLevelToString(LogLevel level);

    /**
     * Convert string to log level
     * @param levelStr String representation
     * @return Log level
     */
    static LogLevel stringToLogLevel(const std::string& levelStr);

signals:
    /**
     * Emitted when a new performance measurement is completed
     * @param measurement Completed measurement
     */
    void measurementCompleted(const PerformanceMeasurement& measurement);

    /**
     * Emitted when a new log entry is added
     * @param entry Log entry
     */
    void logEntryAdded(const LogEntry& entry);

    /**
     * Emitted when performance statistics are updated
     * @param name Measurement name
     * @param stats Updated statistics
     */
    void statsUpdated(const std::string& name, const PerformanceStats& stats);

private:
    /**
     * Private constructor for singleton pattern
     */
    PerformanceMonitor();

    /**
     * Destructor
     */
    ~PerformanceMonitor();

    /**
     * Deleted copy constructor and assignment operator
     */
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

    /**
     * Write log entry to file
     * @param entry Log entry to write
     */
    void writeLogToFile(const LogEntry& entry);

    /**
     * Get current thread ID as string
     * @return Thread ID string
     */
    std::string getCurrentThreadId() const;

    // Performance monitoring data
    std::unordered_map<int, std::unique_ptr<PerformanceMeasurement>> m_activeMeasurements;
    std::unordered_map<std::string, PerformanceStats> m_performanceStats;
    std::vector<PerformanceMeasurement> m_completedMeasurements;
    int m_nextMeasurementId = 1;

    // Logging data
    std::vector<LogEntry> m_logEntries;
    LogLevel m_minLogLevel = LogLevel::Debug;
    std::string m_logFilePath;
    std::unique_ptr<std::ofstream> m_logFile;

    // Configuration
    bool m_monitoringEnabled = true;
    bool m_loggingEnabled = true;
    bool m_fileLoggingEnabled = false;
    size_t m_maxLogEntries = 10000;
    size_t m_maxMeasurements = 10000;

    // Thread safety
    mutable std::mutex m_performanceMutex;
    mutable std::mutex m_loggingMutex;
};

// Convenience macros for performance measurement
#define PERF_TIMER(name) PerformanceTimer _perf_timer(name)
#define PERF_TIMER_CAT(name, category) PerformanceTimer _perf_timer(name, category)

// Convenience macros for logging
#define LOG_DEBUG(category, message) PerformanceMonitor::getInstance().logDebug(category, message)
#define LOG_INFO(category, message) PerformanceMonitor::getInstance().logInfo(category, message)
#define LOG_WARNING(category, message) PerformanceMonitor::getInstance().logWarning(category, message)
#define LOG_ERROR(category, message) PerformanceMonitor::getInstance().logError(category, message)
#define LOG_CRITICAL(category, message) PerformanceMonitor::getInstance().logCritical(category, message)

#endif // PERFORMANCEMONITOR_H