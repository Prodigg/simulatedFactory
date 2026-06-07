//
// Created by prodigg on 02.06.26.
//

#ifndef SIMULATEDFACTORY_FRAMEWORK_H
#define SIMULATEDFACTORY_FRAMEWORK_H

#include <stack>

#include "AdsVariableList.h"
#include <algorithm>
#include <cstring>
#include <ranges>

class Runtime_t;
class IOHandler_t;
class IOProvider_t;

using EntityID_t = uint32_t;

// the last bit (31) is 0 if output () and 1 if input
// test |= 1 << 31;
// test >> 31 == 1;
using IoID_t = uint32_t;

class runtimeEntity_t {
public:
    virtual ~runtimeEntity_t() = default;
    explicit runtimeEntity_t(std::string_view entityName);

    /*!
     * @brief is called every cycle by the runtime
     */
    void virtual cycle() = 0;

    /*!
     * @brief is called at the start of the simulation to allow initialization by the runtime
     */
    void virtual init() = 0;

protected:
    [[nodiscard]] EntityID_t getEntityID() const { return m_entityID; };
    [[nodiscard]] static IOHandler_t& getIOHandler();
    [[nodiscard]] static Runtime_t& getRuntime();
    [[nodiscard]] IOProvider_t& getIOProvider() const;
private:
    EntityID_t m_entityID;
    IOProvider_t& m_ioProvider;
};

enum class IOType_t {
    INVALID = 0,
    BOOL,
    INT8,
    INT16,
    INT32,
    INT64,
    UINT8,
    UINT16,
    UINT32,
    UINT64,
    FLOAT32,
    FLOAT64
};

template<typename T>
concept IOTypeRestriction =
    std::is_same_v<T, bool>   ||
    std::is_same_v<T, int8_t>  ||
    std::is_same_v<T, int16_t> ||
    std::is_same_v<T, int32_t> ||
    std::is_same_v<T, int64_t> ||
    std::is_same_v<T, uint8_t>  ||
    std::is_same_v<T, uint16_t> ||
    std::is_same_v<T, uint32_t> ||
    std::is_same_v<T, uint64_t> ||
    std::is_same_v<T, float>    ||
    std::is_same_v<T, double>;

struct IOMapEntry_t {
    IoID_t ioID;
    std::string ioName;
    IOType_t type;
};

struct IOEntityRegistry_t {
    EntityID_t entityID;
    std::vector<IOMapEntry_t> ioMap;
};

struct writeRequestEntry_t {
    std::string AdsVariableName;
    union {
        bool _bool;
        int8_t _int8;
        int16_t _int16;
        int32_t _int32;
        int64_t _int64;
        uint8_t _uint8;
        uint16_t _uint16;
        uint32_t _uint32;
        uint64_t _uint64;
        float _float32;
        double _float64;
    } value;
};

/*
 * Startup flow:
 *
 * 1. create runtime and IOHandler
 * 2. create all entities
 * 3. entities register io vars at IOHandler over IOProvider
 * 4. IOHandler receives config file with mapping
 * 5. Runtime initializes IOHandler, IOHandler initializes ADS
 * 6. Runtime starts normal execution flow
 */

struct ConfigNetId_t {
    uint8_t _1 = 0;
    uint8_t _2 = 0;
    uint8_t _3 = 0;
    uint8_t _4 = 0;
    uint8_t _5 = 0;
    uint8_t _6 = 0;

    explicit ConfigNetId_t(std::string str) {
        _1 = toNum(str);
        _2 = toNum(str);
        _3 = toNum(str);
        _4 = toNum(str);
        _5 = toNum(str);
        _6 = toNum(str);
    }

    ConfigNetId_t() = default;

    operator AmsNetId () const {return {_1, _2, _3, _4, _5, _6};}
private:
    static uint8_t toNum (std::string& str) {
        size_t const delimiter =  str.find_first_of('.', 0);
        if (delimiter == std::string::npos) {
            return std::stoi(str);
        }
        const uint8_t result = std::stoi(str.substr(0, delimiter));
        str.erase(0, delimiter + 1);
        return result;
    }
};

struct ioToAdsMapElement_t {
    std::string adsName;
    IOType_t type = IOType_t::INVALID;
};

class config_t {
public:
    static void generateConfig(std::string_view path, const std::vector<IOEntityRegistry_t>& ioMap);
    void applyConfig(std::string_view path, std::unordered_map<std::string, ioToAdsMapElement_t>& ioMap);

    [[nodiscard]] inline AmsNetId getLocalNetId() const {return m_localNetId;};
    [[nodiscard]] inline AmsNetId getRemoteNetId() const {return m_remoteNetId;};
    [[nodiscard]] inline std::string remoteIpV4() const {return m_remoteIpV4;};
    [[nodiscard]] inline uint16_t remotePort() const {return m_remotePort;};
private:
    static std::string ioTypeToString(IOType_t type);
    static IOType_t stringToIoType(std::string_view type);

    ConfigNetId_t m_localNetId;
    ConfigNetId_t m_remoteNetId;
    std::string m_remoteIpV4;
    uint16_t m_remotePort = 0;
};

/*!
 * @brief allows machines to register needed variables for mapping. Is the mapping to ADS.
 * @details this class includes a buffer that stores write request for next write all values that are registered as
 *          read are read when the readWriteData() is called by the runtime
 */
class IOHandler_t {
public:
    IOHandler_t(IOHandler_t &other) = delete;
    IOHandler_t &operator=(IOHandler_t &other) = delete;
    static IOHandler_t& GetInstance();

    friend class IOProvider_t;
    friend class Runtime_t;
    ~IOHandler_t();
protected:
    // for ioProvider

    IoID_t registerInput(EntityID_t entityId, const std::string& inputName, IOType_t type);
    IoID_t registerOutput(EntityID_t entityId, std::string outputName, IOType_t type);

    template<IOTypeRestriction T>
    T readInput(const EntityID_t entityId, const IoID_t inputIoID) {
        if (!m_adsRead)
            throw std::runtime_error("readInput called before IOHandler initialized");
        if (inputIoID >> 31 == 0)
            throw std::runtime_error("unable to read input with IoID of output");


        size_t length = sizeof(T);
        const void* ptr = m_adsRead->getSymboleData(
            m_ioToAdsMap.at(getFullyQualifiedName(entityId, inputIoID)).adsName, &length);

        T returnValue;
        std::memcpy(&returnValue, ptr, sizeof(T));
        return returnValue;
    }

    template<IOTypeRestriction T>
    bool writeOutput(const EntityID_t entityId, const IoID_t outputIoID, T value) {
        if (!m_adsWrite)
            throw std::runtime_error("writeOutput called before IOHandler initialized");

        if (outputIoID >> 31 == 1)
            throw std::runtime_error("unable to write output with IoID of input");

        m_adsWrite->setSymbolData(getFullyQualifiedName(entityId, outputIoID), &value, sizeof(T));
        return true;
    }


    // for runtime
    void initialize(std::string_view ipV4, AmsNetId remoteNetID, AmsNetId localNetID, uint16_t port);
    void readWriteData();
    void generateConfig(const std::string_view path) const {config_t::generateConfig(path, m_ioMap);};

    /*!
     * @brief read config and initialize IOHandler_t
     * @param path path to config
     */
    void readConfig(std::string_view path);

    IOHandler_t() = default;
private:
    std::vector<IOEntityRegistry_t> m_ioMap;

    //! needs to be optional to wait until it is configured
    std::optional<AdsVariableList> m_adsRead;
    std::optional<AdsVariableList> m_adsWrite;
    std::optional<AdsDevice> m_route;

    std::string getFullyQualifiedName(EntityID_t entityId, IoID_t inputIoID);


    std::unordered_map<std::string, ioToAdsMapElement_t> m_ioToAdsMap;
};

/*!
 * @brief this allows entities to interact with the io system
 * @details this class restricts the usage and only allows machine to interact with previously registered elements
 */
class IOProvider_t {
public:
    [[nodiscard]] IoID_t registerInput(const std::string& inputName, IOType_t type) const;
    [[nodiscard]] IoID_t registerOutput(const std::string &outputName, IOType_t type) const;

    template<IOTypeRestriction T>
    [[nodiscard]] inline T readInput(const IoID_t inputIoID) const{
        return IOHandler_t::GetInstance().readInput<T>(m_entityID, inputIoID);
    }

    template<IOTypeRestriction T>
    [[nodiscard]] bool writeOutput(const IoID_t outputIoID, T value) {
        return IOHandler_t::GetInstance().writeOutput<T>(m_entityID, outputIoID, value);
    }

    friend class Runtime_t;
private:
    explicit IOProvider_t(EntityID_t entityID);
    EntityID_t m_entityID;
};


struct runtimeEntityEntry {
    EntityID_t entityID;
    std::string entityName;
    runtimeEntity_t& entity;
    IOProvider_t ioProvider;
};

/*!
 * @brief handles the execution of Machines and IO Systems
 */
class Runtime_t {
public:
    Runtime_t(Runtime_t &other) = delete;
    Runtime_t &operator=(Runtime_t &other) = delete;
    static Runtime_t& GetInstance();

    /*!
     *
     */
    static void generateConfig(const std::string_view path) {IOHandler_t::GetInstance().generateConfig(path);}

    /*!
     * @brief configures IOHandler and makes the runtime ready
     */
    void initializeRuntime(std::string_view configPath, bool generateConfig) const;

    /*!
     * @brief start the runtime, requires to first initialize the runtime and creating all the runtime entities
     */
    void runtimeStart() const;

    EntityID_t generateEntityID(runtimeEntity_t& instance, std::string_view instanceName = "");
    void destroyEventID(EntityID_t entityID);
    [[nodiscard]] inline IOProvider_t& getIOProvider(EntityID_t entityID);
    std::string getEntityName(EntityID_t entityID);
protected:
    Runtime_t() = default;

private:
    std::vector<runtimeEntityEntry> m_entries;
    EntityID_t m_entityIdCounter = 1; //! id counter for generating EntityIDs
};





#endif //SIMULATEDFACTORY_FRAMEWORK_H
