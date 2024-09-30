#include "HeadlessEvent.hpp"

#include <cassert>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iostream>

HeadlessEvent::HeadlessEvent(EventType type_, uint32_t ts_)
    : type(type_), ts(ts_), event_params(std::monostate{}) 
{
    assert(type == EventType::AVAILABLE);
}

// Constructor for events with string parameters (e.g., SAVE or MARK)
HeadlessEvent::HeadlessEvent(EventType type_, const std::string& params, uint32_t ts_)
    : type(type_), ts(ts_), event_params(params) 
{
    assert(type == EventType::MARK || type == EventType::SAVE);
}

// Constructor for events with animation parameters (e.g., PLAY)
HeadlessEvent::HeadlessEvent(EventType type_, float t, float rate, uint32_t ts_)
    : type(type_), ts(ts_), event_params(AnimationParams{t, rate}) 
{
    assert(type == EventType::PLAY);
}

std::vector<HeadlessEvent> HeadlessEvent::load_events(std::string filename)
{
    std::vector<HeadlessEvent> events;
    std::ifstream event_file(filename);
    if (event_file.is_open()) {
        std::string line;
        while (std::getline(event_file, line)) {
            std::istringstream string_stream(line);

            std::string ts_str;
            std::string type;

            string_stream >> ts_str >> type;
            uint32_t ts = std::atoi(ts_str.c_str());
            if (type == "AVAILABLE") {
                events.emplace_back(EventType::AVAILABLE, ts);
            }
            else if (type == "SAVE") {
                std::string save_filename;
                std::getline(string_stream, save_filename);
                // get rid of white space
                events.emplace_back(EventType::SAVE, save_filename.substr(1, save_filename.size()-1), ts);
            }
            else if (type == "MARK") {
                std::string mark_string;
                std::getline(string_stream, mark_string);
                events.emplace_back(EventType::MARK, mark_string, ts);
            }
            else if (type == "PLAY") {
                std::string t_str, rate_str;
                string_stream >> t_str >> rate_str;
                float t = std::stof(t_str.c_str());
                float rate = std::stof(rate_str.c_str());
                events.emplace_back(EventType::PLAY, t, rate, ts);
            }
            else {
                std::cerr<<"Unknown type: " + type + ". Ignoring...\n";
            }
            std::string rest_of_string;
            std::getline(string_stream, rest_of_string);
            if (rest_of_string != "") {
                std::cerr<<"Extra parameters from the HeadlessEvent file: " + rest_of_string + ". Ignoring...\n";
            }
        }
        event_file.close();
    }
    for (auto& event : events) {
        event.print();
    }
    return events;
}

void HeadlessEvent::print()
{
    if (type == EventType::AVAILABLE) {
        std::cout << ts << " AVAILABLE\n";
    }
    else if (type == EventType::MARK) {
        std::cout << ts << " MARK," << std::get<std::string>(event_params)<< "\n";
    }
    else if (type == EventType::SAVE) {
        std::cout << ts << " SAVE, " << std::get<std::string>(event_params)<< "\n";
    }
    else if (type == EventType::PLAY) {
        std::cout << ts << " PLAY, playback time:" << std::get<AnimationParams>(event_params).animation_playback_time <<", rate: "<< std::get<AnimationParams>(event_params).animation_rate << "\n";
    }
    else {
        throw std::runtime_error("Unknown type while trying to print HeadlessEvent.");
    }
}
