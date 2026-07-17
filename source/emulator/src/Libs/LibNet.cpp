#include "Kyty/Core/Common.h"

#include "Emulator/Common.h"
#include "Emulator/Libs/Libs.h"
#include "Emulator/Loader/SymbolDatabase.h"
#include "Emulator/Network.h"

#ifdef KYTY_EMU_ENABLED

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define NET_CALL(func)                                                                                                                     \
	[&]()                                                                                                                                  \
	{                                                                                                                                      \
		auto result = func;                                                                                                                \
		if (result < 0)                                                                                                                    \
		{                                                                                                                                  \
			*GetNetErrorAddr() = result;                                                                                                   \
		}                                                                                                                                  \
		return result;                                                                                                                     \
	}()

namespace Kyty::Libs {

namespace Network::Net {
struct NetEtherAddr;
} // namespace Network::Net

namespace LibNet {

LIB_VERSION("Net", 1, "Net", 1, 1);

static thread_local int g_net_errno = 0;

namespace Net = Network::Net;

KYTY_SYSV_ABI int* GetNetErrorAddr()
{
	return &g_net_errno;
}

static int KYTY_SYSV_ABI NetInit()
{
	return NET_CALL(Net::NetInit());
}

int KYTY_SYSV_ABI NetPoolCreate(const char* name, int size, int flags)
{
	return NET_CALL(Net::NetPoolCreate(name, size, flags));
}

int KYTY_SYSV_ABI NetPoolDestroy(int memid)
{
	return NET_CALL(Net::NetPoolDestroy(memid));
}

int KYTY_SYSV_ABI NetInetPton(int af, const char* src, void* dst)
{
	return NET_CALL(Net::NetInetPton(af, src, dst));
}

int KYTY_SYSV_ABI NetEtherNtostr(const Net::NetEtherAddr* n, char* str, size_t len)
{
	return NET_CALL(Net::NetEtherNtostr(n, str, len));
}

int KYTY_SYSV_ABI NetGetMacAddress(Net::NetEtherAddr* addr, int flags)
{
	return NET_CALL(Net::NetGetMacAddress(addr, flags));
}

// BSD-socket bridge. Note: like the real sceNet, failures BOTH set the errno
// cell (sceNetErrnoLoc) via NET_CALL and return the negative SCE code; guests
// built against OpenOrbis read the return value directly.
int KYTY_SYSV_ABI NetSocket(const char* name, int family, int type, int protocol)
{
	return NET_CALL(Net::NetSocket(name, family, type, protocol));
}

int KYTY_SYSV_ABI NetSocketClose(int sock)
{
	return NET_CALL(Net::NetSocketClose(sock));
}

int KYTY_SYSV_ABI NetShutdown(int sock, int how)
{
	return NET_CALL(Net::NetShutdown(sock, how));
}

int KYTY_SYSV_ABI NetConnect(int sock, const void* addr, uint32_t addrlen)
{
	return NET_CALL(Net::NetConnect(sock, addr, addrlen));
}

int KYTY_SYSV_ABI NetSend(int sock, const void* buf, size_t len, int flags)
{
	return NET_CALL(Net::NetSend(sock, buf, len, flags));
}

int KYTY_SYSV_ABI NetRecv(int sock, void* buf, size_t len, int flags)
{
	return NET_CALL(Net::NetRecv(sock, buf, len, flags));
}

int KYTY_SYSV_ABI NetSetsockopt(int sock, int level, int optname, const void* optval, uint32_t optlen)
{
	return NET_CALL(Net::NetSetsockopt(sock, level, optname, optval, optlen));
}

int KYTY_SYSV_ABI NetEpollCreate(const char* name, int flags)
{
	return NET_CALL(Net::NetEpollCreate(name, flags));
}

int KYTY_SYSV_ABI NetEpollControl(int eid, int op, int sock, const void* event)
{
	return NET_CALL(Net::NetEpollControl(eid, op, sock, event));
}

int KYTY_SYSV_ABI NetEpollWait(int eid, void* events, int maxevents, int timeout_usec)
{
	return NET_CALL(Net::NetEpollWait(eid, events, maxevents, timeout_usec));
}

int KYTY_SYSV_ABI NetEpollDestroy(int eid)
{
	return NET_CALL(Net::NetEpollDestroy(eid));
}

int KYTY_SYSV_ABI NetResolverCreate(const char* name, int memid, int flags)
{
	return NET_CALL(Net::NetResolverCreate(name, memid, flags));
}

int KYTY_SYSV_ABI NetResolverStartNtoa(int rid, const char* hostname, uint32_t* addr, int timeout, int retry, int flags)
{
	return NET_CALL(Net::NetResolverStartNtoa(rid, hostname, addr, timeout, retry, flags));
}

int KYTY_SYSV_ABI NetResolverDestroy(int rid)
{
	return NET_CALL(Net::NetResolverDestroy(rid));
}

int KYTY_SYSV_ABI NetResolverAbort(int rid, int flags)
{
	return NET_CALL(Net::NetResolverAbort(rid, flags));
}

LIB_DEFINE(InitNet_1_Net)
{
	LIB_FUNC("Nlev7Lg8k3A", LibNet::NetInit);
	LIB_FUNC("dgJBaeJnGpo", LibNet::NetPoolCreate);
	LIB_FUNC("K7RlrTkI-mw", LibNet::NetPoolDestroy);
	LIB_FUNC("8Kcp5d-q1Uo", LibNet::NetInetPton);
	LIB_FUNC("v6M4txecCuo", LibNet::NetEtherNtostr);
	LIB_FUNC("6Oc0bLsIYe0", LibNet::NetGetMacAddress);

	// BSD-socket bridge (host sockets)
	LIB_FUNC("Q4qBuN-c0ZM", LibNet::NetSocket);
	LIB_FUNC("45ggEzakPJQ", LibNet::NetSocketClose);
	LIB_FUNC("TSM6whtekok", LibNet::NetShutdown);
	LIB_FUNC("OXXX4mUk3uk", LibNet::NetConnect);
	LIB_FUNC("beRjXBn-z+o", LibNet::NetSend);
	LIB_FUNC("9wO9XrMsNhc", LibNet::NetRecv);
	LIB_FUNC("2mKX2Spso7I", LibNet::NetSetsockopt);
	LIB_FUNC("iWQWrwiSt8A", Net::NetHtons);
	LIB_FUNC("SF47kB2MNTo", LibNet::NetEpollCreate);
	LIB_FUNC("ZVw46bsasAk", LibNet::NetEpollControl);
	LIB_FUNC("drjIbDbA7UQ", LibNet::NetEpollWait);
	LIB_FUNC("Inp1lfL+Jdw", LibNet::NetEpollDestroy);
	LIB_FUNC("HQOwnfMGipQ", LibNet::GetNetErrorAddr); // sceNetErrnoLoc

	// DNS resolver (getaddrinfo bridge)
	LIB_FUNC("C4UgDHHPvdw", LibNet::NetResolverCreate);
	LIB_FUNC("Nd91WaWmG2w", LibNet::NetResolverStartNtoa);
	LIB_FUNC("kJlYH5uMAWI", LibNet::NetResolverDestroy);
	LIB_FUNC("AzqoBha7js4", LibNet::NetResolverAbort);
}

} // namespace LibNet

namespace LibSsl {

LIB_VERSION("Ssl", 1, "Ssl", 1, 1);

namespace Ssl = Network::Ssl;

LIB_DEFINE(InitNet_1_Ssl)
{
	LIB_FUNC("hdpVEUDFW3s", Ssl::SslInit);
	LIB_FUNC("0K1yQ6Lv-Yc", Ssl::SslTerm);
}

} // namespace LibSsl

namespace LibHttp {

LIB_VERSION("Http", 1, "Http", 1, 1);

namespace Http = Network::Http;

LIB_DEFINE(InitNet_1_Http)
{
	LIB_FUNC("A9cVMUtEp4Y", Http::HttpInit);
	LIB_FUNC("Ik-KpLTlf7Q", Http::HttpTerm);
	LIB_FUNC("0gYjPTR-6cY", Http::HttpCreateTemplate);
	LIB_FUNC("4I8vEpuEhZ8", Http::HttpDeleteTemplate);
	LIB_FUNC("s2-NPIvz+iA", Http::HttpSetNonblock);
	LIB_FUNC("htyBOoWeS58", Http::HttpsSetSslCallback);
	LIB_FUNC("mSQCxzWTwVI", Http::HttpsDisableOption);
	LIB_FUNC("6381dWF+xsQ", Http::HttpCreateEpoll);
	LIB_FUNC("wYhXVfS2Et4", Http::HttpDestroyEpoll);
	LIB_FUNC("-xm7kZQNpHI", Http::HttpSetEpoll);
	LIB_FUNC("59tL1AQBb8U", Http::HttpUnsetEpoll);
	LIB_FUNC("qgxDBjorUxs", Http::HttpCreateConnectionWithURL);
	LIB_FUNC("P6A3ytpsiYc", Http::HttpDeleteConnection);
	LIB_FUNC("Cnp77podkCU", Http::HttpCreateRequestWithURL2);
	LIB_FUNC("Aeu5wVKkF9w", Http::HttpCreateRequestWithURL);
	LIB_FUNC("qe7oZ+v4PWA", Http::HttpDeleteRequest);
	LIB_FUNC("EY28T2bkN7k", Http::HttpAddRequestHeader);
	LIB_FUNC("1e2BNwI-XzE", Http::HttpSendRequest);
	LIB_FUNC("0a2TBNfE3BU", Http::HttpGetStatusCode);
	LIB_FUNC("yuO2H2Uvnos", Http::HttpGetResponseContentLength);
	LIB_FUNC("P5pdoykPYTk", Http::HttpReadData);
	LIB_FUNC("Tc-hAYDKtQc", Http::HttpSetResolveTimeOut);
	LIB_FUNC("K1d1LqZRQHQ", Http::HttpSetResolveRetry);
	LIB_FUNC("0S9tTH0uqTU", Http::HttpSetConnectTimeOut);
	LIB_FUNC("xegFfZKBVlw", Http::HttpSetSendTimeOut);
	LIB_FUNC("yigr4V0-HTM", Http::HttpSetRecvTimeOut);
	LIB_FUNC("T-mGo9f3Pu4", Http::HttpSetAutoRedirect);
	LIB_FUNC("qFg2SuyTJJY", Http::HttpSetAuthEnabled);
}

} // namespace LibHttp

namespace LibNetCtl {

LIB_VERSION("NetCtl", 1, "NetCtl", 1, 1);

namespace NetCtl = Network::NetCtl;

LIB_DEFINE(InitNet_1_NetCtl)
{
	LIB_FUNC("gky0+oaNM4k", NetCtl::NetCtlInit);
	LIB_FUNC("JO4yuTuMoKI", NetCtl::NetCtlGetNatInfo);
	LIB_FUNC("iQw3iQPhvUQ", NetCtl::NetCtlCheckCallback);
	LIB_FUNC("uBPlr0lbuiI", NetCtl::NetCtlGetState);
	LIB_FUNC("UJ+Z7Q+4ck0", NetCtl::NetCtlRegisterCallback);
	LIB_FUNC("obuxdTiwkF8", NetCtl::NetCtlGetInfo);
}

} // namespace LibNetCtl

namespace LibNpManager {

LIB_VERSION("NpManager", 1, "NpManager", 1, 1);

namespace NpManager = Network::NpManager;

LIB_DEFINE(InitNet_1_NpManager)
{
	LIB_FUNC("3Zl8BePTh9Y", NpManager::NpCheckCallback);
	LIB_FUNC("Ec63y59l9tw", NpManager::NpSetNpTitleId);
	LIB_FUNC("A2CQ3kgSopQ", NpManager::NpSetContentRestriction);
	LIB_FUNC("VfRSmPmj8Q8", NpManager::NpRegisterStateCallback);
	LIB_FUNC("uFJpaKNBAj4", NpManager::NpRegisterGamePresenceCallback);
	LIB_FUNC("GImICnh+boA", NpManager::NpRegisterPlusEventCallback);
	LIB_FUNC("hw5KNqAAels", NpManager::NpRegisterNpReachabilityStateCallback);
	LIB_FUNC("p-o74CnoNzY", NpManager::NpGetNpId);
	LIB_FUNC("XDncXQIJUSk", NpManager::NpGetOnlineId);
	LIB_FUNC("eiqMCt9UshI", NpManager::NpCreateAsyncRequest);
	LIB_FUNC("S7QTn72PrDw", NpManager::NpDeleteRequest);
	LIB_FUNC("2rsFmlGWleQ", NpManager::NpCheckNpAvailability);
	LIB_FUNC("uqcPJLWL08M", NpManager::NpPollAsync);
	LIB_FUNC("eQH7nWPcAgc", NpManager::NpGetState);
}

} // namespace LibNpManager

namespace LibNpManagerForToolkit {

LIB_VERSION("NpManagerForToolkit", 1, "NpManager", 1, 1);

namespace NpManagerForToolkit = Network::NpManagerForToolkit;

LIB_DEFINE(InitNet_1_NpManagerForToolkit)
{
	LIB_FUNC("0c7HbXRKUt4", NpManagerForToolkit::NpRegisterStateCallbackForToolkit);
	LIB_FUNC("JELHf4xPufo", NpManagerForToolkit::NpCheckCallbackForLib);
}

} // namespace LibNpManagerForToolkit

namespace LibNpTrophy {

LIB_VERSION("NpTrophy", 1, "NpTrophy", 1, 1);

namespace NpTrophy = Network::NpTrophy;

LIB_DEFINE(InitNet_1_NpTrophy)
{
	LIB_FUNC("q7U6tEAQf7c", NpTrophy::NpTrophyCreateHandle);
	LIB_FUNC("XbkjbobZlCY", NpTrophy::NpTrophyCreateContext);
	LIB_FUNC("TJCAxto9SEU", NpTrophy::NpTrophyRegisterContext);
	LIB_FUNC("GNcF4oidY0Y", NpTrophy::NpTrophyDestroyHandle);
	LIB_FUNC("LHuSmO3SLd8", NpTrophy::NpTrophyGetTrophyUnlockState);
	LIB_FUNC("28xmRUFao68", NpTrophy::NpTrophyUnlockTrophy);
}

} // namespace LibNpTrophy

namespace LibNpWebApi {

LIB_VERSION("NpWebApi", 1, "NpWebApi", 1, 1);

namespace NpWebApi = Network::NpWebApi;

LIB_DEFINE(InitNet_1_NpWebApi)
{
	LIB_FUNC("G3AnLNdRBjE", NpWebApi::NpWebApiInitialize);
}

} // namespace LibNpWebApi

LIB_DEFINE(InitNet_1)
{
	LibNet::InitNet_1_Net(s);
	LibSsl::InitNet_1_Ssl(s);
	LibHttp::InitNet_1_Http(s);
	LibNetCtl::InitNet_1_NetCtl(s);
	LibNpManager::InitNet_1_NpManager(s);
	LibNpManagerForToolkit::InitNet_1_NpManagerForToolkit(s);
	LibNpTrophy::InitNet_1_NpTrophy(s);
	LibNpWebApi::InitNet_1_NpWebApi(s);
}

} // namespace Kyty::Libs

#endif // KYTY_EMU_ENABLED
