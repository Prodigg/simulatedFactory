//
// Created by prodigg on 08.09.26.
//

#ifndef SIMULATEDFACTORY_TERMINALSYSTEM_H
#define SIMULATEDFACTORY_TERMINALSYSTEM_H
#include <functional>

#include "RuntimeEntity.h"

namespace sim::framework {
    class TerminalHandler_t {
    public:
        void registerCMD(EntityID_t entityId, std::string_view cmdName, std::function<void(std::string)> func);
        void cycle();
    private:
        friend class TerminalProvider_t;

        struct registeredCMD_t {
            std::string cmdName;
            std::function<void(std::string)> func;
        };

        struct entityCMDRegistry_t {
            EntityID_t entityID;
            std::vector<registeredCMD_t> cmds;
        };

        std::vector<entityCMDRegistry_t> m_entityCMDRegistry;
    };

    class TerminalProvider_t {
    public:
        void inline registerCMD(const std::string_view cmdName, const std::function<void(std::string)> func) const {m_terminalHandler.registerCMD(m_entityID, cmdName, func);};
    private:
        friend class Runtime_t;
        explicit TerminalProvider_t(const EntityID_t entityID, TerminalHandler_t& terminalHandler) : m_terminalHandler(terminalHandler), m_entityID(entityID) {};
        TerminalHandler_t& m_terminalHandler;
        EntityID_t m_entityID;
    };
}

#endif //SIMULATEDFACTORY_TERMINALSYSTEM_H
