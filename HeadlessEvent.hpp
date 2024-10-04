#include <string>
#include <vector>
#include <variant>
#include <stdint.h>


struct HeadlessEvent
{
    enum EventType {
        AVAILABLE,
        PLAY,
        SAVE,
        MARK
    } type;
    
    struct AnimationParams {
        float animation_playback_time;
        float animation_rate;
    };

    uint32_t ts; // Microseconds
    
    // Use std::variant to store the different event parameters
    std::variant<std::monostate, std::string, AnimationParams> event_params;

    // Constructor for AVAILABLE
    HeadlessEvent(EventType type, uint32_t ts);

    // Constructor for SAVE or MARK
    HeadlessEvent(EventType type, const std::string& params, uint32_t ts);

    // Constructor for PLAY
    HeadlessEvent(EventType type, float t, float rate, uint32_t ts);

    static std::vector<HeadlessEvent> load_events(std::string filename);

    void print() const;
};

// void write_ppm();

