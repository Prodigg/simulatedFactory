//
// Created by prodigg on 08.09.26.
//
#include "VisualizationSystem.h"
#include "../Framework.h"

using json = nlohmann::json;

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

    CROW_ROUTE(m_app, "/serverSymbols/<string>")([this](const std::string& request) {
        if (request == "list") {
            std::shared_lock lock(m_registeredServerSymbolsMutex);
            nlohmann::json json;
            for (const auto &[entityName, serverSymbols]: m_registeredServerSymbols | std::views::values) {
                for (const auto & symbol: serverSymbols) {
                    json.emplace_back(nlohmann::json({
                        {"symbolName", symbol.name},
                        {"fullyQualifiedName", symbol.fullyQualifiedName}
                    }));
                }
            }
            return crow::response(200, "application/json", json.dump(3));
        } else {
            return crow::response(400, "unknown request");
        }
    });

    CROW_ROUTE(m_app, "/serverSymbols/<string>/<string>") ([this](const std::string& request, const std::string& serverSymbol) {
        std::pair<std::string, std::string> parsedServerSymbol;
        try {
            parsedServerSymbol = parseFullyQualifiedSymbolName(serverSymbol);

        } catch (std::invalid_argument& e) {
            return crow::response(400, "malformed server symbol");
        }

        EntityID_t serverSymbolEntityID;
        try {
            serverSymbolEntityID = Runtime_t::GetInstance().getEntityID(parsedServerSymbol.first);
        } catch (std::runtime_error &e) {
            return crow::response(400, "unknown entity name");
        }

        std::shared_lock lock(m_registeredServerSymbolsMutex);
        const auto &[entityName, serverSymbols] = m_registeredServerSymbols.at(serverSymbolEntityID);

        const auto it = std::ranges::find_if(serverSymbols, [&](const registeredServerSymbols_t &symbol) {
            return symbol.fullyQualifiedName == serverSymbol;
        });

        if (it == serverSymbols.end()) {
            return crow::response(400, "unknown server symbol name");
        }

        const registeredServerSymbols_t& symbolData = *it;

        if (request == "getDetails") {
            const nlohmann::json json = {
                {"fullyQualifiedName", symbolData.fullyQualifiedName},
                {"symbolName", symbolData.name},
                {
                    "details", {
                        {"description", symbolData.details.serverSymbolDescription},
                        {"allowSubsymbolAccess", symbolData.details.allowSubsymbolAccess},
                        {"readOnly", symbolData.details.readOnly},
                        {"writeOnly", symbolData.details.writeOnly}
                    }
                }
            };
            return crow::response(200, "application/json", json.dump(3));
        }
        if (request == "getInitialValue") {
            return crow::response(200, "application/json", symbolData.initializationValue.dump(3));
        }
        if (request == "getTypeSchema") {
            std::shared_lock lockTypes(m_serverSymbolTypesMutex);
            try {
                const nlohmann::json &typeShema = m_serverSymbolTypes.at(symbolData.typeSchemaName);
                const nlohmann::json json = {
                    {"schemaName", symbolData.typeSchemaName},
                    {"schema", typeShema}
                };
                return crow::response(200, "application/json", json.dump(3));
            } catch (std::out_of_range& e) {
                return crow::response(500, "failed to resolve schema type of id: " + symbolData.typeSchemaName);
            }
        }
        return crow::response(400, "unknown request");
    });

    CROW_WEBSOCKET_ROUTE(m_app, "/ws")
            .onopen([&](crow::websocket::connection& conn){
                std::scoped_lock mux(m_websocketClientMutex);
                m_websocketClients.emplace(&conn);
            })
            .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t){
                {
                    std::scoped_lock mux(m_websocketClientMutex);
                    m_websocketClients.erase(&conn);
                }
                Runtime_t::GetInstance().getLogger().logInfo(std::format("Websocket Closed ({}): {}", conn.get_remote_ip(), reason));
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

void sim::framework::VisualizationHandler_t::dispatchVariableChange() {
    std::lock_guard mux(m_websocketProcessMutex);
    m_websocketProcessCondition.notify_all();
}

sim::framework::VisuID_t sim::framework::VisualizationHandler_t::registerServerSymbol(const EntityID_t entityID,
    const std::string_view symbolName, const serverSymbolDetails_t& details, const nlohmann::json& initializationValue,
    const nlohmann::json& typeSchema, std::optional<std::function<void(nlohmann::json)>> writeCallback) {

    const VisuID_t newID = m_visuIDCounter++;

    std::unique_lock lock(m_registeredServerSymbolsMutex);

    if (!m_registeredServerSymbols.contains(entityID))
        m_registeredServerSymbols.emplace(entityID, registeredServerSymbolsStorage_t(Runtime_t::GetInstance().getEntityName(entityID)));

    auto&[entityName, serverSymbols] = m_registeredServerSymbols.at(entityID);

    if (std::ranges::find_if(
        serverSymbols,
        [&symbolName](const registeredServerSymbols_t &symbol) {
            return symbol.name == symbolName;
        }) != serverSymbols.end())
        throw std::invalid_argument("Name of server symbol is already registered");

    serverSymbols.emplace_back(
        newID,
        std::string(symbolName),
        std::string(entityName + "." + symbolName),
        details,
        initializationValue,
        storeServerSymbolTypeShema(typeSchema),
        writeCallback);
    return newID;
}

bool sim::framework::VisualizationHandler_t::writeServerSymbol(EntityID_t entityID, VisuID_t visuID,
    const nlohmann::json &value) {
    if (!m_registeredServerSymbols.contains(entityID))
        throw std::invalid_argument("unknown entity ID");

    auto&[entityName, serverSymbols] = m_registeredServerSymbols.at(entityID);

    const auto it = std::ranges::find_if(serverSymbols, [visuID](auto elem) {return elem.ID = visuID;});
    if (it == serverSymbols.end())
        throw std::invalid_argument("unknown visualization ID");

    Runtime_t::GetInstance().getLogger().logWarning(std::format("Server Symbol write value is not checked against schema. EntityID={}, VisuID={}", entityID, visuID));
    //TODO: add schema validation

    commitWSVariableChange(it->fullyQualifiedName, value);
    return true;
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

void sim::framework::VisualizationHandler_t::websocketProcessChangesThreadMain() {
    std::unique_lock cvMux(m_websocketProcessMutex);
    m_websocketProcessCondition.wait(cvMux);

    {
        std::scoped_lock mux(m_websocketChangeBufferMutex);
        if (m_websocketChangeBuffer.empty())
            return;
        std::swap(m_websocketChangeBuffer, m_websocketProcessBuffer);
    }

    for (websocketStateChangeData_t & processData: m_websocketProcessBuffer) {
        const std::string msg = formateWSVariableChangeMessage(processData).dump(3);
        std::scoped_lock mux(m_websocketClientMutex);
        for (const auto client: m_websocketClients)
            client->send_text(msg);
    }
    m_websocketProcessBuffer.clear();
}

void sim::framework::VisualizationHandler_t::commitWSVariableChange(websocketStateChangeData_t &&data) {
    std::scoped_lock mux(m_websocketChangeBufferMutex);
    m_websocketChangeBuffer.push_back(std::move(data));
}


nlohmann::json  sim::framework::VisualizationHandler_t::formateWSVariableChangeMessage(websocketStateChangeData_t &data) {
    return {
        {"qualifiedVariableName", data.fullyQualifiedName},
        {"data", data.data}
    };
}

void sim::framework::VisualizationHandler_t::startWSChangeUpdateThread() {
    if (m_websocketUpdateClientsThread.has_value())
        throw std::logic_error("m_websocketUpdateClientsThread is already running");

    m_websocketUpdateClientsThread.emplace([&](const std::stop_token& token) {
        while (!token.stop_requested())
            this->websocketProcessChangesThreadMain();
    });
}

std::pair<std::string, std::string> sim::framework::VisualizationHandler_t::parseFullyQualifiedSymbolName(
    const std::string_view symbolName) {
    const size_t delimiterIndex = symbolName.find_first_of('.');

    if (delimiterIndex == std::string::npos)
        throw std::invalid_argument("symbol name doesnt contain a dot");

    return {std::string(symbolName.substr(0, delimiterIndex)), std::string(symbolName.substr(delimiterIndex + 1))};
}

std::string sim::framework::VisualizationHandler_t::storeServerSymbolTypeShema(const nlohmann::json &typeShema) {
    const std::string hash = std::format("{:x}", std::hash<std::string>()(typeShema.dump()));
    std::unique_lock lock(m_serverSymbolTypesMutex);

    const auto [it, inserted] = m_serverSymbolTypes.try_emplace(hash, typeShema);

    return it->first;
}

void sim::framework::visualizationProvider_t::registerApiCall(std::string apiName,
                                                              const std::function<crow::response(std::string)> &callback) const {
    m_visualizationHandler.registerApiCall(m_entityID, apiName, callback);
}
