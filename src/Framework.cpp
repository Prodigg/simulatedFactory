//
// Created by prodigg on 02.06.26.
//
#include "Framework.h"
#include <AdsLib/AdsLib.h>

runtimeEntity_t::runtimeEntity_t(const std::string_view entityName) :
m_entityID(getRuntime().generateEntityID(*this, entityName)),
m_ioProvider(Runtime_t::GetInstance().getIOProvider(m_entityID)) {
}

IOHandler_t & runtimeEntity_t::getIOHandler() {
    return IOHandler_t::GetInstance();
}

Runtime_t & runtimeEntity_t::getRuntime() {
    return Runtime_t::GetInstance();
}

IOProvider_t & runtimeEntity_t::getIOProvider() const {
    return m_ioProvider;
}

std::string IOHandler_t::getFullyQualifiedName(EntityID_t entityId, IoID_t inputIoID) {
    std::string_view ioName;
    if (const auto it = std::ranges::find_if(m_ioMap,[entityId](const IOEntityRegistry_t& entry) { return entityId == entry.entityID;}); it != m_ioMap.end())
        if (const auto it2 = std::ranges::find_if(it->ioMap, [inputIoID](const IOMapEntry_t& entry) { return inputIoID == entry.ioID;}); it2 != it->ioMap.end())
            ioName = it2->ioName;

    if (ioName.empty())
        throw std::runtime_error("unable to find IO name for entity id ");

    std::string fullyQualifiedName;
    fullyQualifiedName += Runtime_t::GetInstance().getEntityName(entityId);
    fullyQualifiedName += "::";
    fullyQualifiedName += ioName;
    return fullyQualifiedName;
}

IoID_t IOProvider_t::registerInput(const std::string& inputName, const IOType_t type) const {
    return IOHandler_t::GetInstance().registerInput(m_entityID, inputName, type);
}

IoID_t IOProvider_t::registerOutput(const std::string &outputName, const IOType_t type) const {
    return IOHandler_t::GetInstance().registerOutput(m_entityID, outputName, type);
}

IOProvider_t::IOProvider_t(const EntityID_t entityID)
    : m_entityID(entityID) {
}

inline IOHandler_t& IOHandler_t::GetInstance()  {
    static IOHandler_t inst; // created on first call
    return inst;
}

IOHandler_t::~IOHandler_t() {
    if (m_adsWrite)
        m_adsWrite.reset();
    if (m_adsRead)
        m_adsRead.reset();
    if (m_route)
        m_route.reset();
}

IoID_t IOHandler_t::registerInput(const EntityID_t entityId, const std::string& inputName, IOType_t type) {
    if (m_adsRead)
        throw std::runtime_error("IOHandler already initialized, unable to register new inputs");

    // check if we find a existing entity entry
    for (auto &[entityID, ioMap] : m_ioMap) {
        if (entityID == entityId) {
            IoID_t ioId = (ioMap.back().ioID + 1) | 1 << 31;
            ioMap.emplace_back(ioId, inputName, type);
            return ioId;
        }
    }

    std::vector<IOMapEntry_t> ioMap = {{static_cast<IoID_t>(1) | (1 << 31), inputName, type}};
    m_ioMap.emplace_back(entityId, ioMap);
    return static_cast<IoID_t>(1) | (1 << 31);
}

IoID_t IOHandler_t::registerOutput(EntityID_t entityId, std::string outputName, IOType_t type) {
    if (m_adsWrite)
        throw std::runtime_error("IOHandler already initialized, unable to register new inputs");

    // check if we find a existing entity entry
    for (auto &[entityID, ioMap] : m_ioMap) {
        if (entityID == entityId) {
            IoID_t outputBitMask = 0xFFFFFFFF;
            outputBitMask = outputBitMask >> 1;
            IoID_t ioId = (ioMap.back().ioID + 1) & outputBitMask;
            ioMap.emplace_back(ioId, outputName, type);
            return ioId;
        }
    }

    std::vector<IOMapEntry_t> ioMap = {{1, outputName, type}};
    m_ioMap.emplace_back(entityId, ioMap);
    return 1;
}

void IOHandler_t::initialize(std::string& ipV4, AmsNetId remoteNetID, AmsNetId localNetID, uint16_t port) {

    // initialize ads route
    bhf::ads::SetLocalAddress(localNetID);
    m_route.emplace(ipV4, remoteNetID, port);

    std::vector<std::string> readList;
    std::vector<std::string> writeList;

    // find write / read list
    for (const auto &[entityID, ioMap]: m_ioMap) {
        std::string entryName = Runtime_t::GetInstance().getEntityName(entityID);
        for (const IOMapEntry_t & entry: ioMap) {
            std::string fullyQualifiedName = entryName + "::" + entry.ioName;
            if (entry.ioID >> 31 == 1)
                readList.push_back(m_ioToAdsMap.at(fullyQualifiedName));
            else
                writeList.push_back(m_ioToAdsMap.at(fullyQualifiedName));
        }
    }

    m_adsWrite.emplace(*m_route, writeList);
    m_adsRead.emplace(*m_route, readList);
}

void IOHandler_t::readWriteData() {
    if (!m_adsWrite || !m_adsRead)
        throw std::runtime_error("readWriteData called before initialising ads");
    m_adsWrite->write();
    m_adsRead->read();
}

Runtime_t& Runtime_t::GetInstance() {
    static Runtime_t inst; // created on first call
    return inst;
}

void Runtime_t::initializeRuntime(std::string& ipV4, const AmsNetId remoteNetID, const AmsNetId localNetID, const uint16_t port) {
    for (const runtimeEntityEntry & entry: m_entries) {
        entry.entity.init();
    }
    //IOHandler_t::GetInstance().readConfig("/path");
    IOHandler_t::GetInstance().m_ioToAdsMap.emplace("testEntity::test", "NodeRed.bIsLightOn");
    IOHandler_t::GetInstance().initialize(ipV4, remoteNetID, localNetID, port);
}

void Runtime_t::runtimeStart() const {
    while (true) {
        IOHandler_t::GetInstance().readWriteData();
        for (const runtimeEntityEntry & entry: m_entries) {
            entry.entity.cycle();
        }
    }
}

EntityID_t Runtime_t::generateEntityID(runtimeEntity_t &instance, const std::string_view instanceName) {
    m_entries.emplace_back(m_entityIdCounter, std::string(instanceName), instance, IOProvider_t(m_entityIdCounter));
    return m_entityIdCounter++;
}

IOProvider_t& Runtime_t::getIOProvider(const EntityID_t entityID) {
    for (runtimeEntityEntry & entry : m_entries) {
        if (entry.entityID == entityID)
            return entry.ioProvider;
    }
    throw std::out_of_range("Entity ID not found");
}

std::string Runtime_t::getEntityName(EntityID_t entityID) {
    if (auto it = std::ranges::find_if(m_entries, [entityID](const auto& entry) { return entry.entityID == entityID; }); it != m_entries.end())
        return it->entityName;
    throw std::runtime_error("Entity ID not found");
}
