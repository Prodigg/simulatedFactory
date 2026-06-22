//
// Created by prodigg on 02.06.26.
//

#ifndef SIMULATEDFACTORY_ADSVARIABLELIST_H
#define SIMULATEDFACTORY_ADSVARIABLELIST_H

#include <AdsLib/AdsDevice.h>
#include "vector"

#include <array>
#include <cstring>

struct AdsVariableList {
    AdsVariableList(const AdsDevice& route, std::vector<std::string> symbolNames)
        :m_route{route},
        m_symboleNames {symbolNames},
        m_symboleEntrys {getSymbolEntrys(m_symboleNames)}
    {
        int dataSize = 0;
        for (const AdsSymbolEntry &en : m_symboleEntrys) {
            dataSize += en.size;
            AdsSymbolInfoByName info {en.iGroup,en.iOffs,en.size};
            m_symboleInfos.push_back(info);
        }
        m_rBuf.resize(4 * m_symboleNames.size() + dataSize,0);
        m_wBuf.resize(12 * symbolNames.size() + dataSize,0);
        initWBuf();
    }

    void read() {
        uint32_t bytesRead = 0;
        size_t symboleSize = m_symboleNames.size();
        if (symboleSize == 0)
            return; // nothing to read

        uint32_t error = m_route.ReadWriteReqEx2(
            ADSIGRP_SUMUP_READ,
            symboleSize,
            m_rBuf.size(),
            const_cast<uint8_t*>(m_rBuf.data()),
            12 * symboleSize,
            m_symboleInfos.data(),
            &bytesRead);
        if (error || (m_rBuf.size() != bytesRead)) {
            throw AdsException(error);
        }
    }

    void write() {
        uint32_t bytesRead = 0;
        size_t symboleSize = m_symboleNames.size();
        if (symboleSize == 0)
            return; // nothing to do

        copyData2WriteBuf();

        size_t readSize = 0;
        auto rbf = getStateBuf(&readSize);

        uint32_t error = m_route.ReadWriteReqEx2(
            ADSIGRP_SUMUP_WRITE,
            symboleSize,
            readSize,
            rbf,
            m_wBuf.size(),
            m_wBuf.data(),
            &bytesRead);
        if (error || (readSize != bytesRead)) {
            throw AdsException(error);
        }
    }

    size_t getSymboleSize(const std::string& name) const {
        int index = 0;
        for (const std::string &n : m_symboleNames) {
            if (name == n) {
                return m_symboleEntrys.at(index).size;
            }
            ++index;
        }
    }

    void * getSymboleData(const std::string& name, size_t* length) {

        auto pdbf = getDataBuf(nullptr);

        for (int i=0; i<m_symboleEntrys.size();++i) {
            if (name == m_symboleNames.at(i)) {
                if (length) {
                    *length = m_symboleEntrys.at(i).size;
                }
                return pdbf;
            }
            pdbf += m_symboleEntrys.at(i).size;
        }
        throw std::out_of_range("symbol name unknown");
    }

    void setSymbolData(const std::string& name, void *data, size_t length, size_t begin = 0) {
        size_t size = 0;
        auto buf = static_cast<uint8_t*>(getSymboleData(name,&size));
        if ((begin + length) > size)
            return;
        std::memcpy((buf + begin),data,length);
    }

    int32_t getStateCode(const std::string& name) const {

        auto psbf = getStateBuf(nullptr);
        for(int i=0;i<m_symboleNames.size();++i) {
            if (name == m_symboleNames.at(i)) {
                return *reinterpret_cast<int32_t*>(psbf + 4 * i);
            }
        }
    }

private:
    uint8_t * getStateBuf(size_t* size) const{
        size_t len = 4 * m_symboleNames.size();

        if (size) {
            *size = len;
        }
        return const_cast<uint8_t*>(m_rBuf.data());
    }

    uint8_t * getDataBuf(size_t* size) const{
        size_t stlen = 4 * m_symboleNames.size();
        size_t len = m_rBuf.size() - stlen;

        if (size) {
            *size = len;
        }
        return const_cast<uint8_t*>(m_rBuf.data()) + stlen;
    }

    uint8_t * getWriteDataBuf(size_t* size) const{
        size_t inflen = 12 * m_symboleNames.size();
        size_t len = m_rBuf.size() - inflen;

        if (size) {
            *size = len;
        }
        return const_cast<uint8_t*>(m_wBuf.data()) + inflen;
    }

    void copyData2WriteBuf() {
        size_t rdataSize = 0;
        auto rbf = getDataBuf(&rdataSize);
        auto wbf = getWriteDataBuf(nullptr);
        std::memcpy(wbf,rbf,rdataSize);
    }

    void initWBuf() {
        auto pInt32buf = reinterpret_cast<int32_t*>(const_cast<uint8_t*>(m_wBuf.data()));
        auto symboleSize = m_symboleNames.size();
        for (int i = 0; i < symboleSize; ++i) {
            *pInt32buf = m_symboleEntrys.at(i).iGroup;
            ++pInt32buf;
            *pInt32buf = m_symboleEntrys.at(i).iOffs;
            ++pInt32buf;
            *pInt32buf = m_symboleEntrys.at(i).size;
            ++pInt32buf;
        }
    }

    AdsSymbolEntry getSymbolEntry(const std::string &symbolName) const
    {
        AdsSymbolEntry entry;
        uint32_t bytesRead = 0;
        uint32_t error = m_route.ReadWriteReqEx2(
            ADSIGRP_SYM_INFOBYNAMEEX,
            0x0,
            sizeof(entry),
            &entry,
            sizeof(symbolName),
            symbolName.c_str(),
            &bytesRead);
        if (error) {
            throw AdsException(error);
        }
        return entry;
    }

    std::vector<AdsSymbolEntry> getSymbolEntrys(const std::vector<std::string>& symbolNames) const
    {
        std::vector<AdsSymbolEntry> re;
        for (const std::string &name : symbolNames){
            re.push_back(getSymbolEntry(name));
        }
        return re;
    }

    const AdsDevice &m_route;
    const std::vector<std::string> m_symboleNames;
    const std::vector<AdsSymbolEntry> m_symboleEntrys;
    std::vector<AdsSymbolInfoByName> m_symboleInfos;
    std::vector<uint8_t> m_rBuf;
    std::vector<uint8_t> m_wBuf;
};
#endif //SIMULATEDFACTORY_ADSVARIABLELIST_H
