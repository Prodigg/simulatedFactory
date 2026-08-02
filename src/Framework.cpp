//
// Created by prodigg on 02.06.26.
//
#include "Framework.h"

#include <fstream>
#include <iostream>
#include <AdsLib/AdsLib.h>
#include <nlohmann/json.hpp>

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

sim::framework::visualizationProvider_t & sim::runtimeEntity_t::getVisualizationProvider() const {
    return sim::framework::Runtime_t::GetInstance().getEntityContext(m_entityID).visualizationProvider;
}

sim::framework::EntityLogger_t & sim::runtimeEntity_t::getLogger() const {
    return sim::framework::Runtime_t::GetInstance().getEntityContext(m_entityID).entityLogger;
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
    } catch (std::runtime_error& e) {
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

void sim::framework::VisualizationHandler_t::initialize() {
    using namespace std::chrono_literals;

    CROW_ROUTE(m_app, "/api/status")([](){
        crow::json::wvalue json;
        json["status"] = "ok";
        return json;
    });

    CROW_ROUTE(m_app, "/")
    ([] {
        return serveFile("./index.html"); //TODO: make served file root configurable
    });

    CROW_ROUTE(m_app, "/assets/<path>")
    ([](const std::string &file)
    {
        return serveFile("./" + file); //TODO: make served file route configurable
    });

    CROW_ROUTE(m_app, "/api/<string>/<string>")
    ([this](const crow::request& req, std::string module, std::string apiCall) {
        std::promise<crow::response> promise;
        auto future = promise.get_future();
        {
            std::scoped_lock lock(m_requestQueueMutex);
            m_requestQueue.push({
                .promise = std::move(promise),
                .module = std::move(module),
                .apiCall = std::move(apiCall),
                .apiCallBody = req.body
            });
        }

        if (future.wait_for(5s) == std::future_status::timeout) {
            // timeout reached... request won't be handled
            Runtime_t::GetInstance().getLogger().logError(std::format("sim thread failed to process request in time. {}/{}", module, apiCall));
            return crow::response(500, "sim thread failed to process request in time");
        }

        return future.get();
    });

    CROW_WEBSOCKET_ROUTE(m_app, "/ws")
            .onopen([&](crow::websocket::connection& conn){
                conn.send_text("hello from websocket");
            })
            .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t){
                Runtime_t::GetInstance().getLogger().logInfo(std::format("Websocket Log Closed ({}): {}", conn.get_remote_ip(), reason));
                conn.close();
            })
            .onmessage([&](crow::websocket::connection& conn, const std::string& data, const bool is_binary){
                if (!is_binary) // Only handle text requests
                    conn.send_text("message received " + data);
                else
                    conn.close("binary mode is not supported", crow::websocket::CloseStatusCode::UnacceptableData);
            });

    CROW_WEBSOCKET_ROUTE(m_app, "/ws/log")
            .onopen([&](crow::websocket::connection& conn){
                {
                    std::scoped_lock mux(m_websocketLogMutex);
                    m_websocketLogClient.emplace(&conn);
                }
                Runtime_t::GetInstance().getLogger().logInfo(std::format("Websocket Log connection opened to: {}", conn.get_remote_ip()));
            })
            .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t){
                {
                    std::scoped_lock mux(m_websocketLogMutex);
                    m_websocketLogClient.erase(&conn);
                }
                Runtime_t::GetInstance().getLogger().logInfo(std::format("Websocket Log Closed ({}): {}", conn.get_remote_ip(), reason));
                conn.close();
            })
            .onmessage([&](crow::websocket::connection& conn, const std::string& data, const bool is_binary){
                Runtime_t::GetInstance().getLogger().logWarning(std::format("Closing Websocket Log Connection due to client writing to it ({})", conn.get_remote_ip()));
                conn.close("route is only for receiving logs", crow::websocket::CloseStatusCode::UnacceptableData);
            });
}

void sim::framework::VisualizationHandler_t::handleRequests() {
    {   // get current requests
        std::scoped_lock lock(m_requestQueueMutex);
        std::swap(m_requestQueue, m_handleQueue);
    }

    // handle all requests
    while (!m_handleQueue.empty())
        handleSingleRequest();
}

void sim::framework::VisualizationHandler_t::registerApiCall(EntityID_t entityID, std::string apiName,
                                                             std::function<crow::response(std::string)> callback) {
    if (m_registeredAPICalls.contains(entityID)) {
        auto& regApiVec = m_registeredAPICalls.at(entityID);
        // check if vector contains duplicate entry
        if (std::ranges::find_if(regApiVec,
                                 [&apiName](auto elem){ return elem.apiCall == apiName;}) != regApiVec.end()) {
            throw std::runtime_error("duplicate api name found. Invalid api name");
        }

        regApiVec.emplace_back(apiName, callback);
        return;
    }

    m_registeredAPICalls.emplace(entityID, std::vector<registeredAPICalls_t>
        {{
            std::move(apiName),
            std::move(callback)
            }
        });
}

void sim::framework::VisualizationHandler_t::writeLog(LogLevel_t logLevel, std::string_view logMessage,
    const nlohmann::json &additionalData, const std::source_location location, std::string_view timestamp) {
    std::scoped_lock mux(m_websocketLogMutex);
    if (m_websocketLogClient.empty())
        return; // return no clients connected

    const nlohmann::json json = {
        {"LogLevel", logLevel},
        {"LogLevelName", logLevelToString(logLevel)},
        {"message", logMessage},
        {"additionalData", additionalData},
        {"timestamp", timestamp},
        {
            "location", {
                {"file", location.file_name()},
                {"line", location.line()},
                {"function", location.function_name()}
            }
        },
    };

    for (const auto client: m_websocketLogClient) {
        client->send_text(json.dump(3));
    }
}

crow::response sim::framework::VisualizationHandler_t::generateServerError(size_t errorID,
                                                                           std::string_view errorMessage) {
    const json err = {
        { "errorID", errorID },
        {"errorMessage", errorMessage }
    };
    return {500, err.dump(3)};
}

crow::response sim::framework::VisualizationHandler_t::serveFile(const std::string &path) {
    std::ifstream file(path, std::ios::binary);

    if (!file)
        return crow::response(404);

    std::ostringstream buffer;
    buffer << file.rdbuf();

    crow::response res;
    res.body = buffer.str();
    return res;
}

void sim::framework::VisualizationHandler_t::handleSingleRequest() {
    auto& request = m_handleQueue.front();
    EntityID_t entityID = 0;
    try {
        entityID = Runtime_t::GetInstance().getEntityID(request.module);
    } catch (std::runtime_error& e) {
        request.promise.set_value(generateServerError(1001, e.what()));
        m_handleQueue.pop();
        return;
    }

    // resolve api call
    if (!m_registeredAPICalls.contains(entityID)) {
        request.promise.set_value(generateServerError(1002, "entity has no api calls registered"));
        m_handleQueue.pop();
        return;
    }

    auto& registeredApiCallVec = m_registeredAPICalls.at(entityID);
    const auto it = std::ranges::find_if(registeredApiCallVec,
            [&request](const registeredAPICalls_t& elem){return elem.apiCall == request.apiCall;});
    if (it == registeredApiCallVec.end()) {
        request.promise.set_value(generateServerError(
            1003,
            "entity has no api call that has the name: " + request.apiCall));
        m_handleQueue.pop();
        return;
    }

    // call the api callback
    try {
        request.promise.set_value(it->apiCallback(request.apiCallBody));
    } catch (std::exception& e) {
        request.promise.set_value(generateServerError(1004, e.what()));
    }

    m_handleQueue.pop();
}

void sim::framework::visualizationProvider_t::registerApiCall(std::string apiName,
    const std::function<crow::response(std::string)> &callback) const {
    m_visualizationHandler.registerApiCall(m_entityID, apiName, callback);
}

uint32_t sim::framework::LoggerHandler_t::attachLogConsumer(LogConsumer_t &logConsumer) {
    std::scoped_lock mux(m_logConsumerMutex);
    m_logConsumers.emplace_back(++m_currentLogConsumerID, logConsumer);
    return m_currentLogConsumerID;
}

void sim::framework::LoggerHandler_t::startLogDispatcherThread() {
    m_logDispatcherThread.emplace([this](const std::stop_token &stopToken) {
        while (!stopToken.stop_requested()) {
            dispatchLog();
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });
}

void sim::framework::LoggerHandler_t::writeLog(const LogLevel_t logLevel, const std::string_view logMessage,
                                               const nlohmann::json &additionalData, const std::source_location location) {
    std::scoped_lock mux(m_logQueueMutex);
    m_logQueue.emplace(logLevel, std::string(logMessage), additionalData, location, generateTimestamp());
}

void sim::framework::LoggerHandler_t::dispatchLog() {
    {
        std::scoped_lock mux(m_logQueueMutex);
        if (m_logConsumers.empty())
            return; // nothing to do, log is empty
        std::swap(m_logQueue, m_handleLogQueue);
    }

    std::scoped_lock mux(m_logConsumerMutex);
    while (!m_handleLogQueue.empty()) {
        auto&[logLevel, logMessage, additionalData, location, timestamp] = m_handleLogQueue.front();
        writeLogToConsumer(logLevel, logMessage, additionalData, location, timestamp);
        m_handleLogQueue.pop();
    }
}

std::string sim::framework::LoggerHandler_t::generateTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};

    localtime_r(&time, &tm);

    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ) % 1000;

    std::ostringstream timestamp;
    timestamp
        << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.'
        << std::setfill('0')
        << std::setw(3)
        << milliseconds.count();

    return timestamp.str();
}

void sim::framework::LoggerHandler_t::writeLogToConsumer(LogLevel_t logLevel, std::string_view logMessage,
    const nlohmann::json &additionalData, std::source_location location, std::string_view timestamp) {
    for (const auto &[logConsumerID, logConsumer]: m_logConsumers) {
        logConsumer.writeLog(logLevel, logMessage, additionalData, location, timestamp);
    }
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
    m_readConfig = configData;

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

    std::stringstream ss;
    ss << localNetID;
    std::string localNetIdStr = ss.str();
    Runtime_t::GetInstance().getLogger().logInfo(
        std::format("IOHandler_t: Connected to PLC: {} ({}) : {}", ipV4, localNetIdStr, port)
        );
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

    Runtime_t::GetInstance().getLogger().logInfo("ads read and write objects initialized");
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
    m_readConfig = config.getConfig();

    initialize(config.remoteIpV4(), config.getRemoteNetId(), config.getLocalNetId(), config.remotePort());
}

void sim::framework::StdOutLogConsumer_t::writeLog(
    LogLevel_t logLevel,
    std::string_view logMessage,
    const nlohmann::json& additionalData,
    const std::source_location location,
    std::string_view timestamp)
{
    constexpr auto ANSI_RESET = "\033[0m";
    constexpr auto ANSI_BOLD  = "\033[1m";

    constexpr auto ANSI_CYAN        = "\033[36m";
    constexpr auto ANSI_BLUE        = "\033[34m";
    constexpr auto ANSI_GREEN       = "\033[32m";
    constexpr auto ANSI_YELLOW      = "\033[33m";
    constexpr auto ANSI_RED         = "\033[31m";
    constexpr auto ANSI_BRIGHT_RED  = "\033[91m";

    constexpr auto TRACE     = "[TRACE   ]";
    constexpr auto DEBUG     = "[DEBUG   ]";
    constexpr auto INFO      = "[INFO    ]";
    constexpr auto WARNING   = "[WARNING ]";
    constexpr auto ERROR     = "[ERROR   ]";
    constexpr auto CRITICAL  = "[CRITICAL]";

    const char* levelText = nullptr;
    const char* levelColor = nullptr;

    switch (logLevel)
    {
        case LogLevel_t::TRACE:
            levelText = TRACE;
            levelColor = ANSI_CYAN;
            break;

        case LogLevel_t::DEBUG:
            levelText = DEBUG;
            levelColor = ANSI_BLUE;
            break;

        case LogLevel_t::INFO:
            levelText = INFO;
            levelColor = ANSI_GREEN;
            break;

        case LogLevel_t::WARNING:
            levelText = WARNING;
            levelColor = ANSI_YELLOW;
            break;

        case LogLevel_t::ERROR:
            levelText = ERROR;
            levelColor = ANSI_RED;
            break;

        case LogLevel_t::CRITICAL:
            levelText = CRITICAL;
            levelColor = ANSI_BRIGHT_RED;
            break;
    }


    std::cout
        << timestamp
        << " "
        << levelColor
        << ANSI_BOLD
        << levelText
        << ANSI_RESET
        << " ["
        << location.file_name()
        << ":"
        << location.line()
        << " "
        << location.function_name()
        << "] "
        << logMessage;
    std::cout << std::endl;
}

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
    }
}

sim::framework::EntityID_t sim::framework::Runtime_t::registerEntity(runtimeEntity_t &instance, const std::string_view instanceName) {
    m_entries.emplace_back(
        m_entityIdCounter,
        std::string(instanceName),
        instance,
        IOProvider_t(m_entityIdCounter, m_ioHandler),
        TerminalProvider_t(m_entityIdCounter, m_terminalHandler),
        visualizationProvider_t(m_entityIdCounter, m_visualizationHandler),
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
