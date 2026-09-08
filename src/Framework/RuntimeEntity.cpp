//
// Created by prodigg on 08.09.26.
//
#include "RuntimeEntity.h"
#include "../Framework.h"
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