//
// Created by prodigg on 02.06.26.
//

#ifndef SIMULATEDFACTORY_FRAMEWORK_H
#define SIMULATEDFACTORY_FRAMEWORK_H

#include "Framework/RuntimeEntity.h"
#include "Framework/IOsystem.h"
#include "Framework/TerminalSystem.h"
#include "Framework/VisualizationSystem.h"
#include "Framework/Log/LogConsumer.h"
#include "Framework/Log/Log.h"

namespace sim::framework {

    struct runtimeEntityContext_t {
        EntityID_t entityID;
        IOProvider_t& ioProvider;
        TerminalProvider_t& terminalProvider;
        visualizationProvider_t& visualizationProvider;
        EntityLogger_t& entityLogger;
    };

    struct runtimeEntityEntry {
        EntityID_t entityID;
        std::string entityName;
        runtimeEntity_t& entity;
        IOProvider_t ioProvider;
        TerminalProvider_t terminalProvider;
        visualizationProvider_t visualizationProvider;
        EntityLogger_t entityLogger;
    };

    /*!
     * @brief handles the execution of Machines and IO Systems
     */
    class Runtime_t {
    public:
        Runtime_t(Runtime_t &other) = delete;
        Runtime_t &operator=(Runtime_t &other) = delete;
        static Runtime_t& GetInstance();

        /*!
         *
         */
        void generateConfig(const std::string_view path) const {m_ioHandler.generateConfig(path);}

        /*!
         * @brief configures IOHandler and makes the runtime ready
         */
        void initializeRuntime(std::string_view configPath, bool generateConfig);

        /*!
         * @brief start the runtime, requires to first initialize the runtime and creating all the runtime entities
         */
        void runtimeStart();

        EntityID_t registerEntity(runtimeEntity_t& instance, std::string_view instanceName = "");

        runtimeEntityContext_t getEntityContext(EntityID_t entityID);

        void destroyEntityID(EntityID_t entityID);
        std::string getEntityName(EntityID_t entityID);
        EntityID_t getEntityID(std::string entityName);

        [[nodiscard]] const Logger_t& getLogger() const { return m_logger.value(); }
    protected:
        Runtime_t() = default;

    private:
        IOHandler_t m_ioHandler;

        std::optional<TerminalProvider_t> m_terminalProvider;
        VisualizationHandler_t m_visualizationHandler;
        std::vector<runtimeEntityEntry> m_entries;
        EntityID_t m_entityIdCounter = 1; //! id counter for generating EntityIDs
        TerminalHandler_t m_terminalHandler;

        StdOutLogConsumer_t m_stdOutLogConsumer;
        LoggerHandler_t m_loggerHandler;
        std::optional<Logger_t> m_logger;
    };
}



#endif //SIMULATEDFACTORY_FRAMEWORK_H
