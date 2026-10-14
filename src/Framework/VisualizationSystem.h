//
// Created by prodigg on 08.09.26.
//

#ifndef SIMULATEDFACTORY_VISUALIZATIONSYSTEM_H
#define SIMULATEDFACTORY_VISUALIZATIONSYSTEM_H
#define CROW_MAIN

#include <crow.h>
#include <queue>
#include <unordered_set>
#include <shared_mutex>

#include "RuntimeEntity.h"
#include "Log/LogConsumer.h"

namespace sim::framework {
    struct serverSymbolDetails_t {
        std::string serverSymbolDescription;
        bool readOnly; // only allow reading from server side
        bool writeOnly; // only allow writing from server side
        bool allowSubsymbolAccess; // allows access to subcomponents of a complex symbol
    };

    template <typename T>
    concept ServerSymbolPrimitive =
        std::is_arithmetic_v<T> ||
        std::is_enum_v<T> ||
        std::is_same_v<T, std::string> ||
        std::is_same_v<T, std::string_view>;

    class VisualizationHandler_t : public LogConsumer_t {
    public:
        /*!
         * @brief initialize the visualization handler
         */
        void initialize();

        /*!
         * @brief start the visualization handler and webserver
         */
        void start() {
            m_webserverThread.emplace([&]() {this->startWebserver();});
            startWSChangeUpdateThread();
        }

        /*!
         * @brief handle all api request received and queued
         */
        void handleRequests();

        /*!
         * @brief register an api call
         * @param entityID entity ID of registering entity
         * @param apiName name of endpoint to call
         * @param callback function to handle the api call
         * @throws std::runtrime_error if apiName is duplicate
         */
        void registerApiCall(EntityID_t entityID, std::string apiName, std::function<crow::response(std::string)> callback);

        void writeLog(LogLevel_t logLevel, std::string_view logMessage, const nlohmann::json &additionalData,
            std::source_location location, std::string_view timestamp) override;

        /*!
         * @brief send data to the websocket clients
         * @param fullyQualifiedName fully qualified name of serverVariable
         * @param data arbitrary data the entity wants to send
         */
        void commitWSVariableChange(const std::string& fullyQualifiedName, const nlohmann::json& data) {
            commitWSVariableChange(websocketStateChangeData_t{fullyQualifiedName, data});
        }

        /*!
         * @brief send data to the websocket clients
         * @param fullyQualifiedName fully qualified name of serverVariable
         * @param data arbitrary data the entity wants to send
         */
        void commitWSVariableChange(std::string&& fullyQualifiedName, nlohmann::json&& data) {
            commitWSVariableChange(websocketStateChangeData_t{std::move(fullyQualifiedName), std::move(data)});
        }

        /*!
         * @brief send data to the websocket clients
         * @param fullyQualifiedName fully qualified name of serverVariable
         * @param data arbitrary data the entity wants to send
         */
        void commitWSVariableChange(const std::string& fullyQualifiedName, nlohmann::json&& data) {
            commitWSVariableChange(websocketStateChangeData_t{fullyQualifiedName, std::move(data)});
        }

        /*!
         * @brief notify all clients of the commited changes in @ref VisualizationHandler_t::commitWSVariableChange
         */
        void dispatchVariableChange();

        /*!
         * @brief registers server symbol for visualization server
         * @throws std::invalid_argument if an identical symbol is registered with the same name
         * @param entityID entity ID of registering entity
         * @param symbolName name of server symbol to register
         * @param details detailed information about the server symbol
         * @param initializationValue initial value of the server symbol
         * @param typeSchema schema of value to verify
         * @param writeCallback callback to call if the value is written to
         * @return visualization id to interact with it
         */
        VisuID_t registerServerSymbol(EntityID_t entityID, std::string_view symbolName,
            const serverSymbolDetails_t& details,
            const nlohmann::json& initializationValue,
            const nlohmann::json& typeSchema,
            std::optional<std::function<void(nlohmann::json)>> writeCallback);

        /*!
         * @brief write a value to a server symbol
         * @throws std::invalid_argument if an entity id is not known or a visualization id is unknown
         * @param entityID entity ID of entity
         * @param visuID visualization id of server symbol
         * @param value value to write
         * @return if value is not according to schema
         */
        bool writeServerSymbol(EntityID_t entityID, VisuID_t visuID, const nlohmann::json &value);
    private:
        static crow::response generateServerError(size_t errorID, std::string_view errorMessage);

        static crow::response serveFile(const std::string& path);

        void handleSingleRequest();

        void startWebserver();

        void websocketProcessChangesThreadMain();

        struct requestQueueData_t {
            std::promise<crow::response> promise;
            std::string module;
            std::string apiCall;
            std::string apiCallBody;
        };

        struct registeredAPICalls_t {
            std::string apiCall;
            std::function<crow::response(std::string)> apiCallback;
        };

        struct websocketStateChangeData_t {
            std::string fullyQualifiedName;
            nlohmann::json data;
        };

        void commitWSVariableChange(websocketStateChangeData_t&& data);

        /*!
         * @brief formate ws variable change message
         * @param data data to formate
         */
        [[nodiscard]] inline static nlohmann::json formateWSVariableChangeMessage(websocketStateChangeData_t& data);

        /*!
        * @brief start the Websocket change update thread. may only be called once
        * @throws std::logic_error when thread has already been started
        */
        void startWSChangeUpdateThread();

        /*!
         * @brief parses the fully qualified name of a server symbol
         * @throws std::invalid_argument if symbolName is not parsable
         * @param symbolName fully qualified name to parse
         * @return first: entityName second: symbolName
         */
        static std::pair<std::string, std::string> parseFullyQualifiedSymbolName(std::string_view symbolName);

        /*!
         * @brief inserts a type shema into the typeShema storage. This generates an access name.
         * @param typeShema json type shema
         * @return fully qualified name of typeShema
         */
        std::string storeServerSymbolTypeShema(const nlohmann::json& typeShema);

        VisuID_t m_visuIDCounter = 1;

        // ----------- crow error handler ---------

        class CrowLogger_t : public crow::ILogHandler {
        public:
            void log(const std::string& message, crow::LogLevel level) final;
        };

        CrowLogger_t m_crowLogger;

        //------------ REST API ----------
        std::mutex m_requestQueueMutex;
        std::queue<requestQueueData_t> m_requestQueue; // queue to write the requests to
        std::queue<requestQueueData_t> m_handleQueue; // queue that is read and handled in the cycle

        std::unordered_map<EntityID_t, std::vector<registeredAPICalls_t>> m_registeredAPICalls;

        //------------ Server Symbol ----------

        struct registeredServerSymbols_t {
            VisuID_t ID;
            std::string name;
            std::string fullyQualifiedName;
            serverSymbolDetails_t details;
            nlohmann::json initializationValue;
            std::string typeSchemaName;
            std::optional<std::function<void(nlohmann::json)>> writeCallback;
        };

        struct registeredServerSymbolsStorage_t {
            std::string entityName; // cache entity name for fast lookup
            std::vector<registeredServerSymbols_t> serverSymbols;
        };

        std::shared_mutex m_registeredServerSymbolsMutex;
        std::unordered_map<EntityID_t, registeredServerSymbolsStorage_t> m_registeredServerSymbols;

        std::shared_mutex m_serverSymbolTypesMutex;
        std::unordered_map<std::string, nlohmann::json> m_serverSymbolTypes;

        //------------ Webserver ----------
        crow::SimpleApp m_app;
        std::optional<std::jthread> m_webserverThread;

        //------------ websocket ---------
        std::mutex m_websocketClientMutex;
        std::unordered_set<crow::websocket::connection*> m_websocketClients;

        std::mutex m_websocketChangeBufferMutex;
        std::vector<websocketStateChangeData_t> m_websocketChangeBuffer;
        std::vector<websocketStateChangeData_t> m_websocketProcessBuffer;

        std::mutex m_websocketProcessMutex;
        std::condition_variable m_websocketProcessCondition;
        std::optional<std::jthread> m_websocketUpdateClientsThread;

        //------------ websocket logs ----------
        std::mutex m_websocketLogMutex;
        std::unordered_set<crow::websocket::connection*> m_websocketLogClient;

    };

    class visualizationProvider_t {
    public:
        /*!
         * @brief register an api endpoint
         * @param apiName name of api endpoint
         * @param callback callback to call if the api endpoint is accessed
         */
        void registerApiCall(std::string apiName, const std::function<crow::response(std::string)> &callback) const;

        /*!
         * @brief register a server symbol
         * @throws std::invalid_argument if an identical symbol is registered with the same name
         * @tparam T server symbol type
         * @param initializationValue initial value of server symbol
         * @param symbolName name of the server symbol
         * @return visualization id to interact with variable
         */
        template <ServerSymbolPrimitive T>
        [[nodiscard]] VisuID_t registerServerSymbol(T initializationValue, std::string_view symbolName) const {
            return registerServerSymbol(initializationValue, symbolName,
                {.serverSymbolDescription = "", .readOnly = false, .writeOnly = false, .allowSubsymbolAccess = false});
        }

        /*!
         * @brief register a server symbol
         * @throws std::invalid_argument if an identical symbol is registered with the same name
         * @tparam T server symbol type
         * @param initializationValue initial value of server symbol
         * @param symbolName name of the server symbol
         * @param details detailed information about the server symbol
         * @return visualization id to interact with variable
         */
        template <ServerSymbolPrimitive T>
        [[nodiscard]] VisuID_t registerServerSymbol(T initializationValue, const std::string_view symbolName, const serverSymbolDetails_t details) const {
            return m_visualizationHandler.registerServerSymbol(m_entityID, symbolName, details,
                converToJson(initializationValue), generatePrimitiveSchema(initializationValue), {});
        }

        /*!
         * @brief register a server symbol
         * @throws std::invalid_argument if an identical symbol is registered with the same name
         * @tparam T server symbol type
         * @param initializationValue initial value of server symbol
         * @param symbolName name of the server symbol
         * @param writeCallback callback to call if the server symbol is written to
         * @return visualization id to interact with variable
         */
        template <ServerSymbolPrimitive T>
        [[nodiscard]] VisuID_t registerServerSymbol(T initializationValue, std::string_view symbolName, std::function<void(T)> writeCallback) const {
            return registerServerSymbol(initializationValue, symbolName,
                {.serverSymbolDescription = "", .readOnly = false, .writeOnly = false, .allowSubsymbolAccess = false},
                writeCallback);
        }

        /*!
         * @brief register a server symbol
         * @throws std::invalid_argument if an identical symbol is registered with the same name
         * @tparam T server symbol type
         * @param initializationValue initial value of server symbol
         * @param symbolName name of server symbol
         * @param details detailed information about the server symbol
         * @param writeCallback callback to call if the server symbol is written to
         * @return visualization id to interact with variable
         */
        template <ServerSymbolPrimitive T>
        [[nodiscard]] VisuID_t registerServerSymbol(T initializationValue, std::string_view symbolName, serverSymbolDetails_t details, std::function<void(T)> writeCallback) const {
            return m_visualizationHandler.registerServerSymbol(m_entityID, symbolName, details,
                converToJson(initializationValue), generatePrimitiveSchema(initializationValue), writeCallback);
        }

        /*!
         * @brief register a server symbol
         * @throws std::invalid_argument if an identical symbol is registered with the same name
         * @param initializationValue initial value of server symbol
         * @param symbolName name of server symbol
         * @param typeShema type schema of server symbol
         * @return visualization id to interact with variable
         */
        [[nodiscard]] VisuID_t registerServerSymbol(const nlohmann::json &initializationValue, const std::string_view symbolName, const nlohmann::json &typeShema) const {
            return registerServerSymbol(initializationValue, symbolName, {.serverSymbolDescription = "", .readOnly = false, .writeOnly = false, .allowSubsymbolAccess = false}, typeShema);
        }

        /*!
         * @brief register a server symbol
         * @throws std::invalid_argument if an identical symbol is registered with the same name
         * @param initializationValue initial value of server symbol
         * @param symbolName name of server symbol
         * @param details detailed information about the server symbol
         * @param typeShema type schema of server symbol
         * @return visualization id to interact with variable
         */
        [[nodiscard]] VisuID_t registerServerSymbol(const nlohmann::json &initializationValue, const std::string_view symbolName, const serverSymbolDetails_t &details, const nlohmann::json &typeShema) const {
            return m_visualizationHandler.registerServerSymbol(m_entityID, symbolName, details, initializationValue, typeShema, {});
        }

        /*!
         * @brief register a server symbol
         * @throws std::invalid_argument if an identical symbol is registered with the same name
         * @param initializationValue initial value of server symbol
         * @param symbolName name of server symbol
         * @param typeShema type schema of server symbol
         * @param writeCallback callback to call if the server symbol is written to
         * @return visualization id to interact with variable
         */
        [[nodiscard]] VisuID_t registerServerSymbol(const nlohmann::json &initializationValue, const std::string_view symbolName, const nlohmann::json &typeShema, const std::function<void(nlohmann::json)> &writeCallback) const {
            return registerServerSymbol(initializationValue, symbolName, {.serverSymbolDescription = "", .readOnly = false, .writeOnly = false, .allowSubsymbolAccess = false}, typeShema, writeCallback);
        }

        /*!
         * @brief register a server symbol
         * @throws std::invalid_argument if an identical symbol is registered with the same name
         * @param initializationValue initial value of server symbol
         * @param symbolName name of server symbol
         * @param details detailed information about the server symbol
         * @param typeShema type schema of server symbol
         * @param writeCallback callback to call if the server symbol is written to
         * @return visualization id to interact with variable
         */
        [[nodiscard]] VisuID_t registerServerSymbol(const nlohmann::json &initializationValue, const std::string_view symbolName, const serverSymbolDetails_t &details, const nlohmann::json &typeShema, const std::function<void(nlohmann::json)>& writeCallback) const {
            return m_visualizationHandler.registerServerSymbol(m_entityID, symbolName, details, initializationValue, typeShema, writeCallback);
        }

        /*!
         * @brief write to server symbol
         * @throws std::invalid_argument if an entity id is not known or a visualization id is unknown
         * @tparam T server symbol type
         * @param id id of server symbol to write
         * @param value value to write
         * @return if value was allowed to be written according to type
         */
        template <ServerSymbolPrimitive T>
        bool writeServerSymbol(const VisuID_t id, T value) const {
            return writeServerSymbol(id, converToJson(value));
        }

        /*!
         * @brief write to server symbol
         * @throws std::invalid_argument if an entity id is not known or a visualization id is unknown
         * @param id id of server symbol to write
         * @param value value to write
         * @return if value was according to schema
         */
        bool writeServerSymbol(const VisuID_t id, const nlohmann::json &value) const {
            return m_visualizationHandler.writeServerSymbol(m_entityID, id, value);
        }

        template <ServerSymbolPrimitive T>
        [[nodiscard]] T readServerSymbol(VisuID_t id);
        [[nodiscard]] nlohmann::json readServerSymbolComplex(VisuID_t id);

        [[nodiscard]] VisuID_t registerServerEvent(std::string_view eventName, std::string_view description);
        [[nodiscard]] VisuID_t registerServerEvent(std::string_view eventName);
        bool emitServerEvent(VisuID_t id);
    private:

        template <ServerSymbolPrimitive T>
        static nlohmann::json converToJson(T value) {
            return nlohmann::json(value);
        }
        template <ServerSymbolPrimitive T>
        static nlohmann::json generatePrimitiveSchema(T value) {
            return nlohmann::json({});
        }

        friend class Runtime_t;
        visualizationProvider_t(const EntityID_t entityID, const std::string& entityName,
            VisualizationHandler_t& visualizationHandler) :
        m_entityID(entityID), m_entityName(entityName), m_visualizationHandler(visualizationHandler) {};

        EntityID_t m_entityID;
        std::string m_entityName;
        VisualizationHandler_t& m_visualizationHandler;
    };
}

#endif //SIMULATEDFACTORY_VISUALIZATIONSYSTEM_H
