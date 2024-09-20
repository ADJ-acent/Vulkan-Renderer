#include "scene.hpp"
#include "sejp.hpp"
#include <fstream>
#include <iostream>

void Scene::load(std::string filename)
{
    if (filename.substr(filename.size()-5, 4) != ".s72") {
        throw std::runtime_error("Scene " + filename + " is not a compatible format (s72 required).");
    }
    sejp::value val = sejp::load(filename);
    try {
        std::vector<sejp::value > const &object = val.as_array().value();
        if (object[0].as_string() != "s72-v2") {
            std::cout << "cannot find the correct header" <<std::endl;
            throw std::runtime_error(" ");
        }
        for (uint32_t i = 0; i < uint32_t(object.size()); ++i) {
            
        }


    } catch (std::exception &e) {
        throw std::runtime_error("File not in .s72 required format.");
    }

}