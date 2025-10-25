#include "performancemonitor.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>

// ============================================================================
// PerformanceTimer Implementation
// ============================================================================

PerformanceTimer::PerformanceTimer(const std::string& name, const std::string& category) {
    if (PerformanceMonitor::getInstance().isMonitoringEnabled()) {
        m_measurement = std::make_unique<PerformanceMeasurement>(name, category);
    }
}

PerformanceTimer::~PerformanceTimer() {
    if (!m_finished && m_measurement) {
        finish();
    }
}

void PerformanceTimer::addMetadata(const std::string& key, const std::string& value) {
    if (m_measurement) {
        m_measurement->addMetadata(key, value);
    }
}

void PerformanceTimer::finish() {
    if (m_measurement && !m_finished) {
        m_measurement->finish();
        PerformanceMonitor::getInstance().addMeasurement(*m_measurement);
        m_finished = true;
    }
}

// ============================================================================
// PerformanceMonitor Implementation
// ============================================================================

PerformanceMonitor& PerformanceMonitor::getInstance() {
    static PerformanceMonitor instance;
    return instance;
}

PerformanceMonitor::PerformanceMonitor() {
    // Initialize default log file path
    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appDataDir);
    m_logFilePath = appDataDir.toStdString() + "/autoslides_performance.log";
}

PerformanceMonitor::~PerformanceMonitor() {
    if (m_logFile && m_logFile->is_open()) {
        m_logFile->close();
    }
}

int PerformanceMonitor::startMeasurement(const std::string& name, const std::string& category) {
    if (!m_monitoringEnabled) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(m_performanceMutex);

    int id = m_nextMeasurementId++;
    m_activeMeasurements[id] = std::make_unique<PerformanceMeasurement>(name, category);

    return id;
}

void PerformanceMonitor::finishMeasurement(int measurementId) {
    if (!m_monitoringEnabled || measurementId < 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_performanceMutex);

    auto it = m_activeMeasurements.find(measurementId);
    if (it != m_activeMeasurements.end()) {
        it->second->finish();
        addMeasurement(*it->second);
        m_activeMeasurements.erase(it);
    }
}

void PerformanceMonitor::addMeasurement(const PerformanceMeasurement& measurement) {
    if (!m_monitoringEnabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_performanceMutex);

    // Add to completed measurements
    m_completedMeasurements.push_back(measurement);

    // Limit the number of stored measurements
    if (m_completedMeasurements.size() > m_maxMeasurements) {
        m_completedMeasurements.erase(m_completedMeasurements.begin());
    }

    // Update statistics
    auto& stats = m_performanceStats[measurement.name];
    if (stats.name.empty()) {
        stats.name = measurement.name;
        stats.category = measurement.category;
    }
    stats.addMeasurement(measurement.durationMs);

    // Emit signals
    emit measurementCompleted(measurement);
    emit statsUpdated(measurement.name, stats);

    // Log performance measurement
    if (m_loggingEnabled) {
        std::ostringstream oss;
        oss << "Performance: " << measurement.name << " completed in "
            << std::fixed << std::setprecision(2) << measurement.durationMs << " ms";

        if (!measurement.metadata.empty()) {
            oss << " [";
            bool first = true;
            for (const auto& [key, value] : measurement.metadata) {
                if (!first) oss << ", ";
                oss << key << "=" << value;
                first = false;
            }
            oss << "]";
        }

        logInfo("Performance", oss.str());
    }
}

const PerformanceStats* PerformanceMonitor::getStats(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_performanceMutex);

    auto it = m_performanceStats.find(name);
    return (it != m_performanceStats.end()) ? &it->second : nullptr;
}

const std::unordered_map<std::string, PerformanceStats>& PerformanceMonitor::getAllStats() const {
    std::lock_guard<std::mutex> lock(m_performanceMutex);
    return m_performanceStats;
}

std::vector<const PerformanceStats*> PerformanceMonitor::getStatsByCategory(const std::string& category) const {
    std::lock_guard<std::mutex> lock(m_performanceMutex);

    std::vector<const PerformanceStats*> result;
    for (const auto& [name, stats] : m_performanceStats) {
        if (stats.category == category) {
            result.push_back(&stats);
        }
    }

    return result;
}

void PerformanceMonitor::log(LogLevel level, const std::string& category, const std::string& message,
                            const std::unordered_map<std::string, std::string>& context) {
    if (!m_loggingEnabled || level < m_minLogLevel) {
        return;
    }

    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.threadId = getCurrentThreadId();
    entry.context = context;

    {
        std::lock_guard<std::mutex> lock(m_loggingMutex);

        // Add to log entries
        m_logEntries.push_back(entry);

        // Limit the number of stored log entries
        if (m_logEntries.size() > m_maxLogEntries) {
            m_logEntries.erase(m_logEntries.begin());
        }

        // Write to file if enabled
        if (m_fileLoggingEnabled) {
            writeLogToFile(entry);
        }
    }

    // Emit signal
    emit logEntryAdded(entry);

    // Also output to console for critical errors
    if (level >= LogLevel::Error) {
        std::cerr << "[" << logLevelToString(level) << "] " << category << ": " << message << std::endl;
    }
}

void PerformanceMonitor::logDebug(const std::string& category, const std::string& message,
                                 const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::Debug, category, message, context);
}

void PerformanceMonitor::logInfo(const std::string& category, const std::string& message,
                                const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::Info, category, message, context);
}

void PerformanceMonitor::logWarning(const std::string& category, const std::string& message,
                                   const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::Warning, category, message, context);
}

void PerformanceMonitor::logError(const std::string& category, const std::string& message,
                                 const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::Error, category, message, context);
}

void PerformanceMonitor::logCritical(const std::string& category, const std::string& message,
                                    const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::Critical, category, message, context);
}

void PerformanceMonitor::setFileLoggingEnabled(bool enabled, const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_loggingMutex);

    m_fileLoggingEnabled = enabled;

    if (!filePath.empty()) {
        m_logFilePath = filePath;
    }

    if (enabled) {
        if (m_logFile) {
            m_logFile->close();
        }
        m_logFile = std::make_unique<std::ofstream>(m_logFilePath, std::ios::app);
        if (!m_logFile->is_open()) {
            std::cerr << "Failed to open log file: " << m_logFilePath << std::endl;
            m_fileLoggingEnabled = false;
        }
    } else {
        if (m_logFile) {
            m_logFile->close();
            m_logFile.reset();
        }
    }
}

void PerformanceMonitor::clearPerformanceData() {
    std::lock_guard<std::mutex> lock(m_performanceMutex);

    m_activeMeasurements.clear();
    m_performanceStats.clear();
    m_completedMeasurements.clear();
}

void PerformanceMonitor::clearLogEntries() {
    std::lock_guard<std::mutex> lock(m_loggingMutex);

    m_logEntries.clear();
}

bool PerformanceMonitor::exportPerformanceData(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(m_performanceMutex);

    try {
        QJsonObject root;
        QJsonArray statsArray;

        for (const auto& [name, stats] : m_performanceStats) {
            QJsonObject statsObj;
            statsObj["name"] = QString::fromStdString(stats.name);
            statsObj["category"] = QString::fromStdString(stats.category);
            statsObj["count"] = stats.count;
            statsObj["totalTimeMs"] = stats.totalTimeMs;
            statsObj["averageTimeMs"] = stats.averageTimeMs;
            statsObj["minTimeMs"] = stats.minTimeMs;
            statsObj["maxTimeMs"] = stats.maxTimeMs;
            statsObj["standardDeviation"] = stats.standardDeviation;

            QJsonArray recentArray;
            for (double measurement : stats.recentMeasurements) {
                recentArray.append(measurement);
            }
            statsObj["recentMeasurements"] = recentArray;

            statsArray.append(statsObj);
        }

        root["performanceStats"] = statsArray;

        QJsonArray measurementsArray;
        for (const auto& measurement : m_completedMeasurements) {
            QJsonObject measurementObj;
            measurementObj["name"] = QString::fromStdString(measurement.name);
            measurementObj["category"] = QString::fromStdString(measurement.category);
            measurementObj["durationMs"] = measurement.durationMs;

            QJsonObject metadataObj;
            for (const auto& [key, value] : measurement.metadata) {
                metadataObj[QString::fromStdString(key)] = QString::fromStdString(value);
            }
            measurementObj["metadata"] = metadataObj;

            measurementsArray.append(measurementObj);
        }

        root["measurements"] = measurementsArray;

        QJsonDocument doc(root);
        QFile file(QString::fromStdString(filePath));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }

        file.write(doc.toJson());
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Failed to export performance data: " << e.what() << std::endl;
        return false;
    }
}

bool PerformanceMonitor::exportLogEntries(const std::string& filePath, int maxEntries) const {
    std::lock_guard<std::mutex> lock(m_loggingMutex);

    try {
        QJsonObject root;
        QJsonArray entriesArray;

        int count = 0;
        int startIndex = (maxEntries > 0 && static_cast<int>(m_logEntries.size()) > maxEntries) ?
                        static_cast<int>(m_logEntries.size()) - maxEntries : 0;

        for (int i = startIndex; i < static_cast<int>(m_logEntries.size()); ++i) {
            const auto& entry = m_logEntries[i];

            QJsonObject entryObj;

            // Convert timestamp to string
            auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            entryObj["timestamp"] = QString::fromStdString(oss.str());

            entryObj["level"] = QString::fromStdString(logLevelToString(entry.level));
            entryObj["category"] = QString::fromStdString(entry.category);
            entryObj["message"] = QString::fromStdString(entry.message);
            entryObj["threadId"] = QString::fromStdString(entry.threadId);

            QJsonObject contextObj;
            for (const auto& [key, value] : entry.context) {
                contextObj[QString::fromStdString(key)] = QString::fromStdString(value);
            }
            entryObj["context"] = contextObj;

            entriesArray.append(entryObj);

            if (maxEntries > 0 && ++count >= maxEntries) {
                break;
            }
        }

        root["logEntries"] = entriesArray;

        QJsonDocument doc(root);
        QFile file(QString::fromStdString(filePath));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }

        file.write(doc.toJson());
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Failed to export log entries: " << e.what() << std::endl;
        return false;
    }
}

std::string PerformanceMonitor::generatePerformanceReport(bool includeDetails) const {
    std::lock_guard<std::mutex> lock(m_performanceMutex);

    std::ostringstream oss;

    oss << "=== Performance Report ===\n\n";

    // Summary statistics
    oss << "Total Measurements: " << m_completedMeasurements.size() << "\n";
    oss << "Unique Operations: " << m_performanceStats.size() << "\n\n";

    // Group by category
    std::unordered_map<std::string, std::vector<const PerformanceStats*>> categorizedStats;
    for (const auto& [name, stats] : m_performanceStats) {
        categorizedStats[stats.category].push_back(&stats);
    }

    for (const auto& [category, statsList] : categorizedStats) {
        oss << "Category: " << (category.empty() ? "Uncategorized" : category) << "\n";
        oss << std::string(50, '-') << "\n";

        // Sort by average time (descending)
        std::vector<const PerformanceStats*> sortedStats = statsList;
        std::sort(sortedStats.begin(), sortedStats.end(),
                 [](const PerformanceStats* a, const PerformanceStats* b) {
                     return a->averageTimeMs > b->averageTimeMs;
                 });

        for (const auto* stats : sortedStats) {
            oss << std::left << std::setw(30) << stats->name
                << " Count: " << std::setw(6) << stats->count
                << " Avg: " << std::fixed << std::setprecision(2) << std::setw(8) << stats->averageTimeMs << "ms"
                << " Min: " << std::setw(8) << stats->minTimeMs << "ms"
                << " Max: " << std::setw(8) << stats->maxTimeMs << "ms";

            if (stats->standardDeviation > 0) {
                oss << " StdDev: " << std::setw(8) << stats->standardDeviation << "ms";
            }

            oss << "\n";

            if (includeDetails && !stats->recentMeasurements.empty()) {
                oss << "  Recent measurements: ";
                int count = 0;
                for (auto it = stats->recentMeasurements.rbegin();
                     it != stats->recentMeasurements.rend() && count < 5; ++it, ++count) {
                    if (count > 0) oss << ", ";
                    oss << std::fixed << std::setprecision(1) << *it << "ms";
                }
                oss << "\n";
            }
        }
        oss << "\n";
    }

    return oss.str();
}

std::vector<LogEntry> PerformanceMonitor::getRecentLogEntries(int maxEntries, LogLevel minLevel) const {
    std::lock_guard<std::mutex> lock(m_loggingMutex);

    std::vector<LogEntry> result;
    result.reserve(std::min(maxEntries, static_cast<int>(m_logEntries.size())));

    int count = 0;
    for (auto it = m_logEntries.rbegin(); it != m_logEntries.rend() && count < maxEntries; ++it) {
        if (it->level >= minLevel) {
            result.push_back(*it);
            count++;
        }
    }

    return result;
}

std::string PerformanceMonitor::logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

LogLevel PerformanceMonitor::stringToLogLevel(const std::string& levelStr) {
    if (levelStr == "DEBUG") return LogLevel::Debug;
    if (levelStr == "INFO") return LogLevel::Info;
    if (levelStr == "WARNING") return LogLevel::Warning;
    if (levelStr == "ERROR") return LogLevel::Error;
    if (levelStr == "CRITICAL") return LogLevel::Critical;
    return LogLevel::Info; // Default
}

void PerformanceMonitor::writeLogToFile(const LogEntry& entry) {
    if (!m_logFile || !m_logFile->is_open()) {
        return;
    }

    try {
        // Format timestamp
        auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            entry.timestamp.time_since_epoch()) % 1000;

        *m_logFile << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        *m_logFile << "." << std::setfill('0') << std::setw(3) << ms.count();
        *m_logFile << " [" << logLevelToString(entry.level) << "]";
        *m_logFile << " [" << entry.threadId << "]";
        *m_logFile << " " << entry.category << ": " << entry.message;

        if (!entry.context.empty()) {
            *m_logFile << " {";
            bool first = true;
            for (const auto& [key, value] : entry.context) {
                if (!first) *m_logFile << ", ";
                *m_logFile << key << "=" << value;
                first = false;
            }
            *m_logFile << "}";
        }

        *m_logFile << std::endl;
        m_logFile->flush();

    } catch (const std::exception& e) {
        std::cerr << "Failed to write log entry to file: " << e.what() << std::endl;
    }
}

std::string PerformanceMonitor::getCurrentThreadId() const {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}