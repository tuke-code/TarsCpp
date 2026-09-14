
#include "hello_test.h"
#include "servant/AdapterProxy.h"
#include "servant/CommunicatorEpoll.h"
#include "servant/ObjectProxy.h"
#include "server/HelloImp.h"
#include "server/RpcServer.h"
#include "server/WinServer.h"
#include "util/tc_socket.h"

class PeerCloseCallback : public HelloPrxCallback
{
public:
    PeerCloseCallback(std::atomic<int> &callbackCount, std::atomic<int> &callbackRet)
    : _callbackCount(callbackCount), _callbackRet(callbackRet)
    {
    }

    void callback_testTimeout(int ret) override
    {
        _callbackRet.store(ret);
        _callbackCount.fetch_add(1);
    }

    void callback_testTimeout_exception(tars::Int32 ret) override
    {
        _callbackRet.store(ret);
        _callbackCount.fetch_add(1);
    }

private:
    std::atomic<int> &_callbackCount;
    std::atomic<int> &_callbackRet;
};

class RpcResultCallback : public HelloPrxCallback
{
public:
    RpcResultCallback(std::atomic<int> &callbackCount,
                      std::atomic<int> &callbackRet,
                      std::atomic<int> &unexpectedCount,
                      int expectedRet)
    : _callbackCount(callbackCount), _callbackRet(callbackRet),
      _unexpectedCount(unexpectedCount), _expectedRet(expectedRet)
    {
    }

    void callback_testTimeout(int ret) override
    {
        record(ret);
    }

    void callback_testTimeout_exception(tars::Int32 ret) override
    {
        record(ret);
    }

    void callback_testHello(int ret, const string &) override
    {
        record(ret);
    }

    void callback_testHello_exception(tars::Int32 ret) override
    {
        record(ret);
    }

private:
    void record(int ret)
    {
        if (ret != _expectedRet)
        {
            _unexpectedCount.fetch_add(1);
        }
        _callbackRet.store(ret);
        _callbackCount.fetch_add(1);
    }

    std::atomic<int> &_callbackCount;
    std::atomic<int> &_callbackRet;
    std::atomic<int> &_unexpectedCount;
    int _expectedRet;
};

static string createRefusedEndpoint()
{
    TC_Socket refusedSocket;
    refusedSocket.createSocket();
    refusedSocket.bind("127.0.0.1", 0);

    string refusedHost;
    uint16_t refusedPort = 0;
    refusedSocket.getSockName(refusedHost, refusedPort);
    refusedSocket.close();

    return "tcp -h 127.0.0.1 -p " + TC_Common::tostr(refusedPort) + " -t 60000";
}

static HelloPrx createRefusedProxy(Communicator *comm)
{
    return comm->stringToProxy<HelloPrx>(
        "TestApp.RpcServer.HelloObj@" + createRefusedEndpoint());
}

static HelloPrx createMixedProxy(Communicator *comm, uint16_t healthyPort)
{
    return comm->stringToProxy<HelloPrx>(
        "TestApp.RpcServer.HelloObj@" + createRefusedEndpoint() +
        ":tcp -h 127.0.0.1 -p " + TC_Common::tostr(healthyPort) + " -t 60000");
}

TEST_F(HelloTest, prxClose)
{
    auto comm = getCommunicator();

    WinServer ws;
    startServer(ws, WIN_CONFIG());

    string obj = getObj(ws.getConfig(), "WinAdapter");

    HelloPrx prx = comm->stringToProxy<HelloPrx>(obj);
    prx->testClose();

    EXPECT_EQ(HelloImp::_current.size(), 1);

    prx->tars_close();

    TC_Common::msleep(10);
    EXPECT_EQ(HelloImp::_current.size(), 0);

    stopServer(ws);

}

TEST_F(HelloTest, prxCloseInCoroutine)
{
    auto comm = getCommunicator();

    WinServer ws;
    startServer(ws, WIN_CONFIG());

    string obj = getObj(ws.getConfig(), "WinAdapter");

    HelloPrx prx = comm->stringToProxy<HelloPrx>(obj);
    prx->testClose();

    EXPECT_EQ(HelloImp::_current.size(), 1);

    funcInCoroutine([&](){
        prx->testClose();
    });

    EXPECT_EQ(HelloImp::_current.size(), 2);

    prx->tars_close();

    TC_Common::msleep(100);
    EXPECT_LE(HelloImp::_current.size(), 2);

    stopServer(ws);

}

TEST_F(HelloTest, rpcFailsFastWhenConnectionCloses)
{
    auto comm = getCommunicator();

    RpcServer server;
    TC_Config serverConfig = RPC1_CONFIG();
    startServer(server, serverConfig, TC_EpollServer::NET_THREAD_MERGE_HANDLES_THREAD);

    string obj = getObj(serverConfig, "RpcAdapter");
    HelloPrx prx = comm->stringToProxy<HelloPrx>(obj);
    string result;

    // 先建立连接，确保后续请求属于已连接的 AdapterProxy。
    ASSERT_EQ(prx->testHello(0, _buffer, result), 0);

    std::atomic<int> callbackCount{0};
    std::atomic<int> callbackRet{0};
    HelloPrxCallbackPtr callback(new PeerCloseCallback(callbackCount, callbackRet));

    const int64_t begin = TNOWMS;
    prx->async_testTimeout(callback, 1);

    // 在网络线程中关闭客户端连接，模拟已建立连接的异常断开。
    TC_Common::msleep(50);
    for (ObjectProxy *objectProxy : prx->getObjectProxys())
    {
        for (AdapterProxy *adapterProxy : objectProxy->getAdapters())
        {
            if (adapterProxy != nullptr && adapterProxy->trans()->isValid())
            {
                objectProxy->getCommunicatorEpoll()->getEpoller()->asyncCallback([adapterProxy]() {
                    adapterProxy->trans()->close();
                });
            }
        }
    }

    while (callbackCount.load() == 0 && TNOWMS - begin < 2000)
    {
        TC_Common::msleep(10);
    }

    EXPECT_EQ(callbackCount.load(), 1);
    EXPECT_EQ(callbackRet.load(), TARSPROXYCONNECTERR);
    EXPECT_LT(TNOWMS - begin, 2000);

    stopServer(server);
}

TEST_F(HelloTest, rpcFailsFastWhenConnectionRefusedSync)
{
    auto comm = getCommunicator();
    HelloPrx prx = createRefusedProxy(comm.get());
    prx->tars_set_timeout(60000);

    const int64_t begin = TNOWMS;
    bool connectionException = false;
    try
    {
        prx->testTimeout(1);
    }
    catch (const TarsServerConnectionException &)
    {
        connectionException = true;
    }

    EXPECT_TRUE(connectionException);
    EXPECT_LT(TNOWMS - begin, 2000);
}

TEST_F(HelloTest, rpcFailsFastWhenConnectionRefusedAsync)
{
    auto comm = getCommunicator();
    HelloPrx prx = createRefusedProxy(comm.get());
    prx->tars_async_timeout(60000);

    std::atomic<int> callbackCount{0};
    std::atomic<int> callbackRet{0};
    std::atomic<int> unexpectedCount{0};
    HelloPrxCallbackPtr callback(new RpcResultCallback(
        callbackCount, callbackRet, unexpectedCount, TARSPROXYCONNECTERR));

    const int64_t begin = TNOWMS;
    prx->async_testTimeout(callback, 1);

    while (callbackCount.load() == 0 && TNOWMS - begin < 2000)
    {
        TC_Common::msleep(10);
    }

    EXPECT_EQ(callbackCount.load(), 1);
    EXPECT_EQ(callbackRet.load(), TARSPROXYCONNECTERR);
    EXPECT_EQ(unexpectedCount.load(), 0);
    EXPECT_LT(TNOWMS - begin, 2000);
}

TEST_F(HelloTest, rpcFailsFastWhenConnectionRefusedConcurrently)
{
    auto comm = getCommunicator();
    HelloPrx prx = createRefusedProxy(comm.get());
    prx->tars_async_timeout(60000);

    const int requestCount = 32;
    std::atomic<int> callbackCount{0};
    std::atomic<int> callbackRet{0};
    std::atomic<int> unexpectedCount{0};
    HelloPrxCallbackPtr callback(new RpcResultCallback(
        callbackCount, callbackRet, unexpectedCount, TARSPROXYCONNECTERR));

    const int64_t begin = TNOWMS;
    vector<std::thread> workers;
    workers.reserve(requestCount);
    for (int i = 0; i < requestCount; ++i)
    {
        workers.emplace_back([&prx, &callback]() {
            prx->async_testTimeout(callback, 1);
        });
    }

    for (auto &worker : workers)
    {
        worker.join();
    }

    while (callbackCount.load() < requestCount && TNOWMS - begin < 2000)
    {
        TC_Common::msleep(10);
    }

    EXPECT_EQ(callbackCount.load(), requestCount);
    EXPECT_EQ(callbackRet.load(), TARSPROXYCONNECTERR);
    EXPECT_EQ(unexpectedCount.load(), 0);
    EXPECT_LT(TNOWMS - begin, 2000);
}

TEST_F(HelloTest, rpcUsesHealthyEndpointWhenOtherEndpointRefused)
{
    auto comm = getCommunicator();
    RpcServer server;
    TC_Config serverConfig = RPC1_CONFIG();
    startServer(server, serverConfig, TC_EpollServer::NET_THREAD_MERGE_HANDLES_THREAD);

    HelloPrx prx = createMixedProxy(comm.get(), 9990);
    prx->tars_async_timeout(60000);

    const int requestCount = 32;
    std::atomic<int> callbackCount{0};
    std::atomic<int> callbackRet{0};
    std::atomic<int> unexpectedCount{0};
    HelloPrxCallbackPtr callback(new RpcResultCallback(
        callbackCount, callbackRet, unexpectedCount, TARSSERVERSUCCESS));
    const string request = _buffer;

    const int64_t begin = TNOWMS;
    vector<std::thread> workers;
    workers.reserve(requestCount);
    for (int i = 0; i < requestCount; ++i)
    {
        workers.emplace_back([&prx, &callback, &request]() {
            prx->async_testHello(callback, 0, request);
        });
    }

    for (auto &worker : workers)
    {
        worker.join();
    }

    while (callbackCount.load() < requestCount && TNOWMS - begin < 3000)
    {
        TC_Common::msleep(10);
    }

    EXPECT_EQ(callbackCount.load(), requestCount);
    EXPECT_EQ(callbackRet.load(), 0);
    EXPECT_EQ(unexpectedCount.load(), 0);
    EXPECT_LT(TNOWMS - begin, 3000);

    stopServer(server);
}

TEST_F(HelloTest, rpcHashUsesHealthyEndpointWhenOtherEndpointRefused)
{
    auto comm = getCommunicator();
    RpcServer server;
    TC_Config serverConfig = RPC1_CONFIG();
    startServer(server, serverConfig, TC_EpollServer::NET_THREAD_MERGE_HANDLES_THREAD);

    HelloPrx prx = createMixedProxy(comm.get(), 9990);
    prx->tars_set_timeout(60000);

    const int64_t begin = TNOWMS;
    for (size_t key = 0; key < 32; ++key)
    {
        string result;
        EXPECT_EQ(prx->tars_hash(key)->testHello(0, _buffer, result), 0);
        EXPECT_EQ(result, _buffer);
        EXPECT_EQ(prx->tars_consistent_hash(key)->testHello(0, _buffer, result), 0);
        EXPECT_EQ(result, _buffer);
    }

    EXPECT_LT(TNOWMS - begin, 3000);
    stopServer(server);
}

TEST_F(HelloTest, rpcConnectionCloseDoesNotAffectOtherEndpoint)
{
    auto comm = getCommunicator();

    RpcServer serverA;
    RpcServer serverB;
    TC_Config configA = RPC1_CONFIG();
    TC_Config configB = RPC2_CONFIG();
    startServer(serverA, configA, TC_EpollServer::NET_THREAD_MERGE_HANDLES_THREAD);
    startServer(serverB, configB, TC_EpollServer::NET_THREAD_MERGE_HANDLES_THREAD);

    HelloPrx prxA = comm->stringToProxy<HelloPrx>(getObj(configA, "RpcAdapter"));
    HelloPrx prxB = comm->stringToProxy<HelloPrx>(getObj(configB, "RpcAdapter"));
    string result;
    ASSERT_EQ(prxA->testHello(0, _buffer, result), 0);
    ASSERT_EQ(prxB->testHello(0, _buffer, result), 0);

    std::atomic<int> callbackCountA{0};
    std::atomic<int> callbackRetA{0};
    std::atomic<int> callbackCountB{0};
    std::atomic<int> callbackRetB{0};
    HelloPrxCallbackPtr callbackA(new PeerCloseCallback(callbackCountA, callbackRetA));
    HelloPrxCallbackPtr callbackB(new PeerCloseCallback(callbackCountB, callbackRetB));

    const int64_t begin = TNOWMS;
    prxA->async_testTimeout(callbackA, 1);
    prxB->async_testTimeout(callbackB, 1);

    TC_Common::msleep(50);
    for (ObjectProxy *objectProxy : prxA->getObjectProxys())
    {
        for (AdapterProxy *adapterProxy : objectProxy->getAdapters())
        {
            if (adapterProxy != nullptr && adapterProxy->trans()->isValid())
            {
                objectProxy->getCommunicatorEpoll()->getEpoller()->asyncCallback([adapterProxy]() {
                    adapterProxy->trans()->close();
                });
            }
        }
    }

    while ((callbackCountA.load() == 0 || callbackCountB.load() == 0) && TNOWMS - begin < 3000)
    {
        TC_Common::msleep(10);
    }

    EXPECT_EQ(callbackCountA.load(), 1);
    EXPECT_EQ(callbackRetA.load(), TARSPROXYCONNECTERR);
    EXPECT_EQ(callbackCountB.load(), 1);
    EXPECT_EQ(callbackRetB.load(), 0);

    stopServer(serverA);
    stopServer(serverB);
}
