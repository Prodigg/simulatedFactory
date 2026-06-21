//
// Created by prodigg on 08.06.26.
// has standard implementations for machines
//

#ifndef SIMULATEDFACTORY_STDMACHINES_H
#define SIMULATEDFACTORY_STDMACHINES_H
#include <iostream>
#include <ostream>
#include <list>

#include "Framework.h"

namespace sim {

    template <typename T>
    class sensorObjectInterface_t {
    public:
        [[nodiscard]] virtual const std::optional<const T>& getSensorObject() const = 0;
        virtual void destroy() = 0;
    };

    template <typename T>
    class objectVoider_t : private runtimeEntity_t {
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
    class objectGenerator_t : private runtimeEntity_t {
    public:
        objectGenerator_t(const std::string_view entityName, EntityIOOutput_t<T>& in) : runtimeEntity_t(entityName), m_output(in) {}

        void init() override {
        }

        void cycle() override {
            if (m_output.getReceiverReady() && m_spawnFlag) {
                // copy object here to preserve object for future generation
                m_output.pushObject(std::move(T(m_spawnElement)));
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
    class conveyorBelt_t : private runtimeEntity_t{
    public:
        explicit conveyorBelt_t(const std::string_view conveyorName, size_t conveyorLength) : runtimeEntity_t(conveyorName){
            m_belt.assign(conveyorLength);
        };

        void assignInput(EntityIOInput_t<T>& io) {m_ioInput.emplace(io);};
        void assignOutput(EntityIOOutput_t<T>& io) {m_ioOutput.emplace(io);};

        sensorObjectInterface_t<T>& getSensorInterface(const size_t sensorPosition) {
            m_sensorInterfaces.emplace_back(*this, ++m_sensorIDCounter, sensorPosition);
            return dynamic_cast<sensorObjectInterface_t<T>&>(m_sensorInterfaces.back());
        }

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

        [[nodiscard]] const std::optional<const T>& getElementAtPosition(size_t position) const {
            return m_belt.at(position);
        };

        void destroySensorInterface(const size_t sensorID) {
            auto it = std::ranges::find_if(m_sensorInterfaces.begin(), m_sensorInterfaces.end(),
                [&](const sensorImpl_t& sensorImpl) {return sensorID == sensorImpl.getSensorID();});

            if (it == m_sensorInterfaces.end())
                throw std::runtime_error("Sensor ID not found cant destroy sensor object");

            m_sensorInterfaces.erase(it);
        }

        enum class forceState_t{
            NO_FORCE,
            FORCE_START,
            FORCE_STOP
        } m_forceState;

        std::vector<std::optional<T>> m_belt;
        framework::IoID_t m_ioMove = 0;
        framework::IoID_t m_ioReverse = 0;
        std::optional<std::reference_wrapper<EntityIOInput_t<T>>> m_ioInput;
        std::optional<std::reference_wrapper<EntityIOOutput_t<T>>> m_ioOutput;

        class sensorImpl_t : public sensorObjectInterface_t<T> {
        public:
            explicit sensorImpl_t(const conveyorBelt_t& conveyorBelt, const size_t sensorID, const size_t beltPositon)
                : m_conveyorBelt(conveyorBelt),
                m_sensorID(sensorID),
                m_beltPositon(beltPositon) {}

            const std::optional<const T>& getSensorObject() const final {
                return m_conveyorBelt.getElementAtPosition(m_beltPositon);
            }

            void destroy() final {
                m_conveyorBelt.destroySensorInterface(m_sensorID);
            }

            [[nodiscard]] size_t getSensorID() const final { return m_sensorID; }

        private:
            const conveyorBelt_t& m_conveyorBelt;
            size_t m_sensorID;
            size_t m_beltPositon;
        };

        std::list<sensorImpl_t> m_sensorInterfaces;
        size_t m_sensorIDCounter = 0;
    };


    namespace internal {
        template <typename T>
        class genericSensor_t : private runtimeEntity_t {
            public:
                explicit genericSensor_t(const std::string_view name) : runtimeEntity_t(name) {}

                void linkSensorInterface(sensorObjectInterface_t<T>& sensorInterface) {
                    if (m_objectInterface)
                        throw std::runtime_error("sensor interface already linked");
                    m_objectInterface.emplace(sensorInterface);
                }

                void unlinkSensorInterface(sensorObjectInterface_t<T>& sensorInterface) {
                    if (!m_objectInterface)
                        return; // interface already unlinked
                    sensorInterface->destroy();
                    sensorInterface.reset();
                }

                ~genericSensor_t() override {
                    unlinkSensorInterface();
                }
            private:

                std::optional<std::reference_wrapper<sensorObjectInterface_t<T>>> m_objectInterface;
        };
    }

    template <typename T>
    class digitalSensor_t : public internal::genericSensor_t<T> {
    public:
        explicit digitalSensor_t(const std::string_view name) : internal::genericSensor_t<T>(name) {}

        void init() override {
            m_outputIO = this->getIOProvider().registerOutput("sensorOutputBool", framework::IOType_t::BOOL);
        }

        void cycle() override {
            if (m_checkFunction && this->m_objectInterface) {
                if (auto sensorElement = this->m_objectInterface->getSensorObject())
                    this->getIOProvider().writeOutput(m_outputIO, m_checkFunction->operator()(sensorElement.value()));
            }
        }

        void setSensorCheckFunction(std::function<bool(const T&)> checkFunction) {
            if (m_checkFunction)
                m_checkFunction.reset();
            m_checkFunction.emplace(checkFunction);
        }

        void unsetSensorCheckFunction() {
            if (m_checkFunction)
                m_checkFunction.reset();
        }

        void setOnSensorTrueCallback(std::function<void(const T&)> callback) {
            m_onSensorTrueCallback.emplace(callback);
        }

        void unsetOnSensorTrueCallback() {
            if (m_onSensorTrueCallback)
                m_onSensorTrueCallback.reset();
        }

    private:
        std::optional<std::function<bool(const T&)>> m_checkFunction;
        std::optional<std::function<void(const T&)>> m_onSensorTrueCallback;
        sim::framework::IoID_t m_outputIO = 0;
    };

}

#endif //SIMULATEDFACTORY_STDMACHINES_H

