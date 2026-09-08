//
// Created by prodigg on 08.09.26.
//

#ifndef SIMULATEDFACTORY_LOG_H
#define SIMULATEDFACTORY_LOG_H
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>

#include "LogConsumer.h"
#include "../RuntimeEntity.h"

namespace sim::framework {
    /*!
     * @brief this is the central system that handles the logs
     */
    class LoggerHandler_t {
    public:
        uint32_t attachLogConsumer(LogConsumer_t& logConsumer);

        void startLogDispatcherThread();

        /*!
         * @brief write a log, this function is thread safe
         * @param logLevel
         * @param logMessage
         * @param additionalData
         * @param location
         */
        void writeLog(LogLevel_t logLevel, std::string_view logMessage, const nlohmann::json& additionalData, std::source_location location);


    private:
        /*!
         * @brief generate a timestamp
         * @return timestamp
         */
        static std::string generateTimestamp();

        /*!
         * @brief empty the queue and send every log message to the consumers
         */
        void dispatchLog();

        /*!
         * @brief write one log to the consumers
         * @param logLevel
         * @param logMessage
         * @param additionalData
         * @param location
         * @param timestamp
         */
        void writeLogToConsumer(LogLevel_t logLevel, std::string_view logMessage, const nlohmann::json& additionalData, std::source_location location, std::string_view timestamp);

        struct LogConsumerListElement_t {
            uint32_t logConsumerID;
            LogConsumer_t& logConsumer;
        };
        std::mutex m_logConsumerMutex;
        std::vector<LogConsumerListElement_t> m_logConsumers;
        uint32_t m_currentLogConsumerID = 0;

        struct LogEntry_t {
            LogLevel_t logLevel;
            std::string logMessage;
            nlohmann::json additionalData;
            std::source_location location;
            std::string timestamp;
        };

        std::mutex m_logQueueMutex;
        std::queue<LogEntry_t> m_logQueue;
        std::queue<LogEntry_t> m_handleLogQueue; // queue for the dispatching of error messages
        std::optional<std::jthread> m_logDispatcherThread;

    };

    class Logger_t {
    public:
        explicit Logger_t(LoggerHandler_t& loggerHandler) : m_loggerHandler(loggerHandler) {};

        virtual void logTrace(const std::string_view logMessage, const std::source_location location = std::source_location::current()) const { logTrace(logMessage, nlohmann::json(), location); }
        virtual void logTrace(const std::string_view logMessage, const nlohmann::json& additionalData, const std::source_location location = std::source_location::current()) const { m_loggerHandler.writeLog(LogLevel_t::TRACE, logMessage, additionalData, location); }
        virtual void logDebug(const std::string_view logMessage, const std::source_location location = std::source_location::current()) const { logDebug(logMessage, nlohmann::json(), location); }
        virtual void logDebug(const std::string_view logMessage, const nlohmann::json& additionalData, const std::source_location location = std::source_location::current()) const { m_loggerHandler.writeLog(LogLevel_t::DEBUG, logMessage, additionalData, location); }
        virtual void logInfo(const std::string_view logMessage, const std::source_location location = std::source_location::current()) const { logInfo(logMessage, nlohmann::json(), location); }
        virtual void logInfo(const std::string_view logMessage, const nlohmann::json& additionalData, const std::source_location location = std::source_location::current()) const { m_loggerHandler.writeLog(LogLevel_t::INFO, logMessage, additionalData, location); }
        virtual void logWarning(const std::string_view logMessage, const std::source_location location = std::source_location::current()) const { logWarning(logMessage, nlohmann::json(), location); }
        virtual void logWarning(const std::string_view logMessage, const nlohmann::json& additionalData, const std::source_location location = std::source_location::current()) const {m_loggerHandler.writeLog(LogLevel_t::WARNING, logMessage, additionalData, location); }
        virtual void logError(const std::string_view logMessage, const std::source_location location = std::source_location::current()) const { logError(logMessage, nlohmann::json(), location); }
        virtual void logError(const std::string_view logMessage, const nlohmann::json& additionalData, const std::source_location location = std::source_location::current()) const { m_loggerHandler.writeLog(LogLevel_t::ERROR, logMessage, additionalData, location); }
        virtual void logCritical(const std::string_view logMessage, const std::source_location location = std::source_location::current()) const { logCritical(logMessage, nlohmann::json(), location); }
        virtual void logCritical(const std::string_view logMessage, const nlohmann::json& additionalData, const std::source_location location = std::source_location::current()) const { m_loggerHandler.writeLog(LogLevel_t::CRITICAL, logMessage, additionalData, location); }

        virtual ~Logger_t() = default;
    private:
        LoggerHandler_t& m_loggerHandler;
    };

    class EntityLogger_t : public Logger_t {
    public:
        EntityLogger_t(const EntityID_t entityID, LoggerHandler_t& loggerHandler) :
        Logger_t(loggerHandler),
        m_entityID(entityID) {};
    private:
        EntityID_t m_entityID;
    };

    class StdOutLogConsumer_t : public LogConsumer_t {
        public:
        void writeLog(LogLevel_t logLevel, std::string_view logMessage, const nlohmann::json &additionalData,
            const std::source_location location, std::string_view timestamp) override;
    };
}

#endif //SIMULATEDFACTORY_LOG_H
