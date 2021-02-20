#ifndef WEBSOCKETCONNECT_H
#define WEBSOCKETCONNECT_H
#include <string>
#include <thread>
#include "libwebsockets.h"

class webSocketListener {
public:
	virtual ~webSocketListener(){}
	virtual void onWsMessage(const char*data,int len) = 0;
};

class websocketConnect
{
public:
	websocketConnect(webSocketListener* wsListener);
	~websocketConnect();
	bool init();
	bool connect(std::string serverIp, short serverPort);

	void onRecvData(const char *data, int len);

	int writeData(const char *data, int len);

protected:
	void wsLoop();
private:
	struct lws_context *m_context;
	struct lws *m_wsi;
	std::thread m_wsThread;
	bool m_exitLoop = false;
	webSocketListener *m_wsListener = nullptr;
};

#endif
