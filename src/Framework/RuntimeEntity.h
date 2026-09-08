//
// Created by prodigg on 08.09.26.
//

#ifndef SIMULATEDFACTORY_RUNTIMEENTITY_H
#define SIMULATEDFACTORY_RUNTIMEENTITY_H

#include <cstdint>
#include <utility>
#include <string_view>
#include <optional>

namespace sim::framework {
    class Runtime_t;
    class IOHandler_t;
    class IOProvider_t;
    struct runtimeEntityContext_t;
    class TerminalProvider_t;
    class visualizationProvider_t;
    class EntityLogger_t;

    using EntityID_t = uint32_t;

    // the last bit (31) is 0 if output and 1 if input
    // test |= 1 << 31;
    // test >> 31 == 1;
    using IoID_t = uint32_t;

    using VisuID_t = uint32_t;
}

namespace sim {
    /*!
     * @brief the base class every entity inherits
     */
    class runtimeEntity_t {
    public:
        virtual ~runtimeEntity_t() = default;
        explicit runtimeEntity_t(std::string_view entityName);

        /*!
         * @brief is called at the start of the simulation to allow initialization by the runtime
         */
        void virtual init() = 0;

        /*!
         * @brief is called every cycle by the runtime
         */
        void virtual cycle() = 0;

    protected:
        [[nodiscard]] framework::EntityID_t getEntityID() const { return m_entityID; };
        [[nodiscard]] static framework::Runtime_t& getRuntime();
        [[nodiscard]] framework::IOProvider_t& getIOProvider() const;
        [[nodiscard]] framework::TerminalProvider_t& getTerminalProvider() const;
        [[nodiscard]] framework::visualizationProvider_t& getVisualizationProvider() const;
        [[nodiscard]] framework::EntityLogger_t& getLogger() const;
    private:
        framework::EntityID_t m_entityID;
    };

    namespace framework::internal {
        template<typename T>
        class EntityIOInputInterface_t {
        public:
            virtual ~EntityIOInputInterface_t() = default;

            T virtual popObject() = 0;
            void virtual setReceiverReady(bool val) = 0;
            [[nodiscard]] bool virtual isObjectPushed() const = 0;
        };

        template<typename T>
        class EntityIOOutputInterface_t {
        public:
            virtual ~EntityIOOutputInterface_t() = default;

            void virtual pushObject(T&& object) = 0;
            [[nodiscard]] bool virtual getReceiverReady() const = 0;
        };

        template<typename T>
        class EntityIOAdapterElement_t : public EntityIOOutputInterface_t<T>, public EntityIOInputInterface_t<T> {
        public:
            [[nodiscard]] bool getReceiverReady() const override {return m_receiverReady;};
            void setReceiverReady(const bool val) override {m_receiverReady = val;};
            void pushObject(T&& object) override {m_objectBuffer.emplace(std::move(object));};
            T popObject() override {
                if (!m_objectBuffer.has_value())
                    throw std::runtime_error("cannot get object when no object pushed");
                T tmp = std::move(m_objectBuffer.value());
                m_objectBuffer.reset();
                return tmp;
            }
            [[nodiscard]] bool isObjectPushed() const override {return m_objectBuffer.has_value();}

        private:
            bool m_receiverReady = false;
            std::optional<T> m_objectBuffer;
        };

        template<typename T, template <typename> class TT>
        class GenericEntityIOClass_t {
        public:
            bool connect(TT<T>& ioOutput) {
                if (m_genericInterface)
                    return false;
                m_genericInterface.emplace(ioOutput);
                return true;
            };

            bool disconnect() {
                if (!m_genericInterface)
                    return false;

                m_genericInterface.reset();
                return true;
            };

            [[nodiscard]] TT<T> & get() {
                if (!m_genericInterface)
                    throw std::runtime_error("no output interface available");
                return m_genericInterface.value();
            }

            [[nodiscard]] inline bool isConnected() { return m_genericInterface.has_value(); }

            TT<T>* operator->() {return &get();}
            TT<T>& operator*() {return get();}
        private:
            std::optional<std::reference_wrapper<TT<T>>> m_genericInterface;
        };
    }

    template<typename T>
    using EntityIOInput_t = framework::internal::GenericEntityIOClass_t<T, framework::internal::EntityIOInputInterface_t>;
    template<typename T>
    using EntityIOOutput_t = framework::internal::GenericEntityIOClass_t<T, framework::internal::EntityIOOutputInterface_t>;

    template<typename T>
    class EntityIOAdapter_t {
    public:
        EntityIOAdapter_t() = default;
        EntityIOAdapter_t(EntityIOOutput_t<T>& ioOutput, EntityIOInput_t<T>& ioInput) {connect(ioOutput, ioInput);}

        /*!
         * @brief connects input and output together
         * @param ioInput inputElement
         * @param ioOutput outputElement
         * @throws std::runtime_error if connection state of ioInput, ioOutput or this adapter is connected to something
         */
        void connect(EntityIOOutput_t<T>& ioOutput, EntityIOInput_t<T>& ioInput) {
            if (m_connected)
                throw std::runtime_error("cannot connect already connected");

            if (ioInput.isConnected())
                throw std::runtime_error("cannot connect to an already connected input");
            if (ioOutput.isConnected())
                throw std::runtime_error("cannot connect to an already connected output");

            ioInput.connect(m_ioAdapter);
            ioOutput.connect(m_ioAdapter);
            m_connected = true;
        }

        /*!
         * @brief disconnects input and output
         * @param ioInput inputElement
         * @param ioOutput outputElement
         * @throws std::runtime_error if connection state of ioInput, ioOutput or this adapter is disconnected
         */
        void disconnect(EntityIOOutput_t<T>& ioOutput, EntityIOInput_t<T>& ioInput) {
            if (!m_connected)
                throw std::runtime_error("cannot disconnect already disconnected");

            if (!ioInput.isConnected())
                throw std::runtime_error("cannot disconnect to an already disconnected input");
            if (!ioOutput.isConnected())
                throw std::runtime_error("cannot disconnect to an already disconnected output");
            ioInput.disconnect();
            ioOutput.disconnect();
            m_connected = false;
        }

        /*!
         * @brief return if the adapter is connected
         * @return is connected
         */
        [[nodiscard]] inline bool isConnected() const { return m_connected; }
    private:
        bool m_connected = false;
        framework::internal::EntityIOAdapterElement_t<T> m_ioAdapter;
    };

    namespace mountpoint {
        /*!
         * @brief is implemented if the mountpoint has isElementPresent
         */
        class hasElement_t {
        public:
            /*!
             * @brief ĉhecks if an element is present at current mountpoint
             * @return is element present
             */
            [[nodiscard]] virtual bool isElementPresent() const = 0;

            virtual ~hasElement_t() = default;
        };

        /*!
         * @brief is implemented if a mounting point provides a interface for pushing an element
         * @tparam T
         */
        template<typename T>
        class canPushElement_t : public hasElement_t{
        public:
            /*!
             * @brief pushes element on the current mountpoint
             * @throws std::runtime_error if an object is present
             * @param element element to push
             */
            virtual void push(T&& element) = 0;

            ~canPushElement_t() override = default;
        };

        /*!
         * @brief is implemented if a mounting point provides a interface for poping an element
         * @tparam T
         */
        template<typename T>
        class canPopElement_t : public hasElement_t {
        public:
            /*!
             * @brief pops current element and returns it.
             * @throws std::runtime_error if no object is present
             * @return removed element
             */
            virtual T pop() = 0;

            ~canPopElement_t() override = default;
        };

        /*!
         * @brief is implemented if a mounting point provides a interface for peeking an element
         * @tparam T
         */
        template<typename T>
        class canPeekElement_t : public hasElement_t{
        public:
            /*!
             * @brief peek at element on the current mountpoint. Do not store reference may be invalid on next cycle.
             * @throws std::runtime_error if no object is present
             * @return reference to element
             */
            virtual const T& peek() = 0;

            ~canPeekElement_t() override = default;
        };

        /*!
         * @bried genric mountpoint
         * @tparam T element type this interface provides interaction with
         */
        template<typename T>
        class mountpoint_t {
        public:
            /*!
             * @brief base implementation of a mountpoint
             * @tparam Interface interface type to cast element to
             * @return interface
             */
            template<class Interface>
            Interface* getInterface() { return dynamic_cast<Interface*>(this); }

            virtual ~mountpoint_t() = default;
        };
    }

    namespace utils
    {
        template<typename Element, typename Common, std::size_t... I>
        std::array<Element, sizeof...(I)> makeElementsInArray(
            Common& common,
            std::index_sequence<I...>)
        {
            return { Element(common, I)... };
        }
    }
}

#endif //SIMULATEDFACTORY_RUNTIMEENTITY_H
