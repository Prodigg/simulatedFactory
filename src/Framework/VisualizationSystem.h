//
// Created by prodigg on 08.09.26.
//

#ifndef SIMULATEDFACTORY_VISUALIZATIONSYSTEM_H
#define SIMULATEDFACTORY_VISUALIZATIONSYSTEM_H
#define CROW_MAIN

#include <crow.h>
#include <queue>
#include <unordered_set>

#include "RuntimeEntity.h"
#include "Log/LogConsumer.h"

namespace sim::framework {
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
         * @param entityName name of the entity that rapports the change
         * @param variableName the variable name of the entity (this is arbitrary) an empty string makes the request invalid and will be discarded
         * @param data arbitrary data the entity wants to send
         */
        void commitWSVariableChange(const std::string& entityName, const std::string& variableName, const nlohmann::json& data) {
            if (!checkWSVariableChangeValid(entityName, variableName))
                return;
            commitWSVariableChange(websocketStateChangeData_t{entityName, variableName, data});
        }

        /*!
         * @brief send data to the websocket clients
         * @param entityName name of the entity that rapports the change
         * @param variableName the variable name of the entity (this is arbitrary) an empty string makes the request invalid and will be discarded
         * @param data arbitrary data the entity wants to send
         */
        void commitWSVariableChange(const std::string& entityName, std::string&& variableName, nlohmann::json&& data) {
            if (checkWSVariableChangeValid(entityName, variableName))
                return;
            commitWSVariableChange(websocketStateChangeData_t{entityName, std::move(variableName), std::move(data)});
        }

        /*!
         * @brief send data to the websocket clients
         * @param entityName name of the entity that rapports the change
         * @param variableName the variable name of the entity (this is arbitrary) an empty string makes the request invalid and will be discarded
         * @param data arbitrary data the entity wants to send
         */
        void commitWSVariableChange(const std::string& entityName, const std::string& variableName, nlohmann::json&& data) {
            if (checkWSVariableChangeValid(entityName, variableName))
                return;
            commitWSVariableChange(websocketStateChangeData_t{entityName, variableName, std::move(data)});
        }

        /*!
         * @brief notify all clients of the commited changes in @ref VisualizationHandler_t::commitWSVariableChange
         */
        void dispatchVariableChange();

    private:
        static crow::response generateServerError(size_t errorID, std::string_view errorMessage);

        static crow::response serveFile(const std::string& path);

        void handleSingleRequest();

        void startWebserver() {
            m_app.port(18080).loglevel(crow::LogLevel::Warning).multithreaded().run_async();
        }

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
            std::string entityName;
            std::string variableName;
            nlohmann::json data;
        };

        void commitWSVariableChange(websocketStateChangeData_t&& data);

        [[nodiscard]] static bool checkWSVariableChangeValid(std::string_view entityName, std::string_view variableName);

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

        //------------ REST API ----------
        std::mutex m_requestQueueMutex;
        std::queue<requestQueueData_t> m_requestQueue; // queue to write the requests to
        std::queue<requestQueueData_t> m_handleQueue; // queue that is read and handled in the cycle

        std::unordered_map<EntityID_t, std::vector<registeredAPICalls_t>> m_registeredAPICalls;

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

    struct serverSymbolDetails_t {
        std::string_view serverSymbolDescription;
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

    class visualizationProvider_t {
    public:
        void registerApiCall(std::string apiName, const std::function<crow::response(std::string)> &callback) const;

        template <ServerSymbolPrimitive T>
        VisuID_t registerServerSymbol(T initializationValue, std::string_view symbolName, serverSymbolDetails_t details, std::function<void(T)> writeCallback);
        VisuID_t registerServerSymbol(nlohmann::json initializationValue, std::string_view symbolName, serverSymbolDetails_t details, nlohmann::json typeShema, std::function<void(nlohmann::json)> writeCallback);

        template <ServerSymbolPrimitive T>
        bool writeServerSymbol(VisuID_t id, T value);
        bool writeServerSymbol(VisuID_t id, nlohmann::json value);

        template <ServerSymbolPrimitive T>
        T readServerSymbolPrimitive(VisuID_t id);
        nlohmann::json readServerSymbol(VisuID_t id);


    private:
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
