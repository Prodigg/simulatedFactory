#include <iostream>
#include "Framework.h"

class testEntity_t : public runtimeEntity_t {
    public:
    testEntity_t() : runtimeEntity_t("testEntity") {};
    void cycle() override {
        std::cout << "testEntity::test: " << getIOProvider().readInput<bool>(testInput) << "\n";
    }
    void init() override {
        std::cout << "Initialized with ID: " << getEntityID() << "\n";
        std::string inputName = "test";
        testInput = this->getIOProvider().registerInput(inputName, IOType_t::BOOL);
    }
    private:
    IoID_t testInput = 0;
};

int main() {
    // ensure both are ready
    Runtime_t::GetInstance();
    IOHandler_t::GetInstance();

    testEntity_t testEntity;
    Runtime_t::GetInstance().initializeRuntime("/tmp/testDir/Config.json", false);

    /*
    std::string ipV4 = "plc-raphael.localdomain";
    Runtime_t::GetInstance().initializeRuntime(ipV4, {5,109,7,180,1,1}, {192,168,1,126,1,1}, 851);
    */
    Runtime_t::GetInstance().runtimeStart();

    std::string data;
    while (true) {
        std::cin >> data;
        if (data == "exit") {
            return 0;
        }
    }
}
