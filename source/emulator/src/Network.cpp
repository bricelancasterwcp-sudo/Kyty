#include "Emulator/Network.h"

#include "Kyty/Core/ByteBuffer.h"
#include "Kyty/Core/Common.h"
#include "Kyty/Core/DbgAssert.h"
#include "Kyty/Core/String.h"
#include "Kyty/Core/Threads.h"
#include "Kyty/Core/Vector.h"

#include "Emulator/Kernel/Pthread.h"
#include "Emulator/Libs/Errno.h"
#include "Emulator/Libs/Libs.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <curl/curl.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#ifdef KYTY_EMU_ENABLED

namespace Kyty::Libs::Network {

// SCE_HTTP_CONTENTLEN_* result values for sceHttpGetResponseContentLength
// (ORBIS_HTTP_CONTENTLEN_* in OpenOrbis orbis/_types/http.h)
constexpr int HTTP_CONTENTLEN_EXIST     = 0;
constexpr int HTTP_CONTENTLEN_NOT_FOUND = 1;
constexpr int HTTP_CONTENTLEN_CHUNK_ENC = 2;

class Network
{
public:
	class Id
	{
	public:
		static constexpr int MAX_ID = 65536;

		enum class Type : uint32_t
		{
			Invalid    = 0,
			Http       = 1,
			Ssl        = 2,
			Template   = 3,
			Connection = 4,
			Request    = 5,
		};

		explicit Id(int id): m_id(static_cast<uint32_t>(id) & 0xffffu), m_type(static_cast<uint32_t>(id) >> 16u) {}
		[[nodiscard]] int  ToInt() const { return static_cast<int>(m_id + (static_cast<uint32_t>(m_type) << 16u)); }
		[[nodiscard]] bool IsValid() const { return GetType() != Type::Invalid; }
		[[nodiscard]] Type GetType() const
		{
			switch (m_type)
			{
				case static_cast<uint32_t>(Type::Http): return Type::Http; break;
				case static_cast<uint32_t>(Type::Ssl): return Type::Ssl; break;
				case static_cast<uint32_t>(Type::Template): return Type::Template; break;
				case static_cast<uint32_t>(Type::Connection): return Type::Connection; break;
				case static_cast<uint32_t>(Type::Request): return Type::Request; break;
				default: return Type::Invalid;
			}
		}

		friend class Network;

	private:
		Id() = default;
		static Id Invalid() { return {}; }
		static Id Create(int net_id, Type type)
		{
			Id r;
			r.m_id   = net_id;
			r.m_type = static_cast<uint32_t>(type);
			return r;
		}
		[[nodiscard]] int GetId() const { return static_cast<int>(m_id); }

		uint32_t m_id   = 0;
		uint32_t m_type = static_cast<uint32_t>(Type::Invalid);
	};

	using HttpsCallback = KYTY_SYSV_ABI int (*)(int, unsigned int, void* const*, int, void*);

	Network()          = default;
	virtual ~Network() = default;

	KYTY_CLASS_NO_COPY(Network);

	int  PoolCreate(const char* name, int size);
	bool PoolDestroy(int memid);

	Id   SslInit(uint64_t pool_size);
	bool SslTerm(Id ssl_ctx_id);

	Id   HttpInit(int memid, Id ssl_ctx_id, uint64_t pool_size);
	bool HttpTerm(Id http_ctx_id);
	Id   HttpCreateTemplate(Id http_ctx_id, const char* user_agent, int http_ver, bool is_auto_proxy_conf);
	bool HttpDeleteTemplate(Id tmpl_id);
	bool HttpSetNonblock(Id id, bool enable);
	bool HttpsSetSslCallback(Id id, HttpsCallback cbfunc, void* user_arg);
	bool HttpsDisableOption(Id id, uint32_t ssl_flags);
	bool HttpAddRequestHeader(Id id, const char* name, const char* value, bool add);
	bool HttpValid(Id http_ctx_id);
	bool HttpValidTemplate(Id tmpl_id);
	bool HttpValidConnection(Id conn_id);
	bool HttpValidRequest(Id req_id);
	Id   HttpCreateConnectionWithURL(Id tmpl_id, const char* url, bool enable_keep_alive);
	bool HttpDeleteConnection(Id conn_id);
	Id   HttpCreateRequestWithURL2(Id conn_id, const char* method, const char* url, uint64_t content_length);
	bool HttpDeleteRequest(Id req_id);
	bool HttpSetResolveTimeOut(Id id, uint32_t usec);
	bool HttpSetResolveRetry(Id id, int32_t retry);
	bool HttpSetConnectTimeOut(Id id, uint32_t usec);
	bool HttpSetSendTimeOut(Id id, uint32_t usec);
	bool HttpSetRecvTimeOut(Id id, uint32_t usec);
	bool HttpSetAutoRedirect(Id id, int enable);
	bool HttpSetAuthEnabled(Id id, int enable);

	// These return an SCE error code (OK on success) rather than bool: the
	// transport has more than one failure mode the guest distinguishes.
	int HttpSendRequest(Id req_id, const void* post_data, size_t post_size);
	int HttpGetStatusCode(Id req_id, int* status_code);
	int HttpGetResponseContentLength(Id req_id, int* result, uint64_t* content_length);
	int HttpReadData(Id req_id, void* data, size_t size);

private:
	struct Pool
	{
		bool   used = false;
		String name;
		int    size = 0;
	};

	struct Ssl
	{
		bool     used = false;
		uint64_t size = 0;
	};

	struct Http
	{
		bool     used       = false;
		uint64_t size       = 0;
		int      memid      = 0;
		int      ssl_ctx_id = 0;
	};

	struct HttpHeader
	{
		String name;
		String value;
	};

	struct HttpBase
	{
		Vector<HttpHeader> headers;
		bool               used            = false;
		bool               nonblock        = false;
		bool               auto_redirect   = true;
		bool               auth_enabled    = true;
		HttpsCallback      ssl_cbfunc      = nullptr;
		void*              ssl_user_arg    = nullptr;
		uint32_t           ssl_flags       = 0xA7;
		int                http_ctx_id     = 0;
		uint32_t           resolve_timeout = 1'000000;
		int32_t            resolve_retry   = 4;
		uint32_t           connect_timeout = 30'000000;
		uint32_t           send_timeout    = 120'000000;
		uint32_t           recv_timeout    = 120'000000;
	};

	struct HttpTemplate: public HttpBase
	{
		String user_agent;
		int    http_ver           = 0;
		bool   is_auto_proxy_conf = true;
	};

	struct HttpConnection: public HttpTemplate
	{
		explicit HttpConnection(const HttpTemplate& tmpl): HttpTemplate(tmpl) {}
		// int    tmpl_id = 0;
		String url;
		bool   enable_keep_alive = false;
	};

	struct HttpRequest: public HttpConnection
	{
		explicit HttpRequest(HttpConnection& conn): HttpConnection(conn) {}
		// int      conn_id = 0;
		String   method;
		String   url;
		uint64_t content_length = 0;
		// Response state, filled by HttpSendRequest()
		Core::ByteBuffer body;
		uint64_t         read_pos             = 0;
		uint64_t         response_length      = 0;
		int              response_length_type = HTTP_CONTENTLEN_NOT_FOUND;
		int              status_code          = 0;
		bool             performed            = false;
	};

	static constexpr int POOLS_MAX = 32;
	static constexpr int SSL_MAX   = 32;
	static constexpr int HTTP_MAX  = 32;

	Core::Mutex            m_mutex;
	Pool                   m_pools[POOLS_MAX];
	Ssl                    m_ssl[SSL_MAX];
	Http                   m_http[HTTP_MAX];
	Vector<HttpTemplate>   m_templates;
	Vector<HttpConnection> m_connections;
	Vector<HttpRequest>    m_requests;
};

static Network* g_net = nullptr;

KYTY_SUBSYSTEM_INIT(Network)
{
	EXIT_IF(g_net != nullptr);

	g_net = new Network;

	// Not thread-safe; must happen here, before any guest thread can reach the Http HLE
	curl_global_init(CURL_GLOBAL_DEFAULT);
}

KYTY_SUBSYSTEM_UNEXPECTED_SHUTDOWN(Network) {}

KYTY_SUBSYSTEM_DESTROY(Network) {}

int Network::PoolCreate(const char* name, int size)
{
	Core::LockGuard lock(m_mutex);

	for (int id = 0; id < POOLS_MAX; id++)
	{
		if (!m_pools[id].used)
		{
			m_pools[id].used = true;
			m_pools[id].size = size;
			m_pools[id].name = String::FromUtf8(name);

			return id;
		}
	}

	return -1;
}

bool Network::PoolDestroy(int memid)
{
	Core::LockGuard lock(m_mutex);

	if (memid >= 0 && memid < POOLS_MAX && m_pools[memid].used)
	{
		m_pools[memid].used = false;

		return true;
	}

	return false;
}

Network::Id Network::SslInit(uint64_t pool_size)
{
	Core::LockGuard lock(m_mutex);

	for (int id = 0; id < SSL_MAX; id++)
	{
		if (!m_ssl[id].used)
		{
			m_ssl[id].used = true;
			m_ssl[id].size = pool_size;

			return Id::Create(id, Id::Type::Ssl);
		}
	}

	return Id::Invalid();
}

bool Network::SslTerm(Id ssl_ctx_id)
{
	Core::LockGuard lock(m_mutex);

	if (ssl_ctx_id.GetType() == Id::Type::Ssl && ssl_ctx_id.GetId() >= 0 && ssl_ctx_id.GetId() < SSL_MAX && m_ssl[ssl_ctx_id.GetId()].used)
	{
		m_ssl[ssl_ctx_id.GetId()].used = false;

		return true;
	}

	return false;
}

Network::Id Network::HttpInit(int memid, Id ssl_ctx_id, uint64_t pool_size)
{
	Core::LockGuard lock(m_mutex);

	if (ssl_ctx_id.GetType() == Id::Type::Ssl && ssl_ctx_id.GetId() >= 0 && ssl_ctx_id.GetId() < SSL_MAX &&
	    m_ssl[ssl_ctx_id.GetId()].used && memid >= 0 && memid < POOLS_MAX && m_pools[memid].used)
	{
		for (int id = 0; id < HTTP_MAX; id++)
		{
			if (!m_http[id].used)
			{
				m_http[id].used       = true;
				m_http[id].size       = pool_size;
				m_http[id].ssl_ctx_id = ssl_ctx_id.GetId();
				m_http[id].memid      = memid;

				return Id::Create(id, Id::Type::Http);
			}
		}
	}

	return Id::Invalid();
}

bool Network::HttpValid(Id http_ctx_id)
{
	Core::LockGuard lock(m_mutex);

	return (http_ctx_id.GetType() == Id::Type::Http && http_ctx_id.GetId() >= 0 && http_ctx_id.GetId() < HTTP_MAX &&
	        m_http[http_ctx_id.GetId()].used);
}

bool Network::HttpValidTemplate(Id tmpl_id)
{
	Core::LockGuard lock(m_mutex);

	return (tmpl_id.GetType() == Id::Type::Template && m_templates.IndexValid(tmpl_id.GetId()) && m_templates.At(tmpl_id.GetId()).used);
}

bool Network::HttpValidConnection(Id conn_id)
{
	Core::LockGuard lock(m_mutex);

	return (conn_id.GetType() == Id::Type::Connection && m_connections.IndexValid(conn_id.GetId()) &&
	        m_connections.At(conn_id.GetId()).used);
}

bool Network::HttpValidRequest(Id req_id)
{
	Core::LockGuard lock(m_mutex);

	return (req_id.GetType() == Id::Type::Request && m_requests.IndexValid(req_id.GetId()) && m_requests.At(req_id.GetId()).used);
}

bool Network::HttpTerm(Id http_ctx_id)
{
	Core::LockGuard lock(m_mutex);

	if (HttpValid(http_ctx_id))
	{
		m_http[http_ctx_id.GetId()].used = false;

		return true;
	}

	return false;
}

Network::Id Network::HttpCreateTemplate(Id http_ctx_id, const char* user_agent, int http_ver, bool is_auto_proxy_conf)
{
	Core::LockGuard lock(m_mutex);

	if (HttpValid(http_ctx_id))
	{
		HttpTemplate tn {};
		tn.used               = true;
		tn.http_ver           = http_ver;
		tn.user_agent         = String::FromUtf8(user_agent);
		tn.is_auto_proxy_conf = is_auto_proxy_conf;
		tn.http_ctx_id        = http_ctx_id.GetId();
		tn.nonblock           = false;

		int index = 0;
		for (auto& t: m_templates)
		{
			if (!t.used)
			{
				t = tn;
				return Id::Create(index, Id::Type::Template);
			}
			index++;
		}

		if (index < Id::MAX_ID)
		{
			m_templates.Add(tn);
			return Id::Create(index, Id::Type::Template);
		}
	}

	return Id::Invalid();
}

Network::Id Network::HttpCreateConnectionWithURL(Id tmpl_id, const char* url, bool enable_keep_alive)
{
	Core::LockGuard lock(m_mutex);

	if (HttpValidTemplate(tmpl_id))
	{
		HttpConnection cn(m_templates[tmpl_id.GetId()]);
		cn.used              = true;
		cn.enable_keep_alive = enable_keep_alive;
		cn.url               = String::FromUtf8(url);
		// cn.tmpl_id           = tmpl_id.ToInt();

		int index = 0;
		for (auto& t: m_connections)
		{
			if (!t.used)
			{
				t = cn;
				return Id::Create(index, Id::Type::Connection);
			}
			index++;
		}

		if (index < Id::MAX_ID)
		{
			m_connections.Add(cn);
			return Id::Create(index, Id::Type::Connection);
		}
	}

	return Id::Invalid();
}

bool Network::HttpDeleteConnection(Id conn_id)
{
	Core::LockGuard lock(m_mutex);

	if (HttpValidConnection(conn_id))
	{
		m_connections[conn_id.GetId()].used = false;

		return true;
	}

	return false;
}

Network::Id Network::HttpCreateRequestWithURL2(Id conn_id, const char* method, const char* url, uint64_t content_length)
{
	Core::LockGuard lock(m_mutex);

	if (HttpValidConnection(conn_id))
	{
		HttpRequest cn(m_connections[conn_id.GetId()]);
		cn.used   = true;
		cn.method = String::FromUtf8(method);
		cn.url    = String::FromUtf8(url);
		// cn.conn_id        = conn_id.ToInt();
		cn.content_length = content_length;

		int index = 0;
		for (auto& t: m_requests)
		{
			if (!t.used)
			{
				t = cn;
				return Id::Create(index, Id::Type::Request);
			}
			index++;
		}

		if (index < Id::MAX_ID)
		{
			m_requests.Add(cn);
			return Id::Create(index, Id::Type::Request);
		}
	}

	return Id::Invalid();
}

bool Network::HttpDeleteRequest(Id req_id)
{
	Core::LockGuard lock(m_mutex);

	if (HttpValidRequest(req_id))
	{
		m_requests[req_id.GetId()].used = false;

		return true;
	}

	return false;
}

bool Network::HttpDeleteTemplate(Id tmpl_id)
{
	Core::LockGuard lock(m_mutex);

	if (HttpValidTemplate(tmpl_id))
	{
		m_templates[tmpl_id.GetId()].used = false;

		return true;
	}

	return false;
}

// Host-side transport for the SceHttp HLE: each sceHttpSendRequest is one
// synchronous libcurl transfer, buffered in full on the HttpRequest object.
namespace HttpTransfer {

struct Config
{
	std::string              url;
	std::string              method;
	std::string              user_agent;
	std::vector<std::string> headers;
	std::vector<uint8_t>     post_body;
	int                      http_ver             = 0;
	bool                     follow_redirect      = true;
	bool                     skip_verify          = false;
	uint32_t                 connect_timeout_usec = 0;
	uint32_t                 recv_timeout_usec    = 0;
};

struct Result
{
	long                 status         = 0;
	int64_t              content_length = -1;
	bool                 chunked        = false;
	std::vector<uint8_t> body;
};

static size_t WriteCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	auto*  body = static_cast<std::vector<uint8_t>*>(userdata);
	size_t n    = size * nmemb;
	body->insert(body->end(), ptr, ptr + n);
	return n;
}

static size_t HeaderCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	auto*       chunked = static_cast<bool*>(userdata);
	size_t      n       = size * nmemb;
	std::string line(ptr, n);
	for (auto& c: line)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	if (line.rfind("http/", 0) == 0)
	{
		// a new response starts (e.g. after a redirect) - only the final one counts
		*chunked = false;
	}
	if (line.find("transfer-encoding:") != std::string::npos && line.find("chunked") != std::string::npos)
	{
		*chunked = true;
	}
	return n;
}

static int MapError(CURLcode code)
{
	switch (code)
	{
		case CURLE_OPERATION_TIMEDOUT: return HTTP_ERROR_TIMEOUT;
		case CURLE_UNSUPPORTED_PROTOCOL: return HTTP_ERROR_UNKNOWN_SCHEME;
		case CURLE_URL_MALFORMAT: return HTTP_ERROR_INVALID_URL;
		case CURLE_SSL_CONNECT_ERROR:
		case CURLE_PEER_FAILED_VERIFICATION:
		case CURLE_SSL_CERTPROBLEM:
		case CURLE_SSL_CIPHER:
		case CURLE_SSL_ISSUER_ERROR: return HTTP_ERROR_SSL;
		default: return HTTP_ERROR_NETWORK;
	}
}

static int Perform(const Config& cfg, Result* out)
{
	CURL* curl = curl_easy_init();
	if (curl == nullptr)
	{
		return HTTP_ERROR_OUT_OF_MEMORY;
	}

	curl_slist* header_list = nullptr;
	for (const auto& h: cfg.headers)
	{
		header_list = curl_slist_append(header_list, h.c_str());
	}

	char errbuf[CURL_ERROR_SIZE] = {};

	curl_easy_setopt(curl, CURLOPT_URL, cfg.url.c_str());
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out->body);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCb);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &out->chunked);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, cfg.follow_redirect ? 1L : 0L);

	if (!cfg.user_agent.empty())
	{
		curl_easy_setopt(curl, CURLOPT_USERAGENT, cfg.user_agent.c_str());
	}
	if (header_list != nullptr)
	{
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
	}
	if (cfg.http_ver == 1)
	{
		curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);
	} else
	{
		// real sceHttp speaks HTTP/1.1 at most - never let curl negotiate h2
		curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	}
	if (cfg.connect_timeout_usec != 0)
	{
		// sub-millisecond values must not truncate to 0 (curl reads 0 as "default")
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::max<long>(1, static_cast<long>(cfg.connect_timeout_usec / 1000)));
	}
	if (cfg.recv_timeout_usec != 0)
	{
		// Sony's recv timeout limits a stalled read, not the whole transfer
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, std::max<long>(1, static_cast<long>(cfg.recv_timeout_usec / 1000000)));
	}
	if (cfg.skip_verify)
	{
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}
	if (cfg.method == "POST")
	{
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(cfg.post_body.size()));
		curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS,
		                 cfg.post_body.empty() ? "" : reinterpret_cast<const char*>(cfg.post_body.data()));
	} else if (cfg.method == "HEAD")
	{
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	} else if (!cfg.method.empty() && cfg.method != "GET")
	{
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cfg.method.c_str());
	}

	CURLcode rc = curl_easy_perform(curl);

	if (rc == CURLE_OK)
	{
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		out->status = status;

		curl_off_t len = -1;
		curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
		out->content_length = static_cast<int64_t>(len);
	} else
	{
		printf("\t curl: %s (%s)\n", curl_easy_strerror(rc), errbuf);
	}

	curl_slist_free_all(header_list);
	curl_easy_cleanup(curl);

	return (rc == CURLE_OK ? OK : MapError(rc));
}

} // namespace HttpTransfer

int Network::HttpSendRequest(Id req_id, const void* post_data, size_t post_size)
{
	HttpTransfer::Config cfg;
	HttpsCallback        cbfunc      = nullptr;
	void*                cb_user_arg = nullptr;
	int                  cb_ssl_id   = 0;

	{
		Core::LockGuard lock(m_mutex);

		if (!HttpValidRequest(req_id))
		{
			return HTTP_ERROR_INVALID_ID;
		}

		const auto& r = m_requests.At(req_id.GetId());

		cfg.url                  = r.url.C_Str();
		cfg.method               = r.method.C_Str();
		cfg.user_agent           = r.user_agent.C_Str();
		cfg.http_ver             = r.http_ver;
		cfg.follow_redirect      = r.auto_redirect;
		cfg.connect_timeout_usec = r.connect_timeout;
		cfg.recv_timeout_usec    = r.recv_timeout;

		for (const auto& h: r.headers)
		{
			cfg.headers.emplace_back(std::string(h.name.C_Str()) + ": " + h.value.C_Str());
		}

		if (post_data != nullptr && post_size > 0)
		{
			const auto* p = static_cast<const uint8_t*>(post_data);
			cfg.post_body.assign(p, p + post_size);
		}

		if (r.nonblock)
		{
			printf("\t nonblock is not supported - performing synchronously\n");
		}

		cbfunc      = r.ssl_cbfunc;
		cb_user_arg = r.ssl_user_arg;
		if (r.http_ctx_id >= 0 && r.http_ctx_id < HTTP_MAX)
		{
			cb_ssl_id = Id::Create(m_http[r.http_ctx_id].ssl_ctx_id, Id::Type::Ssl).ToInt();
		}
	}

	bool is_https = (cfg.url.size() >= 5 && strncasecmp(cfg.url.c_str(), "https", 5) == 0);

	HttpTransfer::Result res;

	int result = HttpTransfer::Perform(cfg, &res);

	// Sony consults the guest's SSL callback during the TLS handshake; libcurl
	// exposes neither that hook nor the cert chain, so the callback gets an empty
	// cert list (certNum = 0) and a coarse verifyErr. A negative return rejects.
	// Verification stays ON by default; it is bypassed only when it actually
	// failed AND the guest's callback explicitly accepted the peer.
	if (is_https && cbfunc != nullptr)
	{
		if (result == HTTP_ERROR_SSL)
		{
			int cb_ret = cbfunc(cb_ssl_id, 0x02 /* generic verify-failure bit */, nullptr, 0, cb_user_arg);
			if (cb_ret < 0)
			{
				return HTTP_ERROR_SSL;
			}

			cfg.skip_verify = true;
			res             = HttpTransfer::Result();
			result          = HttpTransfer::Perform(cfg, &res);
		} else if (result == OK)
		{
			int cb_ret = cbfunc(cb_ssl_id, 0, nullptr, 0, cb_user_arg);
			if (cb_ret < 0)
			{
				return HTTP_ERROR_SSL;
			}
		}
	}

	if (result != OK)
	{
		return result;
	}

	if (res.body.size() > UINT32_MAX)
	{
		return HTTP_ERROR_OUT_OF_MEMORY;
	}

	Core::LockGuard lock(m_mutex);

	if (!HttpValidRequest(req_id))
	{
		// deleted while the transfer was in flight
		return HTTP_ERROR_INVALID_ID;
	}

	auto& r = m_requests[req_id.GetId()];

	r.performed   = true;
	r.status_code = static_cast<int>(res.status);
	r.read_pos    = 0;
	if (res.content_length >= 0)
	{
		r.response_length_type = HTTP_CONTENTLEN_EXIST;
		r.response_length      = static_cast<uint64_t>(res.content_length);
	} else
	{
		r.response_length_type = (res.chunked ? HTTP_CONTENTLEN_CHUNK_ENC : HTTP_CONTENTLEN_NOT_FOUND);
		r.response_length      = 0;
	}
	r.body = (res.body.empty() ? Core::ByteBuffer() : Core::ByteBuffer(res.body.data(), static_cast<uint32_t>(res.body.size())));

	printf("\t http status = %d, body = %u bytes\n", r.status_code, r.body.Size());

	return OK;
}

int Network::HttpGetStatusCode(Id req_id, int* status_code)
{
	Core::LockGuard lock(m_mutex);

	if (!HttpValidRequest(req_id))
	{
		return HTTP_ERROR_INVALID_ID;
	}
	if (status_code == nullptr)
	{
		return HTTP_ERROR_INVALID_VALUE;
	}

	const auto& r = m_requests.At(req_id.GetId());

	if (!r.performed)
	{
		return HTTP_ERROR_BEFORE_SEND;
	}

	*status_code = r.status_code;

	return OK;
}

int Network::HttpGetResponseContentLength(Id req_id, int* result, uint64_t* content_length)
{
	Core::LockGuard lock(m_mutex);

	if (!HttpValidRequest(req_id))
	{
		return HTTP_ERROR_INVALID_ID;
	}
	if (result == nullptr || content_length == nullptr)
	{
		return HTTP_ERROR_INVALID_VALUE;
	}

	const auto& r = m_requests.At(req_id.GetId());

	if (!r.performed)
	{
		return HTTP_ERROR_BEFORE_SEND;
	}

	*result         = r.response_length_type;
	*content_length = r.response_length;

	return OK;
}

int Network::HttpReadData(Id req_id, void* data, size_t size)
{
	Core::LockGuard lock(m_mutex);

	if (!HttpValidRequest(req_id))
	{
		return HTTP_ERROR_INVALID_ID;
	}
	if (data == nullptr)
	{
		return HTTP_ERROR_INVALID_VALUE;
	}

	auto& r = m_requests[req_id.GetId()];

	if (!r.performed)
	{
		return HTTP_ERROR_BEFORE_SEND;
	}
	if (std::strcmp(r.method.C_Str(), "HEAD") == 0)
	{
		return HTTP_ERROR_READ_BY_HEAD_METHOD;
	}

	uint64_t body_size = r.body.Size();
	uint64_t avail     = (r.read_pos < body_size ? body_size - r.read_pos : 0);
	uint64_t n         = std::min<uint64_t>(std::min<uint64_t>(avail, size), INT32_MAX);

	if (n > 0)
	{
		std::memcpy(data, r.body.GetDataConst() + r.read_pos, n);
		r.read_pos += n;
	}

	return static_cast<int>(n);
}

bool Network::HttpSetNonblock(Id id, bool enable)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	} else if (HttpValidRequest(id))
	{
		base = &m_requests[id.GetId()];
	}

	if (base != nullptr)
	{
		base->nonblock = enable;
		return true;
	}

	return false;
}

bool Network::HttpsSetSslCallback(Id id, HttpsCallback cbfunc, void* user_arg)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	} else if (HttpValidRequest(id))
	{
		base = &m_requests[id.GetId()];
	}

	if (base != nullptr)
	{
		base->ssl_cbfunc   = cbfunc;
		base->ssl_user_arg = user_arg;
		return true;
	}

	return false;
}

bool Network::HttpsDisableOption(Id id, uint32_t ssl_flags)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	} else if (HttpValidRequest(id))
	{
		base = &m_requests[id.GetId()];
	}

	if (base != nullptr)
	{
		base->ssl_flags &= ~ssl_flags;
		return true;
	}

	return false;
}

bool Network::HttpAddRequestHeader(Id id, const char* name, const char* value, bool add)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	} else if (HttpValidRequest(id))
	{
		base = &m_requests[id.GetId()];
	}

	if (base != nullptr)
	{
		HttpHeader nh({String::FromUtf8(name), String::FromUtf8(value)});
		if (add)
		{
			base->headers.Add(nh);
		} else
		{
			for (auto& h: base->headers)
			{
				if (h.name == nh.name)
				{
					h.value = nh.value;
				}
			}
		}
		return true;
	}

	return false;
}

bool Network::HttpSetResolveTimeOut(Id id, uint32_t usec)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	}

	if (base != nullptr)
	{
		base->resolve_timeout = usec;
		return true;
	}

	return false;
}

bool Network::HttpSetResolveRetry(Id id, int32_t retry)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	}

	if (base != nullptr)
	{
		base->resolve_retry = retry;
		return true;
	}

	return false;
}

bool Network::HttpSetConnectTimeOut(Id id, uint32_t usec)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	} else if (HttpValidRequest(id))
	{
		base = &m_requests[id.GetId()];
	}

	if (base != nullptr)
	{
		base->connect_timeout = usec;
		return true;
	}

	return false;
}

bool Network::HttpSetSendTimeOut(Id id, uint32_t usec)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	} else if (HttpValidRequest(id))
	{
		base = &m_requests[id.GetId()];
	}

	if (base != nullptr)
	{
		base->send_timeout = usec;
		return true;
	}

	return false;
}

bool Network::HttpSetRecvTimeOut(Id id, uint32_t usec)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	} else if (HttpValidRequest(id))
	{
		base = &m_requests[id.GetId()];
	}

	if (base != nullptr)
	{
		base->recv_timeout = usec;
		return true;
	}

	return false;
}

bool Network::HttpSetAutoRedirect(Id id, int enable)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	} else if (HttpValidRequest(id))
	{
		base = &m_requests[id.GetId()];
	}

	if (base != nullptr)
	{
		base->auto_redirect = (enable != 0);
		return true;
	}

	return false;
}

bool Network::HttpSetAuthEnabled(Id id, int enable)
{
	Core::LockGuard lock(m_mutex);

	HttpBase* base = nullptr;

	if (HttpValidTemplate(id))
	{
		base = &m_templates[id.GetId()];
	} else if (HttpValidConnection(id))
	{
		base = &m_connections[id.GetId()];
	} else if (HttpValidRequest(id))
	{
		base = &m_requests[id.GetId()];
	}

	if (base != nullptr)
	{
		base->auth_enabled = (enable != 0);
		return true;
	}

	return false;
}

namespace Net {

LIB_NAME("Net", "Net");

struct NetEtherAddr
{
	uint8_t data[6] = {0};
};

int KYTY_SYSV_ABI NetInit()
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI NetTerm()
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI NetPoolCreate(const char* name, int size, int flags)
{
	PRINT_NAME();

	printf("\t name = %s\n", name);
	printf("\t size = %d\n", size);
	printf("\t flags = %d\n", flags);

	EXIT_IF(g_net == nullptr);

	EXIT_NOT_IMPLEMENTED(flags != 0);
	EXIT_NOT_IMPLEMENTED(size == 0);

	int id = g_net->PoolCreate(name, size);

	if (id < 0)
	{
		return NET_ERROR_ENFILE;
	}

	return id;
}

int KYTY_SYSV_ABI NetPoolDestroy(int memid)
{
	PRINT_NAME();

	EXIT_IF(g_net == nullptr);

	if (!g_net->PoolDestroy(memid))
	{
		return NET_ERROR_EBADF;
	}

	return OK;
}

int KYTY_SYSV_ABI NetInetPton(int af, const char* src, void* dst)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(af != 2);
	EXIT_NOT_IMPLEMENTED(src == nullptr);
	EXIT_NOT_IMPLEMENTED(dst == nullptr);

	printf("\t src = %.16s\n", src);

	// network byte order, like the real sceNetInetPton (the old hard-coded
	// 127.0.0.1 wrote host order, which nothing could round-trip)
	int res = inet_pton(AF_INET, src, dst);
	if (res != 1)
	{
		return NET_ERROR_EINVAL;
	}

	return 1;
}

// --- BSD-socket bridge -------------------------------------------------------
// sceNet* over host sockets, IPv4 only. Socket ids are host fds; error returns
// are SCE net codes: 0x804101xx with FreeBSD errno numbering (which differs
// from the Linux host's - see the translation below).

static int errno_to_sce(int host_errno)
{
	switch (host_errno)
	{
		case EINTR: return NET_ERROR_EINTR;
		case EBADF: return NET_ERROR_EBADF;
		case ENOMEM: return NET_ERROR_ENOMEM;
		case EACCES: return NET_ERROR_EACCES;
		case EFAULT: return NET_ERROR_EFAULT;
		case EINVAL: return NET_ERROR_EINVAL;
		case EPIPE: return NET_ERROR_EPIPE;
		case EAGAIN: return NET_ERROR_EAGAIN;
		case EINPROGRESS: return NET_ERROR_EINPROGRESS;
		case EALREADY: return NET_ERROR_EALREADY;
		case EADDRINUSE: return NET_ERROR_EADDRINUSE;
		case ENETUNREACH: return NET_ERROR_ENETUNREACH;
		case ECONNRESET: return NET_ERROR_ECONNRESET;
		case EISCONN: return NET_ERROR_EISCONN;
		case ENOTCONN: return NET_ERROR_ENOTCONN;
		case ETIMEDOUT: return static_cast<int>(0x8041013CU);    // FreeBSD ETIMEDOUT = 60
		case ECONNREFUSED: return static_cast<int>(0x8041013DU); // FreeBSD ECONNREFUSED = 61
		case EHOSTUNREACH: return static_cast<int>(0x80410141U); // FreeBSD EHOSTUNREACH = 65
		default: return NET_ERROR_EINVAL;
	}
}

// Guest sockaddr_in (FreeBSD-style): len, family, port (BE), addr (BE), zero[8]
struct NetSockaddrIn
{
	uint8_t  sin_len;
	uint8_t  sin_family;
	uint16_t sin_port;
	uint32_t sin_addr;
	uint8_t  sin_zero[8];
};
static_assert(sizeof(NetSockaddrIn) == 16, "guest sockaddr_in ABI");

int KYTY_SYSV_ABI NetSocket(const char* name, int family, int type, int protocol)
{
	PRINT_NAME();

	printf("\t name = %s, family = %d, type = %d, protocol = %d\n", (name != nullptr ? name : "(null)"), family, type, protocol);

	EXIT_NOT_IMPLEMENTED(family != 2);           // SCE_NET_AF_INET
	EXIT_NOT_IMPLEMENTED(type != 1 && type != 2); // SCE_NET_SOCK_STREAM / SOCK_DGRAM

	int fd = socket(AF_INET, (type == 1 ? SOCK_STREAM : SOCK_DGRAM), 0);
	if (fd < 0)
	{
		return errno_to_sce(errno);
	}

	return fd;
}

static void epoll_forget_socket(int sock);

int KYTY_SYSV_ABI NetSocketClose(int sock)
{
	PRINT_NAME();

	// real epoll drops a fd from every set on last close; without this, host
	// fd-number reuse would make stale registrations watch an unrelated socket
	epoll_forget_socket(sock);

	if (close(sock) < 0)
	{
		return errno_to_sce(errno);
	}

	return OK;
}

int KYTY_SYSV_ABI NetShutdown(int sock, int how)
{
	PRINT_NAME();

	// SCE_NET_SHUT_RD/WR/RDWR = 0/1/2, same as the host
	if (shutdown(sock, how) < 0)
	{
		return errno_to_sce(errno);
	}

	return OK;
}

// DNS resolver: a resolver id is just an opaque handle (there is no per-handle
// host state - getaddrinfo runs synchronously in StartNtoa).
static std::atomic<int> g_resolver_next {1};

// SCE_NET_RESOLVER_ERROR_NO_RECORD
static constexpr int NET_RESOLVER_ERROR_NO_RECORD = static_cast<int>(0x804101A1U);

int KYTY_SYSV_ABI NetResolverCreate(const char* name, int memid, int flags)
{
	PRINT_NAME();

	printf("\t name = %s\n", (name != nullptr ? name : "(null)"));

	EXIT_NOT_IMPLEMENTED(flags != 0);

	// memid (the sceNet memory pool) is irrelevant to a getaddrinfo bridge
	(void)memid;

	return g_resolver_next.fetch_add(1);
}

int KYTY_SYSV_ABI NetResolverStartNtoa(int rid, const char* hostname, uint32_t* addr, int timeout, int retry, int flags)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(hostname == nullptr || addr == nullptr);
	EXIT_NOT_IMPLEMENTED(flags != 0);

	(void)rid;
	// LIMITATION: the guest's timeout/retry are not honored - getaddrinfo has
	// no timeout parameter and runs synchronously on the caller (the guest's
	// Http/Server worker thread, so it never blocks the main loop), and the
	// no-op NetResolverAbort cannot interrupt it. Bounding it via a detached
	// helper thread was tried and rejected: a lookup still in flight at process
	// exit (e.g. ClassiCube's heartbeat host on a real DNS server) crashes
	// during static teardown. getaddrinfo's own resolver timeout is the bound.
	(void)timeout;
	(void)retry;

	printf("\t hostname = %s\n", hostname);

	addrinfo hints {};
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	addrinfo* res = nullptr;
	int       rc  = getaddrinfo(hostname, nullptr, &hints, &res);
	if (rc != 0)
	{
		// getaddrinfo leaves *res undefined on error - do not free it
		return NET_RESOLVER_ERROR_NO_RECORD;
	}
	if (res == nullptr)
	{
		return NET_RESOLVER_ERROR_NO_RECORD;
	}

	*addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr; // network order
	freeaddrinfo(res);

	printf("\t resolved to 0x%08x\n", *addr);

	return OK;
}

int KYTY_SYSV_ABI NetResolverDestroy(int rid)
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI NetResolverAbort(int rid, int flags)
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI NetConnect(int sock, const void* addr, uint32_t addrlen)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(addr == nullptr);
	EXIT_NOT_IMPLEMENTED(addrlen < sizeof(NetSockaddrIn));

	const auto* in = static_cast<const NetSockaddrIn*>(addr);

	EXIT_NOT_IMPLEMENTED(in->sin_family != 2);

	sockaddr_in host {};
	host.sin_family      = AF_INET;
	host.sin_port        = in->sin_port; // already network byte order
	host.sin_addr.s_addr = in->sin_addr;

	printf("\t connect to %s:%u\n", inet_ntoa(host.sin_addr), ntohs(host.sin_port));

	if (connect(sock, reinterpret_cast<sockaddr*>(&host), sizeof(host)) < 0)
	{
		return errno_to_sce(errno);
	}

	return OK;
}

int KYTY_SYSV_ABI NetSend(int sock, const void* buf, size_t len, int flags)
{
	EXIT_NOT_IMPLEMENTED(flags != 0);

	auto sent = send(sock, buf, len, MSG_NOSIGNAL);
	if (sent < 0)
	{
		return errno_to_sce(errno);
	}

	return static_cast<int>(sent);
}

// --- POSIX socket-server bridge -------------------------------------------
//
// The sceNet* functions above are the SCE HLE (SCE error codes, guest calls
// them by their sce* names). OpenOrbis titles can ALSO use the raw POSIX
// socket API imported from the "libkernel" library (socket via __sys_socketex,
// plus bind/listen/accept). Those follow POSIX semantics: return an fd / 0 on
// success, -1 on error with the thread's errno cell set. This bridge provides
// the server side (bind/listen/accept) the sceNet layer never had. Ids are raw
// host fds, shared with the sceNet functions above.

int HostErrnoToPosix(int host_errno)
{
	switch (host_errno)
	{
		case EBADF: return Posix::POSIX_EBADF;
		case EINVAL: return Posix::POSIX_EINVAL;
		case EACCES: return Posix::POSIX_EACCES;
		case EFAULT: return Posix::POSIX_EFAULT;
		case EAGAIN: return Posix::POSIX_EAGAIN;
		case EMFILE: return Posix::POSIX_EMFILE;
		case ENFILE: return Posix::POSIX_ENFILE;
		case ENOTSOCK: return Posix::POSIX_ENOTSOCK;
		case EOPNOTSUPP: return Posix::POSIX_EOPNOTSUPP;
		case EAFNOSUPPORT: return Posix::POSIX_EAFNOSUPPORT;
		case EPROTONOSUPPORT: return Posix::POSIX_EPROTONOSUPPORT;
		case EADDRINUSE: return Posix::POSIX_EADDRINUSE;
		case EADDRNOTAVAIL: return Posix::POSIX_EADDRNOTAVAIL;
		case ECONNRESET: return Posix::POSIX_ECONNRESET;
		case ENOTCONN: return Posix::POSIX_ENOTCONN;
		case ECONNREFUSED: return Posix::POSIX_ECONNREFUSED;
		default: return Posix::POSIX_EINVAL;
	}
}

// True if fd is a host socket we handed to the guest (so POSIX read/write/close
// on it must go to the host socket, not the file-descriptor registry).
bool IsHostSocket(int fd)
{
	if (fd < 0)
	{
		return false;
	}
	int       type = 0;
	socklen_t len  = sizeof(type);
	return getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0;
}

// POSIX close() of a guest socket. Mirrors NetSocketClose: purge the fd from any
// sceNet epoll set first (else host fd-number reuse leaves a stale registration
// watching an unrelated socket). Returns 0, or a POSIX errno on failure.
int HostSocketClose(int fd)
{
	epoll_forget_socket(fd);

	if (close(fd) < 0)
	{
		return HostErrnoToPosix(errno);
	}

	return 0;
}

// POSIX dup() of a guest socket: the new host fd needs no registration (IsHostSocket
// is a live getsockopt probe). Returns the new fd, or -posix_errno on failure.
int HostSocketDup(int fd)
{
	int nfd = dup(fd);
	return (nfd < 0 ? -HostErrnoToPosix(errno) : nfd);
}

// POSIX dup2(oldfd, newfd) between guest sockets: purge newfd from any epoll set
// first (the sceNet epoll table is keyed by fd number). Returns 0, or -posix_errno.
int HostSocketDup2(int oldfd, int newfd)
{
	epoll_forget_socket(newfd);

	if (dup2(oldfd, newfd) < 0)
	{
		return -HostErrnoToPosix(errno);
	}

	return 0;
}

int KYTY_SYSV_ABI NetSysSocket(const char* name, int domain, int type, int protocol)
{
	PRINT_NAME();

	printf("\t name = %s, domain = %d, type = %d, protocol = %d\n", (name != nullptr ? name : "(null)"), domain, type, protocol);

	// AF_INET == 2 and SOCK_STREAM == 1 / SOCK_DGRAM == 2 on both the guest
	// (FreeBSD ABI) and the Linux host, so the constants pass through directly.
	if (domain != AF_INET)
	{
		*Posix::GetErrorAddr() = Posix::POSIX_EAFNOSUPPORT;
		return -1;
	}
	if (type != SOCK_STREAM && type != SOCK_DGRAM)
	{
		*Posix::GetErrorAddr() = Posix::POSIX_EPROTONOSUPPORT;
		return -1;
	}

	int fd = socket(AF_INET, type, 0);
	if (fd < 0)
	{
		*Posix::GetErrorAddr() = HostErrnoToPosix(errno);
		return -1;
	}

	(void)protocol;
	return fd;
}

int KYTY_SYSV_ABI NetBind(int sock, const void* addr, uint32_t addrlen)
{
	PRINT_NAME();

	if (addr == nullptr || addrlen < sizeof(NetSockaddrIn))
	{
		*Posix::GetErrorAddr() = Posix::POSIX_EINVAL;
		return -1;
	}

	const auto* in = static_cast<const NetSockaddrIn*>(addr);

	if (in->sin_family != AF_INET)
	{
		*Posix::GetErrorAddr() = Posix::POSIX_EAFNOSUPPORT;
		return -1;
	}

	sockaddr_in host {};
	host.sin_family      = AF_INET;
	host.sin_port        = in->sin_port; // already network byte order
	host.sin_addr.s_addr = in->sin_addr;

	printf("\t bind to %s:%u\n", inet_ntoa(host.sin_addr), ntohs(host.sin_port));

	if (bind(sock, reinterpret_cast<sockaddr*>(&host), sizeof(host)) < 0)
	{
		*Posix::GetErrorAddr() = HostErrnoToPosix(errno);
		return -1;
	}

	return OK;
}

int KYTY_SYSV_ABI NetListen(int sock, int backlog)
{
	PRINT_NAME();

	printf("\t sock = %d, backlog = %d\n", sock, backlog);

	if (listen(sock, backlog) < 0)
	{
		*Posix::GetErrorAddr() = HostErrnoToPosix(errno);
		return -1;
	}

	return OK;
}

int KYTY_SYSV_ABI NetAccept(int sock, void* addr, uint32_t* addrlen)
{
	PRINT_NAME();

	sockaddr_in host {};
	socklen_t   host_len = sizeof(host);

	int fd = accept(sock, reinterpret_cast<sockaddr*>(&host), &host_len);
	if (fd < 0)
	{
		*Posix::GetErrorAddr() = HostErrnoToPosix(errno);
		return -1;
	}

	// translate the host peer address back into the guest FreeBSD layout
	if (addr != nullptr && addrlen != nullptr && *addrlen >= sizeof(NetSockaddrIn))
	{
		auto* out          = static_cast<NetSockaddrIn*>(addr);
		out->sin_len       = sizeof(NetSockaddrIn);
		out->sin_family    = AF_INET;
		out->sin_port      = host.sin_port;        // network byte order
		out->sin_addr      = host.sin_addr.s_addr; // network byte order
		memset(out->sin_zero, 0, sizeof(out->sin_zero));
		*addrlen = sizeof(NetSockaddrIn);
	}

	printf("\t accepted %s:%u -> fd %d\n", inet_ntoa(host.sin_addr), ntohs(host.sin_port), fd);

	return fd;
}

int KYTY_SYSV_ABI NetRecv(int sock, void* buf, size_t len, int flags)
{
	EXIT_NOT_IMPLEMENTED(flags != 0);

	auto got = recv(sock, buf, len, 0);
	if (got < 0)
	{
		return errno_to_sce(errno);
	}

	return static_cast<int>(got);
}

int KYTY_SYSV_ABI NetSetsockopt(int sock, int level, int optname, const void* optval, uint32_t optlen)
{
	PRINT_NAME();

	printf("\t sock = %d, level = 0x%x, optname = 0x%x\n", sock, static_cast<unsigned>(level), static_cast<unsigned>(optname));

	// SCE_NET_SOL_SOCKET / SCE_NET_SO_NBIO -> O_NONBLOCK
	if (level == 0xffff && optname == 0x1200)
	{
		EXIT_NOT_IMPLEMENTED(optval == nullptr || optlen < 4);
		int  nb    = *static_cast<const int*>(optval);
		int  fl    = fcntl(sock, F_GETFL, 0);
		int  newfl = (nb != 0 ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
		if (fl < 0 || fcntl(sock, F_SETFL, newfl) < 0)
		{
			return errno_to_sce(errno);
		}
		return OK;
	}

	// anything else is best-effort ignorable for now (buffer sizes etc.)
	printf("\t (ignored)\n");
	return OK;
}

uint16_t KYTY_SYSV_ABI NetHtons(uint16_t host16)
{
	return htons(host16);
}

// Guest-visible epoll event, matching SceNetEpollEvent (24 bytes)
struct NetEpollEvent
{
	uint32_t events;   // SCE_NET_EPOLLIN 0x1, EPOLLOUT 0x2, EPOLLERR 0x8, EPOLLHUP 0x10
	uint32_t reserved;
	int32_t  ident;
	uint32_t pad;
	uint64_t data;
};
static_assert(sizeof(NetEpollEvent) == 24, "guest epoll event ABI");

struct NetEpollEntry
{
	int      sock   = -1;
	uint32_t events = 0;
	uint64_t data   = 0;
};

struct NetEpoll
{
	bool                  used = false;
	Vector<NetEpollEntry> fds;
};

static Core::Mutex      g_epoll_mutex;
static Vector<NetEpoll> g_epolls;

static void epoll_forget_socket(int sock)
{
	Core::LockGuard lock(g_epoll_mutex);

	for (uint32_t e = 0; e < g_epolls.Size(); e++)
	{
		if (!g_epolls[e].used)
		{
			continue;
		}
		auto& fds = g_epolls[e].fds;
		for (uint32_t i = 0; i < fds.Size();)
		{
			if (fds[i].sock == sock)
			{
				fds.RemoveAt(i);
			} else
			{
				i++;
			}
		}
	}
}

int KYTY_SYSV_ABI NetEpollCreate(const char* name, int flags)
{
	PRINT_NAME();

	printf("\t name = %s\n", (name != nullptr ? name : "(null)"));

	EXIT_NOT_IMPLEMENTED(flags != 0);

	Core::LockGuard lock(g_epoll_mutex);

	for (uint32_t i = 0; i < g_epolls.Size(); i++)
	{
		if (!g_epolls[i].used)
		{
			g_epolls[i].used = true;
			g_epolls[i].fds.Clear();
			return static_cast<int>(i);
		}
	}

	NetEpoll e;
	e.used = true;
	g_epolls.Add(e);
	return static_cast<int>(g_epolls.Size()) - 1;
}

int KYTY_SYSV_ABI NetEpollControl(int eid, int op, int sock, const void* event)
{
	PRINT_NAME();

	Core::LockGuard lock(g_epoll_mutex);

	if (eid < 0 || static_cast<uint32_t>(eid) >= g_epolls.Size() || !g_epolls[eid].used)
	{
		return NET_ERROR_EBADF;
	}

	auto& fds = g_epolls[eid].fds;

	int index = -1;
	for (uint32_t i = 0; i < fds.Size(); i++)
	{
		if (fds[i].sock == sock)
		{
			index = static_cast<int>(i);
			break;
		}
	}

	switch (op)
	{
		case 1: // SCE_NET_EPOLL_CTL_ADD
		case 2: // SCE_NET_EPOLL_CTL_MOD
		{
			EXIT_NOT_IMPLEMENTED(event == nullptr);
			const auto* ev = static_cast<const NetEpollEvent*>(event);
			if (index < 0)
			{
				NetEpollEntry entry;
				entry.sock = sock;
				fds.Add(entry);
				index = static_cast<int>(fds.Size()) - 1;
			}
			fds[index].events = ev->events;
			fds[index].data   = ev->data;
			return OK;
		}
		case 3: // SCE_NET_EPOLL_CTL_DEL
			if (index >= 0)
			{
				fds.RemoveAt(index);
			}
			return OK;
		default: return NET_ERROR_EINVAL;
	}
}

int KYTY_SYSV_ABI NetEpollWait(int eid, void* events, int maxevents, int timeout_usec)
{
	Vector<NetEpollEntry> fds;
	{
		Core::LockGuard lock(g_epoll_mutex);

		if (eid < 0 || static_cast<uint32_t>(eid) >= g_epolls.Size() || !g_epolls[eid].used)
		{
			return NET_ERROR_EBADF;
		}
		fds = g_epolls[eid].fds;
	}

	EXIT_NOT_IMPLEMENTED(events == nullptr || maxevents < 1);

	Vector<pollfd> host;
	auto           num = static_cast<int>(fds.Size());
	for (int i = 0; i < num; i++)
	{
		pollfd p {};
		p.fd     = fds[i].sock;
		p.events = static_cast<short>(((fds[i].events & 0x1u) != 0 ? POLLIN : 0) | ((fds[i].events & 0x2u) != 0 ? POLLOUT : 0));
		host.Add(p);
	}

	// 64-bit intermediate: timeout_usec near INT_MAX must not overflow
	int timeout_ms = (timeout_usec < 0 ? -1 : static_cast<int>((static_cast<int64_t>(timeout_usec) + 999) / 1000));

	int ready = poll(host.GetData(), num, timeout_ms);
	if (ready < 0)
	{
		return errno_to_sce(errno);
	}

	auto* out = static_cast<NetEpollEvent*>(events);
	int   n   = 0;
	for (int i = 0; i < num && n < maxevents; i++)
	{
		if (host[i].revents == 0)
		{
			continue;
		}
		out[n]        = NetEpollEvent {};
		out[n].events = ((host[i].revents & POLLIN) != 0 ? 0x1u : 0) | ((host[i].revents & POLLOUT) != 0 ? 0x2u : 0) |
		                ((host[i].revents & POLLERR) != 0 ? 0x8u : 0) | ((host[i].revents & POLLHUP) != 0 ? 0x10u : 0);
		out[n].ident = fds[i].sock;
		out[n].data  = fds[i].data;
		n++;
	}

	return n;
}

int KYTY_SYSV_ABI NetEpollDestroy(int eid)
{
	PRINT_NAME();

	Core::LockGuard lock(g_epoll_mutex);

	if (eid < 0 || static_cast<uint32_t>(eid) >= g_epolls.Size() || !g_epolls[eid].used)
	{
		return NET_ERROR_EBADF;
	}

	g_epolls[eid].used = false;
	g_epolls[eid].fds.Clear();

	return OK;
}

int KYTY_SYSV_ABI NetEtherNtostr(const NetEtherAddr* n, char* str, size_t len)
{
	PRINT_NAME();

	NetEtherAddr zero {};

	EXIT_NOT_IMPLEMENTED(len != 18);
	EXIT_NOT_IMPLEMENTED(n == nullptr);
	EXIT_NOT_IMPLEMENTED(str == nullptr);
	EXIT_NOT_IMPLEMENTED(memcmp(n->data, zero.data, sizeof(zero.data)) != 0);

	strcpy(str, "00:00:00:00:00:00"); // NOLINT

	return OK;
}

int KYTY_SYSV_ABI NetGetMacAddress(NetEtherAddr* addr, int flags)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(addr == nullptr);
	EXIT_NOT_IMPLEMENTED(flags != 0);

	memset(addr->data, 0, sizeof(addr->data));

	return OK;
}

} // namespace Net

namespace Ssl {

LIB_NAME("Ssl", "Ssl");

int KYTY_SYSV_ABI SslInit(uint64_t pool_size)
{
	PRINT_NAME();

	printf("\t size = %" PRIu64 "\n", pool_size);

	EXIT_IF(g_net == nullptr);

	EXIT_NOT_IMPLEMENTED(pool_size == 0);

	auto id = g_net->SslInit(pool_size);

	if (!id.IsValid())
	{
		return SSL_ERROR_OUT_OF_SIZE;
	}

	return id.ToInt();
}

int KYTY_SYSV_ABI SslTerm(int ssl_ctx_id)
{
	PRINT_NAME();

	EXIT_IF(g_net == nullptr);

	if (!g_net->SslTerm(Network::Id(ssl_ctx_id)))
	{
		return SSL_ERROR_INVALID_ID;
	}

	return OK;
}

} // namespace Ssl

namespace Http {

struct HttpEpoll
{
	Network::Id http_ctx_id = Network::Id(0);
	Network::Id request_id  = Network::Id(0);
	void*       user_arg    = nullptr;
};

LIB_NAME("Http", "Http");

int KYTY_SYSV_ABI HttpInit(int memid, int ssl_ctx_id, uint64_t pool_size)
{
	PRINT_NAME();

	printf("\t memid      = %d\n", memid);
	printf("\t ssl_ctx_id = %d\n", ssl_ctx_id);
	printf("\t size       = %" PRIu64 "\n", pool_size);

	EXIT_IF(g_net == nullptr);

	EXIT_NOT_IMPLEMENTED(pool_size == 0);

	auto id = g_net->HttpInit(memid, Network::Id(ssl_ctx_id), pool_size);

	if (!id.IsValid())
	{
		return HTTP_ERROR_OUT_OF_MEMORY;
	}

	return id.ToInt();
}

int KYTY_SYSV_ABI HttpTerm(int http_ctx_id)
{
	PRINT_NAME();

	EXIT_IF(g_net == nullptr);

	if (!g_net->HttpTerm(Network::Id(http_ctx_id)))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpCreateTemplate(int http_ctx_id, const char* user_agent, int http_ver, int is_auto_proxy_conf)
{
	PRINT_NAME();

	printf("\t http_ctx_id        = %d\n", http_ctx_id);
	printf("\t user_agent         = %s\n", user_agent);
	printf("\t http_ver           = %d\n", http_ver);
	printf("\t is_auto_proxy_conf = %d\n", is_auto_proxy_conf);

	EXIT_IF(g_net == nullptr);

	auto id = g_net->HttpCreateTemplate(Network::Id(http_ctx_id), user_agent, http_ver, is_auto_proxy_conf != 0);

	if (!id.IsValid())
	{
		return HTTP_ERROR_OUT_OF_MEMORY;
	}

	return id.ToInt();
}

int KYTY_SYSV_ABI HttpDeleteTemplate(int tmpl_id)
{
	PRINT_NAME();

	EXIT_IF(g_net == nullptr);

	if (!g_net->HttpDeleteTemplate(Network::Id(tmpl_id)))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpSetNonblock(int id, int enable)
{
	PRINT_NAME();

	printf("\t id     = %d\n", id);
	printf("\t enable = %d\n", enable);

	if (!g_net->HttpSetNonblock(Network::Id(id), enable != 0))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpsSetSslCallback(int id, HttpsCallback cbfunc, void* user_arg)
{
	PRINT_NAME();

	printf("\t id     = %d\n", id);

	if (!g_net->HttpsSetSslCallback(Network::Id(id), cbfunc, user_arg))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpsDisableOption(int id, uint32_t ssl_flags)
{
	PRINT_NAME();

	printf("\t id        = %d\n", id);
	printf("\t ssl_flags = %u\n", ssl_flags);

	if (!g_net->HttpsDisableOption(Network::Id(id), ssl_flags))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpSetResolveTimeOut(int id, uint32_t usec)
{
	PRINT_NAME();

	printf("\t id   = %d\n", id);
	printf("\t usec = %u\n", usec);

	if (!g_net->HttpSetResolveTimeOut(Network::Id(id), usec))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpSetResolveRetry(int id, int32_t retry)
{
	PRINT_NAME();

	printf("\t id    = %d\n", id);
	printf("\t retry = %d\n", retry);

	if (!g_net->HttpSetResolveRetry(Network::Id(id), retry))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpSetConnectTimeOut(int id, uint32_t usec)
{
	PRINT_NAME();

	printf("\t id   = %d\n", id);
	printf("\t usec = %u\n", usec);

	if (!g_net->HttpSetConnectTimeOut(Network::Id(id), usec))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpSetSendTimeOut(int id, uint32_t usec)
{
	PRINT_NAME();

	printf("\t id   = %d\n", id);
	printf("\t usec = %u\n", usec);

	if (!g_net->HttpSetSendTimeOut(Network::Id(id), usec))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpSetRecvTimeOut(int id, uint32_t usec)
{
	PRINT_NAME();

	printf("\t id   = %d\n", id);
	printf("\t usec = %u\n", usec);

	if (!g_net->HttpSetRecvTimeOut(Network::Id(id), usec))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpSetAutoRedirect(int id, int enable)
{
	PRINT_NAME();

	printf("\t id     = %d\n", id);
	printf("\t enable = %d\n", enable);

	if (!g_net->HttpSetAutoRedirect(Network::Id(id), enable))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpSetAuthEnabled(int id, int enable)
{
	PRINT_NAME();

	printf("\t id     = %d\n", id);
	printf("\t enable = %d\n", enable);

	if (!g_net->HttpSetAuthEnabled(Network::Id(id), enable))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpAddRequestHeader(int id, const char* name, const char* value, uint32_t mode)
{
	PRINT_NAME();

	printf("\t id    = %d\n", id);
	printf("\t name  = %s\n", name);
	printf("\t value = %s\n", value);
	printf("\t mode  = %u\n", mode);

	EXIT_NOT_IMPLEMENTED(mode != 0 && mode != 1);

	if (!g_net->HttpAddRequestHeader(Network::Id(id), name, value, mode == 1))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpCreateEpoll(int http_ctx_id, HttpEpollHandle* eh)
{
	PRINT_NAME();

	printf("\t http_ctx_id = %d\n", http_ctx_id);

	EXIT_IF(g_net == nullptr);

	EXIT_NOT_IMPLEMENTED(eh == nullptr);

	EXIT_NOT_IMPLEMENTED(!g_net->HttpValid(Network::Id(http_ctx_id)));

	*eh = new HttpEpoll;

	(*eh)->http_ctx_id = Network::Id(http_ctx_id);

	return OK;
}

int KYTY_SYSV_ABI HttpDestroyEpoll(int http_ctx_id, HttpEpollHandle eh)
{
	PRINT_NAME();

	printf("\t http_ctx_id = %d\n", http_ctx_id);

	EXIT_IF(g_net == nullptr);

	EXIT_NOT_IMPLEMENTED(eh == nullptr);

	EXIT_NOT_IMPLEMENTED(!g_net->HttpValid(Network::Id(http_ctx_id)));

	delete eh;

	return OK;
}

int KYTY_SYSV_ABI HttpSetEpoll(int id, HttpEpollHandle eh, void* user_arg)
{
	PRINT_NAME();

	printf("\t id = %d\n", id);

	EXIT_NOT_IMPLEMENTED(eh == nullptr);

	EXIT_NOT_IMPLEMENTED(!g_net->HttpValidRequest(Network::Id(id)));

	eh->request_id = Network::Id(id);
	eh->user_arg   = user_arg;

	return OK;
}

int KYTY_SYSV_ABI HttpUnsetEpoll(int id)
{
	PRINT_NAME();

	printf("\t id = %d\n", id);

	EXIT_NOT_IMPLEMENTED(!g_net->HttpValidRequest(Network::Id(id)));

	return OK;
}

int KYTY_SYSV_ABI HttpSendRequest(int request_id, const void* post_data, size_t size)
{
	PRINT_NAME();

	printf("\t request_id = %d\n", request_id);
	printf("\t post_size  = %" PRIu64 "\n", static_cast<uint64_t>(size));

	EXIT_IF(g_net == nullptr);

	return g_net->HttpSendRequest(Network::Id(request_id), post_data, size);
}

int KYTY_SYSV_ABI HttpCreateConnectionWithURL(int tmpl_id, const char* url, int enable_keep_alive)
{
	PRINT_NAME();

	printf("\t tmpl_id           = %d\n", tmpl_id);
	printf("\t url               = %s\n", url);
	printf("\t enable_keep_alive = %d\n", enable_keep_alive);

	EXIT_IF(g_net == nullptr);

	auto id = g_net->HttpCreateConnectionWithURL(Network::Id(tmpl_id), url, enable_keep_alive != 0);

	if (!id.IsValid())
	{
		return HTTP_ERROR_OUT_OF_MEMORY;
	}

	return id.ToInt();
}

int KYTY_SYSV_ABI HttpDeleteConnection(int conn_id)
{
	PRINT_NAME();

	printf("\t conn_id = %d\n", conn_id);

	EXIT_IF(g_net == nullptr);

	if (!g_net->HttpDeleteConnection(Network::Id(conn_id)))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpCreateRequestWithURL2(int conn_id, const char* method, const char* url, uint64_t content_length)
{
	PRINT_NAME();

	printf("\t conn_id        = %d\n", conn_id);
	printf("\t url            = %s\n", url);
	printf("\t method         = %s\n", method);
	printf("\t content_length = %" PRIu64 "\n", content_length);

	EXIT_IF(g_net == nullptr);

	auto id = g_net->HttpCreateRequestWithURL2(Network::Id(conn_id), method, url, content_length);

	if (!id.IsValid())
	{
		return HTTP_ERROR_OUT_OF_MEMORY;
	}

	return id.ToInt();
}

int KYTY_SYSV_ABI HttpCreateRequestWithURL(int conn_id, int method, const char* url, uint64_t content_length)
{
	PRINT_NAME();

	// SCE_HTTP_METHOD_* ordering (ORBIS_METHOD_* in OpenOrbis)
	static const char* method_names[] = {"GET", "POST", "HEAD", "OPTIONS", "PUT", "DELETE", "TRACE"};

	printf("\t conn_id        = %d\n", conn_id);
	printf("\t method         = %d\n", method);
	printf("\t url            = %s\n", url);
	printf("\t content_length = %" PRIu64 "\n", content_length);

	EXIT_IF(g_net == nullptr);

	if (method < 0 || method >= static_cast<int>(sizeof(method_names) / sizeof(method_names[0])))
	{
		return HTTP_ERROR_UNKNOWN_METHOD;
	}

	auto id = g_net->HttpCreateRequestWithURL2(Network::Id(conn_id), method_names[method], url, content_length);

	if (!id.IsValid())
	{
		return HTTP_ERROR_OUT_OF_MEMORY;
	}

	return id.ToInt();
}

int KYTY_SYSV_ABI HttpDeleteRequest(int req_id)
{
	PRINT_NAME();

	printf("\t req_id = %d\n", req_id);

	EXIT_IF(g_net == nullptr);

	if (!g_net->HttpDeleteRequest(Network::Id(req_id)))
	{
		return HTTP_ERROR_INVALID_ID;
	}

	return OK;
}

int KYTY_SYSV_ABI HttpGetStatusCode(int req_id, int* status_code)
{
	PRINT_NAME();

	printf("\t req_id = %d\n", req_id);

	EXIT_IF(g_net == nullptr);

	int result = g_net->HttpGetStatusCode(Network::Id(req_id), status_code);

	if (result == OK)
	{
		printf("\t status = %d\n", *status_code);
	}

	return result;
}

int KYTY_SYSV_ABI HttpGetResponseContentLength(int req_id, int* result, uint64_t* content_length)
{
	PRINT_NAME();

	printf("\t req_id = %d\n", req_id);

	EXIT_IF(g_net == nullptr);

	int ret = g_net->HttpGetResponseContentLength(Network::Id(req_id), result, content_length);

	if (ret == OK)
	{
		printf("\t type = %d, content_length = %" PRIu64 "\n", *result, *content_length);
	}

	return ret;
}

int KYTY_SYSV_ABI HttpReadData(int req_id, void* data, size_t size)
{
	PRINT_NAME();

	printf("\t req_id = %d, size = %" PRIu64 "\n", req_id, static_cast<uint64_t>(size));

	EXIT_IF(g_net == nullptr);

	return g_net->HttpReadData(Network::Id(req_id), data, size);
}

} // namespace Http

namespace NetCtl {

LIB_NAME("NetCtl", "NetCtl");

struct NetInAddr
{
	uint32_t s_addr = 0;
};

struct NetEtherAddr
{
	uint8_t data[6];
};

struct NetCtlNatInfo
{
	unsigned int size       = sizeof(NetCtlNatInfo);
	int          stunStatus = 0;
	int          natType    = 0;
	NetInAddr    mappedAddr;
};

union NetCtlInfo
{
	uint32_t     device;
	NetEtherAddr ether_addr;
	uint32_t     mtu;
	uint32_t     link;
	NetEtherAddr bssid;
	char         ssid[32 + 1];
	uint32_t     wifi_security;
	uint8_t      rssi_dbm;
	uint8_t      rssi_percentage;
	uint8_t      channel;
	uint32_t     ip_config;
	char         dhcp_hostname[255 + 1];
	char         pppoe_auth_name[127 + 1];
	char         ip_address[16];
	char         netmask[16];
	char         default_route[16];
	char         primary_dns[16];
	char         secondary_dns[16];
	uint32_t     http_proxy_config;
	char         http_proxy_server[255 + 1];
	uint16_t     http_proxy_port;
};

int KYTY_SYSV_ABI NetCtlInit()
{
	PRINT_NAME();

	return OK;
}

void KYTY_SYSV_ABI NetCtlTerm()
{
	PRINT_NAME();
}

int KYTY_SYSV_ABI NetCtlGetNatInfo(NetCtlNatInfo* nat_info)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(nat_info == nullptr);
	EXIT_NOT_IMPLEMENTED(nat_info->size != sizeof(NetCtlNatInfo));

	nat_info->stunStatus        = 1;
	nat_info->natType           = 3;
	nat_info->mappedAddr.s_addr = 0x7f000001;

	return OK;
}

int KYTY_SYSV_ABI NetCtlCheckCallback()
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI NetCtlGetState(int* state)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(state == nullptr);

	*state = 0; // Disconnected

	return OK;
}

int KYTY_SYSV_ABI NetCtlRegisterCallback(NetCtlCallback func, void* /*arg*/, int* cid)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(func == nullptr);
	EXIT_NOT_IMPLEMENTED(cid == nullptr);

	*cid = 1;

	return OK;
}

int KYTY_SYSV_ABI NetCtlGetInfo(int code, NetCtlInfo* info)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(info == nullptr);

	printf("\t code = %d\n", code);

	switch (code)
	{
		case 2: memset(info->ether_addr.data, 0, sizeof(info->ether_addr.data)); break;
		case 11: info->ip_config = 0; break;
		case 14: strcpy(info->ip_address, "127.0.0.1"); break;
		default: EXIT("unknown code: %d\n", code);
	}

	return OK;
}

} // namespace NetCtl

namespace NpManager {

LIB_NAME("NpManager", "NpManager");

struct NpTitleId
{
	char    id[12 + 1];
	uint8_t padding[3];
};

struct NpTitleSecret
{
	uint8_t data[128];
};

struct NpCountryCode
{
	char data[2];
	char term;
	char padding[1];
};

struct NpAgeRestriction
{
	NpCountryCode country_code;
	int8_t        age;
	uint8_t       padding[3];
};

struct NpContentRestriction
{
	size_t                  size;
	int8_t                  default_age_restriction;
	char                    padding[3];
	int32_t                 age_restriction_count;
	const NpAgeRestriction* age_restriction;
};

struct NpOnlineId
{
	char data[16];
	char term;
	char dummy[3];
};

struct NpId
{
	NpOnlineId handle;
	uint8_t    opt[8];
	uint8_t    reserved[8];
};

struct NpCreateAsyncRequestParameter
{
	size_t                   size;
	LibKernel::KernelCpumask cpu_affinity_mask;
	int                      thread_priority;
	uint8_t                  padding[4];
};

int KYTY_SYSV_ABI NpCheckCallback()
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI NpSetNpTitleId(const NpTitleId* title_id, const NpTitleSecret* title_secret)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(title_id == nullptr);
	EXIT_NOT_IMPLEMENTED(title_secret == nullptr);

	printf("\t title_id = %.12s\n", title_id->id);
	printf("\t title_secret = %s\n", String::HexFromBin(Core::ByteBuffer(title_secret->data, 128)).C_Str());

	return OK;
}

int KYTY_SYSV_ABI NpSetContentRestriction(const NpContentRestriction* restriction)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(restriction == nullptr);
	EXIT_NOT_IMPLEMENTED(restriction->size != sizeof(NpContentRestriction));

	printf("\t default_age_restriction = %" PRIi8 "\n", restriction->default_age_restriction);
	printf("\t age_restriction_count   = %" PRIi32 "\n", restriction->age_restriction_count);

	for (int i = 0; i < restriction->age_restriction_count; i++)
	{
		printf("\t age_restriction[%d].age = %" PRIi8 "\n", i, restriction->age_restriction[i].age);
		printf("\t age_restriction[%d].country_code.data = %.2s\n", i, restriction->age_restriction[i].country_code.data);
	}

	return OK;
}

int KYTY_SYSV_ABI NpRegisterStateCallback(void* /*callback*/, void* /*userdata*/)
{
	PRINT_NAME();

	return OK;
}

void KYTY_SYSV_ABI NpRegisterGamePresenceCallback(void* /*callback*/, void* /*userdata*/)
{
	PRINT_NAME();
}

int KYTY_SYSV_ABI NpRegisterPlusEventCallback(void* /*callback*/, void* /*userdata*/)
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI NpRegisterNpReachabilityStateCallback(void* /*callback*/, void* /*userdata*/)
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI NpGetNpId(int user_id, NpId* np_id)
{
	PRINT_NAME();

	printf("\t user_id = %d\n", user_id);

	EXIT_NOT_IMPLEMENTED(np_id == nullptr);

	int s = snprintf(np_id->handle.data, 16, "Kyty");

	EXIT_NOT_IMPLEMENTED(s >= 16);

	np_id->handle.term = 0;

	return OK;
}

int KYTY_SYSV_ABI NpGetOnlineId(int user_id, NpOnlineId* online_id)
{
	PRINT_NAME();

	printf("\t user_id = %d\n", user_id);

	EXIT_NOT_IMPLEMENTED(online_id == nullptr);

	int s = snprintf(online_id->data, 16, "Kyty");

	EXIT_NOT_IMPLEMENTED(s >= 16);

	online_id->term = 0;

	return OK;
}

int KYTY_SYSV_ABI NpCreateAsyncRequest(const NpCreateAsyncRequestParameter* param)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(param == nullptr);

	printf("\t size              = %" PRIu64 "\n", param->size);
	printf("\t cpu_affinity_mask = %" PRIu64 "\n", param->cpu_affinity_mask);
	printf("\t thread_priority   = %d\n", param->thread_priority);

	static std::atomic_int id = 0;

	EXIT_NOT_IMPLEMENTED(id >= 1);

	return ++id;
}

int KYTY_SYSV_ABI NpDeleteRequest(int req_id)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(req_id != 1);

	printf("\t req_id = %d\n", req_id);

	return OK;
}

int KYTY_SYSV_ABI NpCheckNpAvailability(int req_id, const char* user, void* result)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(req_id != 1);
	EXIT_NOT_IMPLEMENTED(user == nullptr);
	EXIT_NOT_IMPLEMENTED(result != nullptr);

	printf("\t req_id = %d\n", req_id);
	printf("\t user   = %s\n", user);

	return OK;
}

int KYTY_SYSV_ABI NpPollAsync(int req_id, int* result)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(req_id != 1);
	EXIT_NOT_IMPLEMENTED(result == nullptr);

	printf("\t req_id = %d\n", req_id);

	*result = 0;

	return 0;
}

int KYTY_SYSV_ABI NpGetState(int user_id, uint32_t* state)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(state == nullptr);

	printf("\t user_id = %d\n", user_id);

	*state = 1; // Signed out

	return OK;
}

} // namespace NpManager

namespace NpManagerForToolkit {

LIB_NAME("NpManagerForToolkit", "NpManager");

int KYTY_SYSV_ABI NpRegisterStateCallbackForToolkit(void* /*callback*/, void* /*userdata*/)
{
	PRINT_NAME();

	return OK;
}

int KYTY_SYSV_ABI NpCheckCallbackForLib()
{
	PRINT_NAME();

	return OK;
}

} // namespace NpManagerForToolkit

namespace NpTrophy {

LIB_NAME("NpTrophy", "NpTrophy");

struct NpTrophyFlagArray
{
	uint32_t flag_bits[4];
};

int KYTY_SYSV_ABI NpTrophyCreateHandle(int* handle)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(handle == nullptr);

	*handle = 1;

	return OK;
}

int KYTY_SYSV_ABI NpTrophyCreateContext(int* context, int user_id, uint32_t service_label, uint64_t options)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(context == nullptr);
	EXIT_NOT_IMPLEMENTED(options != 0);

	*context = 1;

	printf("\t user_id       = %d\n", user_id);
	printf("\t service_label = %u\n", service_label);

	return OK;
}

int KYTY_SYSV_ABI NpTrophyRegisterContext(int context, int handle, uint64_t options)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(options != 0);
	EXIT_NOT_IMPLEMENTED(context != 1);
	EXIT_NOT_IMPLEMENTED(handle != 1);

	printf("\t context = %d\n", context);
	printf("\t handle  = %d\n", handle);

	return OK;
}

int KYTY_SYSV_ABI NpTrophyDestroyHandle(int handle)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(handle != 1);

	printf("\t handle  = %d\n", handle);

	return OK;
}

int KYTY_SYSV_ABI NpTrophyUnlockTrophy(int context, int handle, int trophy_id, int* platinum_id)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(platinum_id == nullptr);
	EXIT_NOT_IMPLEMENTED(context != 1);
	EXIT_NOT_IMPLEMENTED(handle != 1);

	printf("\t TROPHY UNLOCKED: id = %d\n", trophy_id);

	// this unlock never completes a platinum
	*platinum_id = -1; // SCE_NP_TROPHY_INVALID_TROPHY_ID

	return OK;
}

int KYTY_SYSV_ABI NpTrophyGetTrophyUnlockState(int context, int handle, NpTrophyFlagArray* flags, uint32_t* count)
{
	PRINT_NAME();

	EXIT_NOT_IMPLEMENTED(flags == nullptr);
	EXIT_NOT_IMPLEMENTED(count == nullptr);
	EXIT_NOT_IMPLEMENTED(context != 1);
	EXIT_NOT_IMPLEMENTED(handle != 1);

	printf("\t context = %d\n", context);
	printf("\t handle  = %d\n", handle);

	flags->flag_bits[0] = 0;
	flags->flag_bits[1] = 0;
	flags->flag_bits[2] = 0;
	flags->flag_bits[3] = 0;

	*count = 0;

	return OK;
}

} // namespace NpTrophy

namespace NpWebApi {

LIB_NAME("NpWebApi", "NpWebApi");

int KYTY_SYSV_ABI NpWebApiInitialize(int http_ctx_id, size_t pool_size)
{
	PRINT_NAME();

	EXIT_IF(g_net == nullptr);

	printf("\t http_ctx_id = %d\n", http_ctx_id);
	printf("\t pool_size   = %" PRIu64 "\n", pool_size);

	EXIT_NOT_IMPLEMENTED(!g_net->HttpValid(Network::Id(http_ctx_id)));

	static int id = 0;

	return ++id;
}

int KYTY_SYSV_ABI NpWebApiTerminate(int lib_ctx_id)
{
	PRINT_NAME();

	printf("\t lib_ctx_id = %d\n", lib_ctx_id);

	return OK;
}

} // namespace NpWebApi

} // namespace Kyty::Libs::Network

#endif // KYTY_EMU_ENABLED
