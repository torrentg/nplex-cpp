#include <thread>
#include <doctest.h>
#include "mqueue.hpp"

using namespace std;
using namespace nplex;

#define QUEUE_LEN 5
#define T_NUM_VALS 100

TEST_CASE("mqueue")
{
    int sum = 0;
    int counter = 0;
    mqueue<int> queue{QUEUE_LEN};

    while (counter < QUEUE_LEN)
        CHECK_NOTHROW(queue.push(++counter));

    CHECK(counter == QUEUE_LEN);
    CHECK_THROWS_AS(queue.push(QUEUE_LEN + 1), nplex_mqueue_exceeded);
    CHECK(queue.try_push(QUEUE_LEN + 1) == false);
    CHECK(counter == QUEUE_LEN);

    thread worker([&queue, &sum] {
        int val = 0;
        while (val != T_NUM_VALS) {
            val = queue.pop();
            sum += val;
        }
    });

    while (counter < T_NUM_VALS + QUEUE_LEN) {
        if (queue.try_push(counter + 1))
            counter++;
        else
            this_thread::sleep_for(chrono::milliseconds(1)); // without this, valgrind takes a lot of time
    }

    worker.join();
    CHECK(sum == (T_NUM_VALS * (T_NUM_VALS + 1)) / 2);
    CHECK(counter == T_NUM_VALS + QUEUE_LEN);

    queue.clear();
}
