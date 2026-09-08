//
// Created by prodigg on 02.06.26.
//
#include "Framework.h"

#include <iostream>
#include <nlohmann/json.hpp>

sim::framework::Runtime_t& sim::framework::Runtime_t::GetInstance() {
    static Runtime_t inst; // created on first call
    return inst;
}

void sim::framework::Runtime_t::initializeRuntime(const std::string_view configPath, const bool generateConfig) {
    m_logger.emplace(m_loggerHandler);
    m_loggerHandler.attachLogConsumer(m_stdOutLogConsumer);
    m_loggerHandler.attachLogConsumer(m_visualizationHandler);
    m_loggerHandler.startLogDispatcherThread();
    m_logger->logInfo("Logger initialized");

    for (const runtimeEntityEntry & entry: m_entries) {
        entry.entity.init();
    }
    if (generateConfig) {
        m_ioHandler.generateConfig(configPath);
        m_logger->logInfo(std::string("Config written to: ") + configPath);
        exit(EXIT_SUCCESS);
    }

    m_ioHandler.readConfig(configPath);

    m_terminalProvider.emplace(TerminalProvider_t(0, m_terminalHandler));

    m_terminalProvider->registerCMD("modules", [&](std::string arg) {
        if (arg == "list") {
            std::cout << "\nList all entities:\n";
            for (const runtimeEntityEntry & entry: m_entries) {
                std::cout << entry.entityID << ": " << entry.entityName << "\n";
            }
            std::cout.flush();
        } else {
            std::cerr << arg << " is not a recognised argument" << std::endl;
        }
    });

    m_visualizationHandler.initialize();

    m_visualizationHandler.registerApiCall(0, "getConfig", [&](std::string body) {
        return m_ioHandler.getConfig().dump(3);
    });

    m_visualizationHandler.start();
}

void sim::framework::Runtime_t::runtimeStart() {
    while (true) {
        m_ioHandler.readWriteData();
        m_terminalHandler.cycle();
        m_visualizationHandler.handleRequests();
        for (const runtimeEntityEntry & entry: m_entries) {
            entry.entity.cycle();
        }
        m_visualizationHandler.dispatchVariableChange();
    }
}

sim::framework::EntityID_t sim::framework::Runtime_t::registerEntity(runtimeEntity_t &instance, const std::string_view instanceName) {
    m_entries.emplace_back(
        m_entityIdCounter,
        std::string(instanceName),
        instance,
        IOProvider_t(m_entityIdCounter, m_ioHandler),
        TerminalProvider_t(m_entityIdCounter, m_terminalHandler),
        visualizationProvider_t(m_entityIdCounter, std::string(instanceName), m_visualizationHandler),
        EntityLogger_t(m_entityIdCounter, m_loggerHandler));

    return m_entityIdCounter++;
}

sim::framework::runtimeEntityContext_t sim::framework::Runtime_t::getEntityContext(EntityID_t entityID) {
    const auto it = std::ranges::find_if(m_entries, [entityID](const auto& entry) { return entry.entityID == entityID; });

    if (it == m_entries.end())
        throw std::runtime_error("Entity ID not found");

    return {
        .entityID = entityID,
        .ioProvider = it->ioProvider,
        .terminalProvider = it->terminalProvider,
        .visualizationProvider = it->visualizationProvider,
        .entityLogger = it->entityLogger
    };
}

std::string sim::framework::Runtime_t::getEntityName(EntityID_t entityID) {
    if (entityID == 0)
        return "runtime";
    if (const auto it = std::ranges::find_if(m_entries, [entityID](const auto& entry) { return entry.entityID == entityID; }); it != m_entries.end())
        return it->entityName;
    throw std::runtime_error("Entity ID not found");
}

sim::framework::EntityID_t sim::framework::Runtime_t::getEntityID(std::string entityName) {
    if (entityName == "runtime")
        return 0;
    if (const auto it = std::ranges::find_if(m_entries, [entityName](const auto& entry) { return entry.entityName == entityName; }); it != m_entries.end())
        return it->entityID;
    throw std::runtime_error("Entity name not found");
}
