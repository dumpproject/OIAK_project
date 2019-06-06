#ifndef TIMER_H
#define TIMER_H

#include <chrono>

template<typename T>
class Timer {
   public:
      Timer() : t0(std::chrono::high_resolution_clock::now()) {
      }
      T elapsed() const {
	 using namespace std::chrono;
	 auto time_spent = high_resolution_clock::now() - t0;
	 return duration<T, seconds::period>(time_spent).count();
      }
   private:
      std::chrono::high_resolution_clock::time_point t0;
};

#endif
