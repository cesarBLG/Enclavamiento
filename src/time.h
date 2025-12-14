#pragma once
#include <chrono>
#include <functional>
#include <memory>
inline int64_t get_milliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct timer
{
	int64_t time;
	std::function<void()> action;
};
std::shared_ptr<timer> set_timer(std::function<void()> action, int64_t delay);
void clear_timer(std::shared_ptr<timer> timer);