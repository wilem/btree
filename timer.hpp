#ifndef __MY_TIMER__
#define __MY_TIMER__

#include <cstdio>
#include <iostream>
#include <ctime>

class Timer
{
public:

	clock_t start_tick;

	Timer()
	{
		start_tick = 0;
	}

	void Start()
	{
		// report
		std::cout << "timer started: "<<  get_time().c_str() << std::endl;

		start_tick = clock();
	}

	// return elapsed time: in sec.
	double Stop()
	{
		clock_t end_tick = clock();
		clock_t elapsed_tick = end_tick - start_tick;
		// report
		std::cout << "timer stopped: " << get_time().c_str() << std::endl;
		double sec = (double) elapsed_tick / (double) CLOCKS_PER_SEC;
		std::cout << "Elapsed time(sec): " << sec << std::endl;

		return sec;
	}

	void Reset()
	{
		Start();
	}

	std::string get_time()
	{
		char buffer[80];

#if defined(__MINGW32__) || defined(__GNUC__)
		time_t rawtime;
		struct tm *timeinfo;
		rawtime = time(0);
		timeinfo = localtime(&rawtime);
		snprintf(buffer, sizeof(buffer), "%s\b", asctime(timeinfo));
#elif defined(__WIN32__)
		time_t rawtime;
		struct tm timeinfo;
		time(&rawtime);
		localtime_s(&timeinfo, &rawtime);
		strftime(buffer, 80, "%I:%M%p.", &timeinfo);
#endif
		return std::string(buffer);
	}
	
};



#endif
