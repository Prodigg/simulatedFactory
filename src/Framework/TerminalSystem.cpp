//
// Created by prodigg on 08.09.26.
//
#include "TerminalSystem.h"

#if __linux__
#include "../osSpecific/LinuxSpecific.h"
#else
#error "Unsupported platform"
#endif

#include <iostream>

#include "../Framework.h"

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
    for (const auto &[cmdName, func]: it->cmds) {
        std::cerr << cmdName << "\n";
    }
    std::cerr.flush();
}