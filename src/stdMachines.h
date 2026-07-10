//
// Created by prodigg on 08.06.26.
// has standard implementations for machines
//

#ifndef SIMULATEDFACTORY_STDMACHINES_H
#define SIMULATEDFACTORY_STDMACHINES_H
#include <iostream>
#include <ostream>
#include <list>
#include <bits/chrono.h>

#include "Framework.h"

namespace sim {

    template <typename T>
    class sensorObjectInterface_t {
    public:
        virtual ~sensorObjectInterface_t() = default;

        [[nodiscard]] virtual const std::optional<T>& getSensorObject() const = 0;
        virtual void destroy() = 0;
    };

    template <typename T>
    class objectVoider_t : private runtimeEntity_t {
        public:
        explicit objectVoider_t(std::string_view entityName) : runtimeEntity_t(entityName) {}

        void init() override {
        }

        void cycle() override {
            if (!m_input.isConnected())
                return; // do nothing if no input is connected

            m_input->setReceiverReady(true);
            if (m_input->isObjectPushed())
                m_input->popObject(); // discard input
        }

        EntityIOInput_t<T>& getInput() {return m_input;}
    private:
        EntityIOInput_t<T> m_input;
    };

    template <typename T>
    class objectGenerator_t : protected runtimeEntity_t {
    public:
        explicit objectGenerator_t(const std::string_view entityName) : runtimeEntity_t(entityName) {}

        void init() override {
        }

        void cycle() override {
            if (!m_output.isConnected())
                return; // do nothing if nothing is connected

            if (m_output->getReceiverReady() && m_spawnFlag && std::chrono::steady_clock::now() >= m_spawnTime + m_lastSpawn) {
                m_lastSpawn = std::chrono::steady_clock::now();
                // copy object here to preserve object for future generation
                m_output->pushObject(std::move(T(m_spawnElement)));
                if (!m_spawnContinuous)
                    m_spawnFlag = false;
            }
        }

        objectGenerator_t& spawnContinuous(const bool continuous) {m_spawnContinuous = continuous; return *this;}

        objectGenerator_t& setSpawnElement(const T& element) {m_spawnElement = element; return *this;}
        objectGenerator_t& setSpawnElement(T&& element) {m_spawnElement = std::move(element); return *this;}

        objectGenerator_t& setSpawnInterval(const std::chrono::steady_clock::duration duration) {
            m_lastSpawn = std::chrono::steady_clock::now();
            m_spawnTime = duration;
            return *this;
        }

        objectGenerator_t& spawn() {m_spawnFlag = true; return *this;}

        EntityIOOutput_t<T>& getOutput() {return m_output;}
    private:
        bool m_spawnFlag = false;
        bool m_spawnContinuous = false;
        std::chrono::steady_clock::time_point m_lastSpawn;
        std::chrono::steady_clock::duration m_spawnTime = std::chrono::steady_clock::duration::zero();
        T m_spawnElement;
        EntityIOOutput_t<T> m_output;
    };

    namespace internal {
        template <typename T>
        class linearItemMovement_t : protected runtimeEntity_t {
        public:
            explicit linearItemMovement_t(const std::string_view elementName, size_t linearItemsArrayLength) :
            runtimeEntity_t(elementName) {
                m_items.assign(linearItemsArrayLength, {});
            }

            [[nodiscard]] inline EntityIOInput_t<T>& getInput() {return m_ioInput;}
            [[nodiscard]] inline EntityIOOutput_t<T>& getOutput() {return m_ioOutput;}


            void init() override {

            }

            void cycle() override {
                if (m_moveElementsOnce || (m_moveElements && std::chrono::steady_clock::now() >= m_moveTime + m_lastMove)) {
                    m_lastMove = std::chrono::steady_clock::now();
                    m_moveElementsOnce = false;

                    // set state of input
                    if (m_ioInput.isConnected() && !m_items.empty()) {
                        m_ioInput->setReceiverReady(!m_items.front().has_value());
                    }
                    moveElementsInItemsOnce();
                }
            }

        protected:
            /*!
             * @brief set a move interval that is checked if moveElements is true
             * @param duration interval between movements
             */
            void setMoveInterval(const std::chrono::steady_clock::duration duration) {
                m_moveTime = duration;
                m_lastMove = std::chrono::steady_clock::now();
            }

            /*!
             * @brief get the current set move time
             * @return moveTime
             */
            [[nodiscard]] inline std::chrono::steady_clock::duration getMoveTime() const {
                return m_moveTime;
            }

            /*!
             * @brief get position of a element in the items array
             * @param position position in items array
             * @return reference to optional element
             */
            [[nodiscard]] const std::optional<T>& getElementAtPosition(size_t position) const {
                return m_items.at(position);
            };

            /*!
             * @brief move the elements on the next cycle once
             */
            virtual void moveElementsOnce() {m_moveElementsOnce = true;}

            /*!
             * @brief start or stop movement on the elements
             * @param move should elements move
             */
            void moveElements(const bool move) {
                m_moveElements = move;
                m_lastMove = std::chrono::steady_clock::now();
            }
        private:
            void moveElementsInItemsOnce() {
                if (m_items.empty())
                    return; // nothing to do

                // check last element + output
                if (m_ioOutput.isConnected() && m_ioOutput->getReceiverReady() && m_items.back().has_value()) {
                    m_ioOutput->pushObject(std::move(*m_items.back()));
                    m_items.back().reset();
                }

                for (int i = m_items.size() - 2; i >= 0; --i) { // skip last element of the vector
                    if (m_items.at(i).has_value() && !m_items.at(i + 1).has_value()) {
                        m_items.at(i + 1).swap(m_items.at(i));
                    }
                }

                // check first element + input
                if (m_ioInput.isConnected() && m_ioInput->isObjectPushed() && !m_items.front().has_value()) {
                    m_items.front().emplace(m_ioInput->popObject());
                    m_ioInput->setReceiverReady(false);
                }
            }

            std::vector<std::optional<T>> m_items;
            EntityIOInput_t<T> m_ioInput;
            EntityIOOutput_t<T> m_ioOutput;
            std::chrono::steady_clock::time_point m_lastMove;
            std::chrono::steady_clock::duration m_moveTime = std::chrono::steady_clock::duration::zero();
            bool m_moveElementsOnce = false;
            bool m_moveElements = false;
        };
    }

    template<typename T>
    class conveyorBelt_t : public internal::linearItemMovement_t<T>{
    public:
        explicit conveyorBelt_t(const std::string_view conveyorName, size_t conveyorLength) :
            internal::linearItemMovement_t<T>(conveyorName, conveyorLength),
            m_forceState(forceState_t::NO_FORCE) {};

        sensorObjectInterface_t<T>& getSensorInterface(const size_t sensorPosition) {
            m_sensorInterfaces.emplace_back(*this, ++m_sensorIDCounter, sensorPosition);
            return dynamic_cast<sensorObjectInterface_t<T>&>(m_sensorInterfaces.back());
        }

        void init() override {
            m_ioMove = this->getIOProvider().registerInput("move", framework::IOType_t::BOOL);

            this->getTerminalProvider().registerCMD("forceMove", [&](std::string arg) {
                if (arg == "none") { // stop forcing
                    m_forceState = forceState_t::NO_FORCE;
                } else if (arg == "start") {
                    m_forceState = forceState_t::FORCE_START;
                } else if (arg == "stop") {
                    m_forceState = forceState_t::FORCE_STOP;
                } else {
                    std::cerr << "arg invalid. Arg must be start, stop or none" << std::endl;
                }
            });
            internal::linearItemMovement_t<T>::init();
        }

        void cycle() override {
            bool moveConveyor = m_forceState == forceState_t::FORCE_START ||
                (this->getIOProvider().template readInput<bool>(m_ioMove) && m_forceState != forceState_t::FORCE_STOP);
            this->moveElements(moveConveyor);
            internal::linearItemMovement_t<T>::cycle();
        }

        using internal::linearItemMovement_t<T>::setMoveInterval;
        using internal::linearItemMovement_t<T>::getMoveTime;

    private:
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

        framework::IoID_t m_ioMove = 0;
        framework::IoID_t m_ioReverse = 0;

        class sensorImpl_t : public sensorObjectInterface_t<T> {
        public:
            explicit sensorImpl_t(conveyorBelt_t& conveyorBelt, const size_t sensorID, const size_t beltPositon)
                : m_conveyorBelt(conveyorBelt),
                m_sensorID(sensorID),
                m_beltPositon(beltPositon) {}

            const std::optional<T>& getSensorObject() const final {
                return m_conveyorBelt.getElementAtPosition(m_beltPositon);
            }

            void destroy() final {
                m_conveyorBelt.destroySensorInterface(m_sensorID);
            }

            [[nodiscard]] size_t getSensorID() const { return m_sensorID; }

        private:
            conveyorBelt_t& m_conveyorBelt;
            size_t m_sensorID;
            size_t m_beltPositon;
        };

        std::list<sensorImpl_t> m_sensorInterfaces;
        size_t m_sensorIDCounter = 0;
    };


    namespace internal {
        template <typename T>
        class genericSensor_t : protected runtimeEntity_t {
            public:
                explicit genericSensor_t(const std::string_view name) : runtimeEntity_t(name) {}

                void linkSensorInterface(sensorObjectInterface_t<T>& sensorInterface) {
                    if (m_objectInterface)
                        throw std::runtime_error("sensor interface already linked");
                    m_objectInterface.emplace(sensorInterface);
                }

                void unlinkSensorInterface() {
                    if (!m_objectInterface)
                        return; // interface already unlinked
                    m_objectInterface.value().get().destroy();
                    m_objectInterface.reset();
                }

                ~genericSensor_t() override {
                    unlinkSensorInterface();
                }
            protected:

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
                if (auto sensorElement = this->m_objectInterface.value().get().getSensorObject()) {
                    bool isSensorTrue = (*m_checkFunction)(sensorElement.value());
                    this->getIOProvider().writeOutput(m_outputIO, isSensorTrue);
                    if (m_onSensorTrueCallback && isSensorTrue)
                        (*m_onSensorTrueCallback)(sensorElement.value());
                } else {
                    this->getIOProvider().writeOutput(m_outputIO, false);
                }
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

