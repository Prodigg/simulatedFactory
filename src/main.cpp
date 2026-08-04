#include <iostream>
#include "Framework.h"
#include "stdMachines.h"

class testEntity_t : public sim::runtimeEntity_t {
    public:
    testEntity_t() : runtimeEntity_t("testEntity") {};
    void cycle() override {
    }
    void init() override {
        getLogger().logInfo(std::format("Initialized with ID: {}", getEntityID()));
        std::string inputName = "test";
        testInput = this->getIOProvider().registerInput(inputName, sim::framework::IOType_t::BOOL);
        this->getTerminalProvider().registerCMD("currentTestInput", [&](std::string_view arg) {
            auto testInputData = getIOProvider().readInput<bool>(testInput);
            getLogger().logInfo(
                std::format("testEntity::test: {}", testInputData));
        this->getVisualizationProvider().emmitVariableChange("test", {"inputState", testInputData});
        });
    }
    private:
    sim::framework::IoID_t testInput = 0;
};

struct testObject_t {
    int number = 0;
};

int main(int argc, char** argv) {

    sim::framework::Runtime_t::GetInstance();

    testEntity_t testEntity;

    testObject_t testObject;
    sim::conveyorBelt_t<testObject_t, 3> conveyorBelt("conveyor");
    conveyorBelt.setMoveInterval(std::chrono::seconds(1));

    sim::objectGenerator_t<testObject_t> generator("generator");
    generator.setSpawnElement(testObject);
    generator.setSpawnInterval(std::chrono::seconds(5)).spawnContinuous(true).spawn();

    sim::EntityIOAdapter_t<testObject_t> conveyorBeltInAdapter(generator.getOutput(), conveyorBelt.getInput());

    sim::objectVoider_t<testObject_t> objectVoider("voider");
    sim::EntityIOAdapter_t<testObject_t> conveyorBeltOutAdapter(conveyorBelt.getOutput(), objectVoider.getInput());

    sim::digitalSensor_t<testObject_t> digitalSensor("digitalSensor");

    digitalSensor.setOnSensorTrueCallback([](const testObject_t& obj){ std::cout << "TestEntityNum: " << obj.number << std::endl; });
    digitalSensor.setSensorCheckFunction([](const testObject_t& obj) {return true;});
    digitalSensor.linkSensorInterface(conveyorBelt.getMountpointInterface(2));


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
