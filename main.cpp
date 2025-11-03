
#include <regex>
#include <set>
#include "time.h"
#include "items.h"
#include "mqtt.h"
void event_loop();
int main(int argc, char **argv)
{
    init_mqtt(argc > 1 ? std::string(argv[1]) : "main");

    init_items(argc > 2 ? std::string(argv[2]) : "");

    event_loop();

    exit_mqtt();
    return 0;
}
bool running = true;
std::set<std::shared_ptr<timer>> timer_queue;
std::shared_ptr<timer> set_timer(std::function<void()> action, int64_t delay)
{
    auto t = std::make_shared<timer>(timer({get_milliseconds()+delay, action}));
    timer_queue.insert(t);
    return t;
}
void clear_timer(std::shared_ptr<timer> timer)
{
    timer_queue.erase(timer);
}
void event_loop()
{
    while (running) {
		int64_t now = get_milliseconds();
        int64_t wait_time = 1000;
        for (auto it=timer_queue.begin(); it!=timer_queue.end(); ) {
            if ((*it)->time <= now) {
                (*it)->action();
                it = timer_queue.erase(it);
            } else {
                wait_time = std::min((*it)->time - now, wait_time);
                ++it;
            }
        }
        loop_items();
		loop_mqtt(wait_time);
	};
}