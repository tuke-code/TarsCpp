#include "util/tc_timeout_queue_new.h"
#include "util/tc_timeout_queue_noid.h"
#include "gtest/gtest.h"

#include <vector>

using namespace tars;

TEST(TCTimeoutQueueNewTest, DrainPreservesSendStateAndOrder)
{
    TC_TimeoutQueueNew<int *> queue;
    int sentValue = 1;
    int queuedValue = 2;
    int *sent = &sentValue;
    int *queued = &queuedValue;

    ASSERT_TRUE(queue.push(sent, 1, TNOWMS + 10000));
    ASSERT_TRUE(queue.push(queued, 2, TNOWMS + 10000, false));

    std::vector<TC_TimeoutQueueNew<int *>::PendingInfo> pending;
    queue.drain(pending);

    ASSERT_EQ(pending.size(), 2U);
    EXPECT_EQ(pending[0].ptr, queued);
    EXPECT_FALSE(pending[0].hasSend);
    EXPECT_EQ(pending[1].ptr, sent);
    EXPECT_TRUE(pending[1].hasSend);
    EXPECT_EQ(queue.size(), 0U);
    EXPECT_TRUE(queue.sendListEmpty());
}

TEST(TCTimeoutQueueNewTest, ExtractIfPreservesOtherRequests)
{
    TC_TimeoutQueueNoID<int *> queue;
    int firstValue = 1;
    int secondValue = 2;
    int *first = &firstValue;
    int *second = &secondValue;

    ASSERT_TRUE(queue.push(first, TNOWMS + 10000));
    ASSERT_TRUE(queue.push(second, TNOWMS + 10000));

    std::vector<int *> extracted;
    queue.extractIf([first](int *value) {
        return value == first;
    }, extracted);

    ASSERT_EQ(extracted.size(), 1U);
    EXPECT_EQ(extracted[0], first);
    ASSERT_EQ(queue.size(), 1U);

    int *remaining = nullptr;
    ASSERT_TRUE(queue.pop(remaining));
    EXPECT_EQ(remaining, second);
}
