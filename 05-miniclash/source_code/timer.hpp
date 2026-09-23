#ifndef HASHCLASH_TIMER_HPP
#define HASHCLASH_TIMER_HPP

#include "types.hpp"

namespace hashclash
{

	class timer_detail;
	class timer {
	public:
		timer(bool direct_start = false);
		~timer();
		void start();
		void stop();
		double time() const;// get time between start and stop (or now if still running) in seconds
		bool isrunning() const { return running; } // check if timer is running

	private:
		timer_detail* detail;
		bool running;
	};

} // namespace hashclash

#endif // HASHCLASH_TIMER_HPP
