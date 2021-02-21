#include "websocketConnect.h"



#define MAX_PAYLOAD_SIZE  15 * 1024

static uint8_t sendBuf[LWS_PRE + MAX_PAYLOAD_SIZE];
/**
 * 会话上下文对象，结构根据需要自定义
 */
struct session_data {
	int msg_count;
	unsigned char buf[LWS_PRE + MAX_PAYLOAD_SIZE];
	int len;
};


int wssCallback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    printf("%s %s %d wsi: %p, reason: %d, user: %p, in: %p, len: %d\n",
            __FILE__, __func__, __LINE__, wsi, reason, user, in, len);
	websocketConnect *wsConnection = nullptr;
	if (user != nullptr) {
		wsConnection = static_cast<websocketConnect*>(user);
	}
	switch (reason) {
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		if (wsConnection == nullptr) {
			return -1;
		}
		//wsConnection->onNetStatChanged(LWS_CALLBACK_CLIENT_ESTABLISHED);
		lws_set_timer_usecs(wsi, 5 * LWS_USEC_PER_SEC);
		break;
	case LWS_CALLBACK_CLIENT_RECEIVE:
		if (wsConnection == nullptr) {
			return -1;
		}

		wsConnection->onRecvData((const char *)in, len);
		break;
	case LWS_CALLBACK_CLIENT_CLOSED:
		if (wsConnection == nullptr) {
			return -1;
		} 

		//wsConnection->onNetStatChanged(LWS_CALLBACK_CLIENT_CLOSED);
		lws_cancel_service(lws_get_context(wsi));
		break;
	case LWS_CALLBACK_CLIENT_WRITEABLE:
		if (wsConnection == nullptr) {
			return -1;
		}
		//if (wsConnection->m_sendPing) 
// 		{
// 			//wsConnection->m_sendPing = false;
// 			uint8_t ping[LWS_PRE + 125] = { 0 };
// 			int m = lws_write(wsi, ping + LWS_PRE, 0, LWS_WRITE_PING);
// 			if (m < 0) {
// 				return -1;
// 			}
// 			lws_callback_on_writable(wsi);
// 		}
		break;
	case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
		break;
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
	{
		char *error = (char *)in;
		//wsConnection->onNetStatChanged(LWS_CALLBACK_CLIENT_CLOSED);
		lws_cancel_service(lws_get_context(wsi));
	}
		break;
	case LWS_CALLBACK_TIMER:
		//if (wsConnection == nullptr) {
		//	return -1;
		//}
		//wsConnection->m_sendPing = true;
		//lws_callback_on_writable(wsi);
		//lws_set_timer_usecs(wsi, 5 * LWS_USEC_PER_SEC);
	default:
		break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}


struct lws_protocols protocols[] = {
	{
		//协议名称，协议回调，接收缓冲区大小
		"wss", wssCallback, sizeof(struct session_data), MAX_PAYLOAD_SIZE
	},
	{
		NULL, NULL,   0// 最后一个元素固定为此格式
	}
};


websocketConnect::websocketConnect(webSocketListener* wsListener)
{
	m_context = nullptr;
	m_wsi = nullptr;
	m_wsListener = wsListener;
}

websocketConnect::~websocketConnect()
{
	m_exitLoop = true;

	if (m_wsThread.joinable()) {
		m_wsThread.join();
	}
	if (m_context)
		lws_context_destroy(m_context);
}

bool websocketConnect::init()
{
	// 用于创建vhost或者context的参数
	struct lws_context_creation_info ctx_info = { 0 };
	ctx_info.port = CONTEXT_PORT_NO_LISTEN;
	ctx_info.iface = NULL;
	ctx_info.gid = -1;
	ctx_info.uid = -1;
	ctx_info.protocols = protocols;
	ctx_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	//ctx_info.ssl_cert_filepath = "D:/webrtcpuher/Debug/localhost-100y.cert";
	//ctx_info.ssl_private_key_filepath = "D:/webrtcpuher/Debug/localhost-100y.key";

	// 创建一个WebSocket处理器
	m_context = lws_create_context(&ctx_info);
	return m_context != nullptr;
}

bool websocketConnect::connect(std::string serverIp, short serverPort)
{
	char addr_port[256] = { 0 };
	sprintf(addr_port, "%s:%u", serverIp.c_str(), serverPort & 65535);
	// 客户端连接参数
	struct lws_client_connect_info conn_info = { 0 };
	conn_info.context = m_context;
	conn_info.address = serverIp.c_str();
	conn_info.port = serverPort;
    conn_info.path = "/?sdkAppid=1400188366&identifier=user_65290312&userSig=eJw1jUELgjAYhv*K7Bz2bXNrBh0qupQQlYc6RbRpS7OxqQnRfy9Nr*-D87xvFEcH3*kUTT00IerOVtn2VN3iTb7Yx0c7TpuI0MIEkF*X*Jm4ZC3m9e41QyOvU1VjtFU-GzPGCAD0u5PZxRgtWxIAYCEo5z2rlW3viA9DpdSPrsExFSFldKhoqYpSJ-ovVE7ZM2ckBIoJ*nwB98s0cQ__";
	conn_info.host = addr_port;
	conn_info.protocol = protocols[0].name;
//    conn_info.method = "GET";
    conn_info.origin = "https://trtc-1252463788.file.myqcloud.com";
	conn_info.ssl_connection =  LCCSCF_USE_SSL | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK | LCCSCF_ALLOW_INSECURE;
	conn_info.userdata = this;
	
	
	m_wsi = lws_client_connect_via_info(&conn_info);

	std::thread t(std::bind(&websocketConnect::wsLoop, this));
	t.swap(m_wsThread);

	return m_wsi != nullptr;
}

void websocketConnect::onRecvData(const char *data, int len)
{
	if (m_wsListener) {
		m_wsListener->onWsMessage(data, len);
	}
}

int websocketConnect::writeData(const char *data, int len)
{
	memcpy(&sendBuf[LWS_PRE], data, len);
	return lws_write(m_wsi, &sendBuf[LWS_PRE], len, LWS_WRITE_TEXT);
}

void websocketConnect::wsLoop()
{
	while (!m_exitLoop) {
		if (m_context != nullptr && m_wsi != nullptr) {
			lws_service(m_context, 100);
		}
	}
	
}
