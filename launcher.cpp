#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <cstdlib>

std::map<std::string, uint32_t> colorsByName = {
    {"aquamarine", 0x7fffd4},
    {"black", 0x000000},
    {"coral", 0xFF7F50},
    {"deeppink", 0xFF1493},
    {"gray", 0x808080},
    {"hotpink", 0xFF69B4},
    {"lavender", 0xE6E6FA},
    {"lightcyan", 0xE0FFFF},
    {"lightgray", 0xD3D3D3},
    {"navy", 0x000080},
    {"powderblue", 0xB0E0E6},
    {"red", 0xFF0000},
    {"white", 0xFFFFFF},
};

uint32_t expand12BitColorTo24(uint32_t color)
{
    uint8_t r = (color & 0xF00) >> 8;
    r = (r << 4) | r;
    uint8_t g = (color & 0x0F0) >> 4;
    g = (g << 4) | g;
    uint8_t b = (color & 0x00F) >> 0;
    b = (b << 4) | b;
    return (r << 16) | (g << 8) | (b << 0);
}

std::string convertToHexColor(const std::string& name)
{
    uint32_t color;

    if(name[0] == '#') {
        color = strtoul(name.c_str() + 1, nullptr, 16);
        if(name.length() < 4) { // Just three hex digits
            color = expand12BitColorTo24(color);
        }
    } else {
        color = strtoul(name.c_str(), nullptr, 16);
        if(errno == EINVAL) {
            color = colorsByName.at(name);
        } else {
            if(name.length() < 3) { // Just three hex digits
                color = expand12BitColorTo24(color);
            }
        }
    }

    std::stringstream ss;
    ss << std::setfill('0') << std::setw(6) << std::hex << color;
    return ss.str();
}

bool hasTrueOption(const nlohmann::json& options, const std::string& name)
{
    if(options.contains(name)) {
        if(options[name].type() == nlohmann::json::value_t::boolean) {
            return options[name].get<bool>();
        } else {
            return options[name].get<int>();
        }
    } else {
        return false;
    }
}

int main(int argc, char **argv)
{
    if(argc < 2) {
        std::cerr << "usage: " << argv[0] << " programs.json [romsdir programToRun]\n";
        exit(EXIT_FAILURE);
    }

    std::ifstream programsFile(argv[1]);
    nlohmann::json programs;
    programsFile >> programs;

    if(argc < 4) {
        size_t maxlength = 0;
        for (const auto& [program, specifics] : programs.items()) {
            maxlength = std::max(program.length(), maxlength);
        }
        for (const auto& [program, specifics] : programs.items()) {
            std::cout << std::setw(maxlength) << program << std::setw(0) << " : " << specifics["title"] << "\n";
            std::cout << std::setw(maxlength) << "" << std::setw(0) << "   " << specifics["desc"] << "\n";
        }
        exit(EXIT_SUCCESS);
    }

    std::string romsDir = argv[2];
    std::string chosenProgram = argv[3];

    if(!programs.contains(chosenProgram)) {
        std::cerr << "unknown program \"" << chosenProgram << "\"\n";
        exit(EXIT_FAILURE);
    }

    const auto& program = programs[chosenProgram];

    std::vector<std::string> emulatorArgs;

    emulatorArgs.push_back("xochip");

    if(program["platform"] == "schip") {
        emulatorArgs.push_back("--platform schip");
    } else if(program["platform"] == "xochip") {
        emulatorArgs.push_back("--platform xochip");
    }

    const auto& options = program["options"];
    if(options.contains("tickrate")) {
        if(options["tickrate"].type() == nlohmann::json::value_t::string) {
            emulatorArgs.push_back("--rate " + options["tickrate"].get<std::string>());
        } else {
            emulatorArgs.push_back("--rate " + std::to_string(options["tickrate"].get<int>()));
        }
    }

    if(options.contains("backgroundColor")) {
        emulatorArgs.push_back("--color 0 " + convertToHexColor(options["backgroundColor"]));
    }
    if(options.contains("fillColor")) {
        emulatorArgs.push_back("--color 1 " + convertToHexColor(options["fillColor"]));
    }
    if(options.contains("fillColor2")) {
        emulatorArgs.push_back("--color 2 " + convertToHexColor(options["fillColor2"]));
    }
    if(options.contains("blendColor")) {
        emulatorArgs.push_back("--color 3 " + convertToHexColor(options["blendColor"]));
    }
    if(options.contains("screenRotation")) {
        emulatorArgs.push_back("--rotation " + std::to_string(options["screenRotation"].get<int>()));
    }

    if(hasTrueOption(options, "shiftQuirks")) {
        emulatorArgs.push_back("--quirk shift");
    }

    if(hasTrueOption(options, "loadStoreQuirks")) {
        emulatorArgs.push_back("--quirk loadstore");
    }

    if(hasTrueOption(options, "logicQuirks")) {
        emulatorArgs.push_back("--quirk logic");
    }

    if(hasTrueOption(options, "vfOrderQuirks")) {
        emulatorArgs.push_back("--quirk vforder");
    }

    if(hasTrueOption(options, "clipQuirks")) {
        emulatorArgs.push_back("--quirk clip");
    }

    if(hasTrueOption(options, "jumpQuirks")) {
        emulatorArgs.push_back("--quirk jump");
    }

    // "vfOrderQuirks": false,
    // "vBlankQuirks": false,

    emulatorArgs.push_back(romsDir + "/" + chosenProgram + ".ch8");

    bool first = true;
    for(const auto& arg: emulatorArgs) {
        if(!first) {
            std::cout << " ";
        }
        std::cout << arg;
        first = false;
    }
    std::cout << "\n";

    exit(EXIT_SUCCESS);
}
