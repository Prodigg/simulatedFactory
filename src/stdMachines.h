//
// Created by prodigg on 08.06.26.
// has standard implementations for machines
//

#ifndef SIMULATEDFACTORY_STDMACHINES_H
#define SIMULATEDFACTORY_STDMACHINES_H
#include <iostream>
#include <ostream>

#include "Framework.h"

namespace sim {
    template <typename T>
    class objectVoider_t : runtimeEntity_t {
        public:
        objectVoider_t(std::string_view entityName, EntityIOInput_t<T>& in) : runtimeEntity_t(entityName), m_input(in) {}

        void init() override {
        }

        void cycle() override {
            m_input.setReceiverReady(true);
            if (m_input.isObjectPushed())
                m_input.popObject(); // discard input
        }

    private:
        EntityIOInput_t<T>& m_input;
    };

    template <typename T>
    class objectGenerator_t : runtimeEntity_t {
    public:
        objectGenerator_t(const std::string_view entityName, EntityIOOutput_t<T>& in) : runtimeEntity_t(entityName), m_output(in) {}

        void init() override {
        }

        void cycle() override {
            if (m_output.getReceiverReady() && m_spawnFlag) {
                T tmp = m_spawnElement;
                m_output.pushObject(std::move(tmp));
                if (!m_spawnContinuous)
                    m_spawnFlag = false;
            }
        }

        objectGenerator_t& spawnContinuous(const bool continuous) {m_spawnContinuous = continuous; return *this;}

        objectGenerator_t& setSpawnElement(const T& element) {m_spawnElement = element; return *this;}
        objectGenerator_t& setSpawnElement(T&& element) {m_spawnElement = std::move(element); return *this;}

        objectGenerator_t& spawn() {m_spawnFlag = true; return *this;}
    private:
        bool m_spawnFlag = false;
        bool m_spawnContinuous = false;
        T m_spawnElement;
        EntityIOOutput_t<T>& m_output;
    };

    template<typename T>
    class conveyorBelt_t : runtimeEntity_t{
    public:
        explicit conveyorBelt_t(const std::string_view conveyorName, size_t conveyorLength) : runtimeEntity_t(conveyorName){
            belt.assign(conveyorLength);
        };

        void assignInput(EntityIOInput_t<T>& io) {m_ioInput.emplace(io);};
        void assignOutput(EntityIOOutput_t<T>& io) {m_ioOutput.emplace(io);};

        void init() override {
            m_ioMove = getIOProvider().registerInput("move", framework::IOType_t::BOOL);

            getTerminalProvider().registerCMD("forceMove", [&](std::string arg) {
                if (arg == "none") { // stop forcing
                    m_forceState = forceState_t::NO_FORCE;
                } if (arg == "start") {
                    m_forceState = forceState_t::FORCE_START;
                } else if (arg == "stop") {
                    m_forceState = forceState_t::FORCE_STOP;
                } else {
                    std::cerr << "arg invalid. Arg must be start, stop or none" << std::endl;
                }
            });
        }

        void cycle() override {

            if (m_forceState == forceState_t::FORCE_START ||
                (getIOProvider().template readInput<bool>(m_ioMove) && m_forceState != forceState_t::FORCE_STOP)) {
                // set state of input
                if (m_ioInput && !m_belt.empty()) {
                    m_ioInput->setReceiverReady(!m_belt.front().has_value());
                }

                moveElementsOnce();
            }
        }

    private:
        void moveElementsOnce() {
            if (m_belt.empty())
                return; // nothing to do

            // check last element + output
            if (m_ioOutput.has_value() && m_ioOutput->getReceiverReady() && m_belt.back().has_value()) {
                m_ioOutput->pushObject(std::move(*m_belt.back()));
                m_belt.back().reset();
            }

            for (size_t i = m_belt.size() - 2; i >= 1; --i) { // skip first and last element of the vector
                if (m_belt.at(i).has_value() && !m_belt.at(i + 1).has_value()) {
                    m_belt.at(i + 1).swap(m_belt.at(i));
                }
            }

            // check first element + input
            if (m_ioInput.has_value() && m_ioInput->isObjectPushed() && !m_belt.front().has_value()) {
                m_belt.front().emplace(m_ioInput->popObject());
                m_ioInput->setReceiverReady(false);
            }
        }

        enum class forceState_t{
            NO_FORCE,
            FORCE_START,
            FORCE_STOP
        } m_forceState;

        std::vector<std::optional<T>> m_belt;
        framework::IoID_t m_ioMove = 0;
        framework::IoID_t m_ioReverse = 0;
        std::optional<EntityIOInput_t<T>&> m_ioInput;
        std::optional<EntityIOOutput_t<T>&> m_ioOutput;
    };
}

#endif //SIMULATEDFACTORY_STDMACHINES_H

