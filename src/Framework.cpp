//
// Created by prodigg on 02.06.26.
//
#include "Framework.h"

#include <fstream>
#include <iostream>
#include <AdsLib/AdsLib.h>
#include <nlohmann/json.hpp>
#include <oneapi/tbb/task_group.h>

#if __linux__
#include "osSpecific/LinuxSpecific.h"
#else
#error "Unsupported platform"
#endif

using json = nlohmann::json;

sim::runtimeEntity_t::runtimeEntity_t(const std::string_view entityName) {
    m_entityID = getRuntime().registerEntity(*this, entityName);
}

sim::framework::Runtime_t & sim::runtimeEntity_t::getRuntime() {
    return framework::Runtime_t::GetInstance();
}

sim::framework::IOProvider_t & sim::runtimeEntity_t::getIOProvider() const {
    return sim::framework::Runtime_t::GetInstance().getEntityContext(m_entityID).ioProvider;
}

sim::framework::TerminalProvider_t & sim::runtimeEntity_t::getTerminalProvider() const {
    return sim::framework::Runtime_t::GetInstance().getEntityContext(m_entityID).terminalProvider;
}

std::string sim::framework::IOHandler_t::getFullyQualifiedName(const EntityID_t entityId, IoID_t inputIoID) {
    std::string_view ioName;
    if (!m_ioMap.contains(entityId))
        throw std::runtime_error("unable to find IO name for entity id ");
    auto& ioMap= m_ioMap.at(entityId).ioMap;

    if (const auto it = std::ranges::find_if(ioMap, [inputIoID](const IOMapEntry_t& entry) { return inputIoID == entry.ioID;}); it != ioMap.end())
        ioName = it->ioName;

    if (ioName.empty())
        throw std::runtime_error("unable to find IO name for entity id ");

    std::string fullyQualifiedName;
    fullyQualifiedName += Runtime_t::GetInstance().getEntityName(entityId);
    fullyQualifiedName += "::";
    fullyQualifiedName += ioName;
    return fullyQualifiedName;
}

sim::framework::IoID_t sim::framework::IOProvider_t::registerInput(const std::string& inputName, const IOType_t type) const {
    return m_ioHandler.registerInput(m_entityID, inputName, type);
}

sim::framework::IoID_t sim::framework::IOProvider_t::registerOutput(const std::string &outputName, const IOType_t type) const {
    return m_ioHandler.registerOutput(m_entityID, outputName, type);
}

sim::framework::IOProvider_t::IOProvider_t(const EntityID_t entityID, IOHandler_t& ioHandler)
    : m_ioHandler(ioHandler),
    m_entityID(entityID) {
}

void sim::framework::TerminalHandler_t::registerCMD(EntityID_t entityId, std::string_view cmdName, std::function<void(std::string)> func) {
    // find registry of entity
    if (auto it = std::ranges::find_if(
        m_entityCMDRegistry,
        [entityId](const entityCMDRegistry_t &elem) -> bool { return elem.entityID == entityId;});
        it != m_entityCMDRegistry.end()) {

        // check if element already exists
        if (std::ranges::find_if(it->cmds, [cmdName](const registeredCMD_t &elem) { return elem.cmdName == std::string(cmdName);}) == it->cmds.end())
            throw std::runtime_error(std::string("cmd name is already registered. Name: ") + cmdName);

        it->cmds.emplace_back(std::string(cmdName), func);
        return;
    }

    // construct new entry
    std::vector<registeredCMD_t> tmp({ {std::string(cmdName), func}});
    m_entityCMDRegistry.emplace_back(entityId, tmp);
}

void sim::framework::TerminalHandler_t::cycle() {
    if (!internal::line_available())
        return; // nothing to do
    std::string cmd;
    std::getline(std::cin, cmd);

    if (cmd.empty())
        return;

    if (cmd == "help") {
        std::cout << "Usage: [module name] [cmd name] [args]" << std::endl;
        return;
    }

    //TODO: add help cmd

    const size_t moduleDelimiterPos = cmd.find_first_of(' ');

    if (moduleDelimiterPos == std::string::npos) {
        std::cerr << "Command malformed.\n" << "Usage: [module name] [cmd name] [args]" << std::endl;
        return;
    }

    const std::string moduleName = cmd.substr(0, moduleDelimiterPos);
    cmd.erase(0, moduleDelimiterPos + 1);

    EntityID_t entityID;
    // find module name
    try {
        entityID = Runtime_t::GetInstance().getEntityID(moduleName);
    } catch (std::runtime_error e) {
        std::cerr << "Failed to find module name: " << moduleName << std::endl;
        return;
    }

    const auto& it = std::ranges::find_if(m_entityCMDRegistry, [entityID](const entityCMDRegistry_t& elem){return elem.entityID == entityID; });
    if (it == m_entityCMDRegistry.end()) {
        std::cerr << "module does not have any registered cmds" << std::endl;
        return;
    }

    // parse cmd name
    std::string cmdName;
    const size_t cmdNameDelimiterPos = cmd.find_first_of(' ');
    if (cmdNameDelimiterPos == std::string::npos) {
        cmdName = cmd;
        cmd.clear();
    } else {
        cmdName = cmd.substr(0, cmdNameDelimiterPos);
        cmd.erase(0, cmdNameDelimiterPos + 1);
    }

    // find cmd name and call it
    if (const auto it2 = std::ranges::find_if(it->cmds, [cmdName](const registeredCMD_t &elem) {return elem.cmdName == cmdName;}); it2 != it->cmds.end()) {
        it2->func(cmd);
        return;
    }
    std::cerr << "cmd name is unknown for the selected module\nAvailable cmds:\n";
    for (const registeredCMD_t & cmd_: it->cmds) {
        std::cerr << cmd_.cmdName << "\n";
    }
    std::cerr.flush();
}

void sim::framework::config_t::generateConfig(const std::string_view path, const std::unordered_map<EntityID_t, IOEntityRegistry_t> &ioMap) {
    if (path.empty())
        throw std::runtime_error("configuration file name is empty");

    json config = {
        {
            "AdsConfig",
            {
                {"AmsNetIdRemote", ""},
                {"AmsNetIdLocal", ""},
                {"RemoteIpV4", ""},
                {"RemoteAdsPort", 851}
            }
        },
        {
            "AdsSymbolMapping",
            {}
        }
    };

    for (const auto& [entityID, registry]: ioMap) {
        std::string entityName = Runtime_t::GetInstance().getEntityName(entityID);
        for (const auto& entry: registry.ioMap)
            config.at("AdsSymbolMapping").push_back({{entityName + "::" + entry.ioName, ""},{"type", ioTypeToString(entry.type)}});
    }

    std::ofstream file{std::string(path)};
    if (!file.is_open())
        throw std::runtime_error("could not open configuration file");

    file << config.dump(4);

    file.close();
}

void sim::framework::config_t::applyConfig(const std::string_view path, std::unordered_map<std::string, ioToAdsMapElement_t>& ioMap) {
    if (path.empty())
        throw std::runtime_error("configuration file name is empty");

    std::ifstream file{std::string(path)};

    if (!file.is_open()) {
        throw std::runtime_error("could not open configuration file ");
    }

    json configData = json::parse(file);

    file.close();

    // parse AdsConfig

    if (!configData.contains("AdsConfig"))
        throw std::runtime_error("No AdsConfig");

    if (!configData.at("AdsConfig").contains("AmsNetIdLocal"))
        throw std::runtime_error("AmsNetIdLocal not inside AdsConfig");
    m_localNetId = ConfigNetId_t(configData.at("AdsConfig").at("AmsNetIdLocal"));

    if (!configData.at("AdsConfig").contains("AmsNetIdRemote"))
        throw std::runtime_error("AmsNetIdRemote not inside AdsConfig");
    m_remoteNetId = ConfigNetId_t(configData.at("AdsConfig").at("AmsNetIdRemote"));

    if (!configData.at("AdsConfig").contains("RemoteAdsPort"))
        throw std::runtime_error("RemoteAdsPort not inside AdsConfig");
    m_remotePort = configData.at("AdsConfig").at("RemoteAdsPort");

    if (!configData.at("AdsConfig").contains("RemoteIpV4"))
        throw std::runtime_error("RemoteIpV4 not inside AdsConfig");
    m_remoteIpV4 = configData.at("AdsConfig").at("RemoteIpV4");

    // parse variables
    if (!configData.contains("AdsSymbolMapping"))
        throw std::runtime_error("AdsSymbolMapping not inside AdsConfig");

    for (const auto& element: configData.at("AdsSymbolMapping")) {
        if (!element.contains("type"))
            throw std::runtime_error("type missing inside AdsSymbolMapping array");

        if (element.size() != 2)
            throw std::runtime_error("element inside AdsSymbolMapping array has incorrect number of elements inside");

        for (const auto& elem: element.items()) {
            if (elem.key() != "type") {
                ioToAdsMapElement_t tmp = {elem.value(), stringToIoType(std::string(element.at("type")))};
                ioMap.insert_or_assign(elem.key(), std::move(tmp));
            }
        }
    }
}

std::string sim::framework::config_t::ioTypeToString(IOType_t type) {
    switch (type) {
        case IOType_t::INVALID:
            throw std::runtime_error("invalid IOType_t");
        case IOType_t::BOOL:
            return "BOOL";
        case IOType_t::INT8:
            return "INT8";
        case IOType_t::INT16:
            return "INT16";
        case IOType_t::INT32:
            return "INT32";
        case IOType_t::INT64:
            return "INT64";
        case IOType_t::UINT8:
            return "UINT8";
        case IOType_t::UINT16:
            return "UINT16";
        case IOType_t::UINT32:
            return "UINT32";
        case IOType_t::UINT64:
            return "UINT64";
        case IOType_t::FLOAT32:
            return "FLOAT32";
        case IOType_t::FLOAT64:
            return "FLOAT64";
        default:
            throw std::runtime_error("Unknown IOType_t type");
    }
}

sim::framework::IOType_t sim::framework::config_t::stringToIoType(const std::string_view type) {
    if (type == "BOOL")
        return IOType_t::BOOL;
    if (type == "INT8")
        return IOType_t::INT8;
    if (type == "INT16")
        return IOType_t::INT16;
    if (type == "INT32")
        return IOType_t::INT32;
    if (type == "INT64")
        return IOType_t::INT64;
    if (type == "UINT8")
        return IOType_t::UINT8;
    if (type == "UINT16")
        return IOType_t::UINT16;
    if (type == "UINT32")
        return IOType_t::UINT32;
    if (type == "UINT64")
        return IOType_t::UINT64;
    if (type == "FLOAT32")
        return IOType_t::FLOAT32;
    if (type == "FLOAT64")
        return IOType_t::FLOAT64;

    throw std::runtime_error(
        std::string("Unknown IOType_t string: ") + std::string(type));
}

sim::framework::IOHandler_t::~IOHandler_t() {
    if (m_adsWrite)
        m_adsWrite.reset();
    if (m_adsRead)
        m_adsRead.reset();
    if (m_route)
        m_route.reset();
}

sim::framework::IoID_t sim::framework::IOHandler_t::registerInput(const EntityID_t entityId, const std::string& inputName, IOType_t type) {
    if (m_adsRead)
        throw std::runtime_error("IOHandler already initialized, unable to register new inputs");

    // check if we find a existing entity entry
    if (m_ioMap.contains(entityId)) {
        auto& ioMap = m_ioMap.at(entityId).ioMap;
        IoID_t ioId = (ioMap.back().ioID + 1) | 1 << 31;
        ioMap.emplace_back(ioId, inputName, type);
        return ioId;
    }

    const std::vector<IOMapEntry_t> ioMap = {{static_cast<IoID_t>(1) | (1 << 31), inputName, type}};
    m_ioMap.emplace(entityId, IOEntityRegistry_t{entityId, ioMap});
    return static_cast<IoID_t>(1) | (1 << 31);
}

sim::framework::IoID_t sim::framework::IOHandler_t::registerOutput(EntityID_t entityId, std::string outputName, IOType_t type) {
    if (m_adsWrite)
        throw std::runtime_error("IOHandler already initialized, unable to register new inputs");

    // check if we find a existing entity entry
    if (m_ioMap.contains(entityId)) {
        auto& ioMap = m_ioMap.at(entityId).ioMap;
        IoID_t outputBitMask = 0xFFFFFFFF;
        outputBitMask = outputBitMask >> 1;
        IoID_t ioId = (ioMap.back().ioID + 1) & outputBitMask;
        ioMap.emplace_back(ioId, outputName, type);
        return ioId;
    }

    std::vector<IOMapEntry_t> ioMap = {{1, outputName, type}};
    m_ioMap.emplace(entityId, IOEntityRegistry_t{entityId, ioMap});
    return 1;
}

void sim::framework::IOHandler_t::initialize(std::string_view ipV4, AmsNetId remoteNetID, const AmsNetId localNetID, uint16_t port) {

    // initialize ads route
    bhf::ads::SetLocalAddress(localNetID);
    m_route.emplace(std::string(ipV4), remoteNetID, port);

    std::cout << "IOHandler_t: Connected to PLC: " << std::string(ipV4) << " (" << localNetID << ") : " << port << std::endl;

    std::vector<std::string> readList;
    std::vector<std::string> writeList;

    // find write / read list
    for (const auto &[entityID, registry]: m_ioMap) {
        std::string entryName = Runtime_t::GetInstance().getEntityName(entityID);
        for (const IOMapEntry_t & entry: registry.ioMap) {
            std::string fullyQualifiedName = entryName + "::" + entry.ioName;
            if (!m_ioToAdsMap.contains(fullyQualifiedName) || m_ioToAdsMap.at(fullyQualifiedName).adsName.empty())
                continue; // skip entry will be ignored

            if (entry.ioID >> 31 == 1)
                readList.push_back(m_ioToAdsMap.at(fullyQualifiedName).adsName);
            else
                writeList.push_back(m_ioToAdsMap.at(fullyQualifiedName).adsName);
        }
    }

    m_adsWrite.emplace(*m_route, writeList);
    m_adsRead.emplace(*m_route, readList);

    std::cout << "IOHandler_t: ads read and write objects initialized." << std::endl;
}

void sim::framework::IOHandler_t::readWriteData() {
    if (!m_adsWrite || !m_adsRead)
        throw std::runtime_error("readWriteData called before initialising ads");
    m_adsWrite->write();
    m_adsRead->read();
}

void sim::framework::IOHandler_t::readConfig(const std::string_view path) {
    config_t config;
    config.applyConfig(path, m_ioToAdsMap);

    initialize(config.remoteIpV4(), config.getRemoteNetId(), config.getLocalNetId(), config.remotePort());
}

sim::framework::Runtime_t& sim::framework::Runtime_t::GetInstance() {
    static Runtime_t inst; // created on first call
    return inst;
}

void sim::framework::Runtime_t::initializeRuntime(std::string_view configPath, bool generateConfig) {
    for (const runtimeEntityEntry & entry: m_entries) {
        entry.entity.init();
    }
    if (generateConfig) {
        m_ioHandler.generateConfig(configPath);
        std::cout << "Config written to: " << configPath << std::endl;
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
            return;
        } else {
            std::cerr << arg << " is not a recognised argument" << std::endl;
        }
    });

    //m_ioHandler.m_ioToAdsMap.emplace("testEntity::test", "NodeRed.bIsLightOn");
    //m_ioHandler.initialize(ipV4, remoteNetID, localNetID, port);
}

void sim::framework::Runtime_t::runtimeStart() {


    while (true) {
        m_ioHandler.readWriteData();
        m_terminalHandler.cycle();
        for (const runtimeEntityEntry & entry: m_entries) {
            entry.entity.cycle();
        }
    }
}

sim::framework::EntityID_t sim::framework::Runtime_t::registerEntity(runtimeEntity_t &instance, const std::string_view instanceName) {
    m_entries.emplace_back(
        m_entityIdCounter,
        std::string(instanceName),
        instance,
        IOProvider_t(m_entityIdCounter, m_ioHandler),
        TerminalProvider_t(m_entityIdCounter, m_terminalHandler));

    return m_entityIdCounter++;
}

sim::framework::runtimeEntityContext_t sim::framework::Runtime_t::getEntityContext(EntityID_t entityID) {
    const auto it = std::ranges::find_if(m_entries, [entityID](const auto& entry) { return entry.entityID == entityID; });

    if (it == m_entries.end())
        throw std::runtime_error("Entity ID not found");

    return {entityID,
        it->ioProvider,
        it->terminalProvider};
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
