#include <iostream>
#include "Framework.h"

class testEntity_t : public sim::framework::runtimeEntity_t {
    public:
    testEntity_t() : runtimeEntity_t("testEntity") {};
    void cycle() override {
    }
    void init() override {
        std::cout << "Initialized with ID: " << getEntityID() << "\n";
        const std::string inputName = "test";
        testInput = this->getIOProvider().registerInput(inputName, sim::framework::IOType_t::BOOL);
        this->getTerminalProvider().registerCMD("currentTestInput", [&](std::string_view input) {
            std::cout << "testEntity::test: " << getIOProvider().readInput<bool>(testInput) << "\n";
        });
    }
    private:
    sim::framework::IoID_t testInput = 0;
};

int main(int argc, char** argv) {
    // ensure both are ready
    sim::framework::Runtime_t::GetInstance();
    sim::framework::IOHandler_t::GetInstance();

    testEntity_t testEntity;

    //TODO: make these two args controllable via cli
    sim::framework::Runtime_t::GetInstance().initializeRuntime("/home/prodigg/CLionProjects/simulatedFactory/Config.json", false);

    sim::framework::Runtime_t::GetInstance().runtimeStart();

    std::string data;
    while (true) {
        std::cin >> data;
        if (data == "exit") {
            return 0;
        }
    }
}
