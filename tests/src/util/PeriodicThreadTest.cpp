#include "gtest/gtest.h"
#include "util/periodic_thread.h"
#include <atomic>

#include "test_utils.h"

class PeriodicThreadTest : public ::testing::Test
{
};

// PeriodicThreadTest.Basic sometimes has issues running on Windows
#ifndef _WIN32

TEST_F(PeriodicThreadTest, Basic)
{
	std::atomic<int> counter = 0;
	PeriodicThread thread = PeriodicThread("Test", [&]() {
		counter++;
	});
	thread.setPeriod(10);
	thread.start();
	usleep(15'000);
	ASSERT_LT(0, counter);
	int copy = counter;
	usleep(15'000);
	ASSERT_LT(copy, counter);
	thread.stop();
	copy = counter;
	usleep(15'000);
	ASSERT_EQ(copy, counter);
}

#endif
