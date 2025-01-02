#include <thread>
#include <doctest.h>
#include "mqueue.hpp"

using namespace std;
using namespace nplex;

TEST_CASE("mqueue")
{
    int sum = 0;
    int counter = 0;
    mqueue<int> queue{5};

    while (counter < 5)
        CHECK_NOTHROW(queue.push(++counter));

    CHECK_THROWS_AS(queue.push(6), nplex_mqueue_exceeded);
    CHECK(queue.try_push(6) == false);

    thread worker([&queue, &sum] {
        int val = 0;
        while (val != 100) {
            val = queue.pop();
            sum += val;
        }
    });

    while (counter < 105) {
        if (queue.try_push(counter + 1))
            counter++;
    }

    worker.join();
    CHECK(sum == (100 * 101) / 2);
    CHECK(counter == 105);

    queue.clear();
}

