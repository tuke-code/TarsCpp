#include "hello_test.h"
#include "mock/StatData.h"
#include "mock/TarsMockUtil.h"

int getStatCount(const StatDataList &data, const string &interfaceName = "")
{
    int sum = 0;

    for (const StatData &report : data)
    {
        for (const auto &entry : report)
        {
            if (!interfaceName.empty() && entry.first.interfaceName != interfaceName)
            {
                continue;
            }

            sum += entry.second.count;
        }
    }

    return sum;
}

int getClientTestHelloCount()
{
    return getStatCount(getClientStatData(), "testHello");
}

int waitForClientTestHelloCount(int expected, int baseline, int timeoutMs = 20000)
{
    const int64_t begin = TNOWMS;
    int current = 0;
    while (TNOWMS - begin < timeoutMs)
    {
        current = getClientTestHelloCount() - baseline;
        if (current >= expected)
        {
            return current;
        }

        const auto &scheduler = TC_CoroutineScheduler::scheduler();
        if (scheduler)
        {
            scheduler->sleep(100);
        }
        else
        {
            TC_Common::msleep(100);
        }
    }

    return current;
}

static void verifyStatReport(const HelloPrx &prx, const string &buffer)
{
    const int reportPerRound = 20;
    const int roundCount = 3;
    const int expectedTotal = reportPerRound * roundCount;

    clearClientStatData();
    int statBaseline = getClientTestHelloCount();

    int totalReport = 0;
    for (int round = 0; round < roundCount; ++round)
    {
        for (int i = 0; i < reportPerRound; ++i)
        {
            string result;
            prx->testHello(i, buffer, result);
        }
        totalReport += reportPerRound;

        int totalRealReport = waitForClientTestHelloCount(totalReport, statBaseline);
        LOG_CONSOLE_DEBUG << "report:" << reportPerRound
                          << ", totalReport:" << totalReport
                          << ", totalRealReport:" << totalRealReport << endl;

        ASSERT_GE(totalRealReport, totalReport);
        ASSERT_LE(totalRealReport - totalReport, 20);
    }

    ASSERT_GE(totalReport, expectedTotal);
}

TEST_F(HelloTest, statReport)
{
    TarsMockUtil tarsMockUtil;
    tarsMockUtil.startFramework();

    HelloServer hs;
    startServer(hs, CONFIG());

    shared_ptr<Communicator> c = getCommunicator();
    HelloPrx prx = getObj<HelloPrx>(c.get(), "HelloAdapter");
    verifyStatReport(prx, _buffer);

    stopServer(hs);
    tarsMockUtil.stopFramework();
}

TEST_F(HelloTest, statReportInCoroutine)
{
    TarsMockUtil tarsMockUtil;
    tarsMockUtil.startFramework();

    HelloServer hs;
    startServer(hs, CONFIG());

    shared_ptr<Communicator> c = getCommunicator();
    funcInCoroutine([=]() {
        HelloPrx prx = getObj<HelloPrx>(c.get(), "HelloAdapter");
        verifyStatReport(prx, _buffer);
    }, true);

    stopServer(hs);
    tarsMockUtil.stopFramework();
}
