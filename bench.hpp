#ifndef __BENCH_H__
#define __BENCH_H__

#include <map>
#include <algorithm>
#include <iostream>

#include "timer.hpp"

using namespace std;

class benchmark
{
	const int cnt;
	int *keys;

	map<int, int> mkv;
	Timer timer;

public:
	benchmark(int cnt) :cnt(cnt)
	{
		keys = new int[cnt];
		random_shuffle(keys, keys + cnt);
	}

#define REPORT 11

	void test()
	{
		test_insert();
		test_search();
		test_erase();
	}

	void test_insert()
	{
		cout << "\nTEST BENCHMARK: INSERTING data..." << endl;
		timer.Start();
		for (int i = 0, i_prev = 0; i < cnt; i++) {
			int k = keys[i];
			mkv[k] = k * 2;
#if REPORT
			// progress report:
			if ((i - i_prev) > 0.001 * cnt) {
				i_prev = i;
				cerr << i * 100.0 / cnt << "%" << "\r";
			}
#endif
		}
		double ins_time = timer.Stop();
		cout << "After " << ins_time << " seconds." << endl;
		cout << "Finished inserting data..." << endl;
		cout << "time for every inserting(sec): " << ins_time / cnt << endl;
	}

	void test_search()
	{
		cout << "\nTEST BENCHMARK: SEARCHING data..." << endl;
		timer.Start();
		for (int i = 0, i_prev = 0; i < cnt; i++) {
			int v = mkv[keys[i]];
			v;
#if REPORT
			// progress report:
			if ((i - i_prev) > 0.001 * cnt) {
				i_prev = i;
				cerr << i * 100.0 / cnt << "%" << "\r";
			}
#endif
		}
		double ins_time = timer.Stop();
		cout << "After " << ins_time << " seconds." << endl;
		cout << "Finished searching data..." << endl;
		cout << "time for every search(sec): " << ins_time / cnt << endl;
	}

	void test_erase()
	{
		cout << "\nTEST BENCHMARK: ERASING data..." << endl;
		timer.Start();
		for (int i = 0, i_prev = 0; i < cnt; i++) {
			mkv.erase(keys[i]);
#if REPORT
			// progress report:
			if ((i - i_prev) > 0.001 * cnt) {
				i_prev = i;
				cerr << i * 100.0 / cnt << "%" << "\r";
			}
#endif
		}
		double ins_time = timer.Stop();
		cout << "After " << ins_time << " seconds." << endl;
		cout << "FINISHED ERASING data..." << endl;
		cout << "time for every erase(sec): " << ins_time / cnt << endl;
	}
};

#endif
