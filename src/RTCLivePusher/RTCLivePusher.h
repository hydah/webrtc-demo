#ifndef RTCLIVEPUSHER_H
#define RTCLIVEPUSHER_H

#include "IRTCLivePusher.h"
#include <TaskQueue/RunLoop.h>
#include <nlohmann/json.hpp>
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "pc/video_track_source.h"
#include "RTCVideoSink.h"
#include "DeviceManager.h"
//websocket
#include "websocketConnect.h"

class MediaStreamImpl;
class PeerConnectionStream;

class RTCLivePusher : public IRTCLivePusher,
                      public webSocketListener {

	typedef  void (RTCLivePusher::*fpn_)(const nlohmann::json &param);
	
public:
	RTCLivePusher();
	~RTCLivePusher();
	virtual IVideoDeviceManager* getVideoDeviceManager() override;
	virtual IPlayoutManager* getPlayoutManager() override;
	virtual IMicManager* getMicManager() override;
	virtual void setPushParam(const RTCPushConfig& config) override;
	virtual int registerRTCLivePushEventHandler(IRTCLivePusherEvent *eventHandler) override;
	virtual int startPreview(WindowIdType winId) override;
	virtual void stopPreview() override;
	virtual int startPush(const char* posturl,const char* streamId) override;
	virtual void stopPush() override;
	virtual int setMirror(bool isMirror) override;
	virtual int muteAudio(bool mute) override;
	virtual int muteVideo(bool mute) override;
	virtual int enableAudioVolumeEvaluation(int interval) override;
	virtual void sendVideoBuffer(const char* data, int width, int height, ColorSpace color) override;
	virtual void sendAudioBuff(const char* data, int len, int channel, int samplesPerSec) override;
	virtual int enableVideoCapture(bool enable) override;
	virtual int enableAudioCapture(bool enable) override;
	virtual bool enableExternalVideoSource(bool enable) override;
	virtual bool enableExternalAudioSource(bool enable) override;
	virtual int sendMessage(const char* msg, int len) override;

	int64_t getServerTime() const;
	void getPlayListFromSchudle();
	bool publishStream();
	void stopStream();
	void setSendVideoFpsAndBitrate();
	bool setVideoFormat();
	void setVideoDeviceIndex(int videoDeviceIndex);
	bool setLocalDevice(const char* deviceName);

	void receiveAudioFrame(const void* audio_data,
		int bits_per_sample,
		int sample_rate,
		int number_of_channels,
		int number_of_frames);

	void initFunctionMap();
	void createRoom();
	void sendPeerSdp(const std::string &srcTinyid, const std::string& sdp, const std::string& type);
	void sendQualityReport();
	void onWsMessage(const char *data, int len) override;
	void onWsInitOK(const nlohmann::json &param);
	void onCreateRoomRes(const nlohmann::json &param);
	void onPushSpecUserList(const nlohmann::json &param);
	void onCreatePeerConnect(const nlohmann::json &param);
	void onAnswerSdp(const nlohmann::json &param);


	void sendOffer(const std::string &offer);
	
protected:
	// create a peerconneciton and add the turn servers info to the configuration.
	bool initPeerConnection();
	bool openLocalStream();
private:
	IRTCLivePusherEvent *m_eventHandler = nullptr;
	task::Runloop *m_runLoop = nullptr;

	rtc::scoped_refptr<PeerConnectionStream> m_localStream;
	int m_curWidth;
	int m_curHeight;
	int m_curFps;
	int m_maxVideoBitrate;
	int m_curVideoDeviceIndex = 0;
	uint8_t *m_uBuffer = nullptr;
	std::string m_streamId;
	std::string m_pushStreamUrl;
	std::string m_sessionId;
	std::string m_postUrl;

	//for create offer
	std::mutex m_mutexOffer;
	std::condition_variable m_conOffer;
	std::string m_answer;


	int64_t  m_currentServerTime = 0;
	int64_t  m_serverTimeFromSchudle = 0;
	int64_t  m_localTimeUTC = 0;
	int64_t  m_timeError = 0;
	uint64_t m_renderCount = 0;

	/*device Manager*/
	VideoManagerImpl *m_videoManager;
	AudioRecoderManagerImpl m_audioRecoderManager;
	AudioPlayoutManagerImpl m_audioPlayOutManager;

	websocketConnect *m_websocket = nullptr;

	std::map<int, fpn_> m_functionMap;
	std::string m_userId;
	std::string m_socketId;
	std::string m_tinyId;
    std::string m_roomId = "6969";
	std::string m_appId = "1400188366";
	std::string m_userSig = "eJxNjl1PgzAUhv8LtxhzWtaGmXixKGLNnIsT5nbT4CjjDC2stFNj-O9WskVvnyfvx1fwNF2cF5tN67SV9rNTwUUAwdmAsVTaYoXKeOh6ZSSh0YgdbdF1WMrCysiU-0J92chBeUZGACSOI86PUn10aJQsKjt0EsYYBThFD8r02GovKBDmtwD*pMW332*EQzTmwMmpssetx-dJdiVuslyH6WydNu96*trUd9S9JGvudpCuakHmS9fukS5zuhITTCZhLXZWhPPkOb7d50aM0c6ia8rjw8K5-AEfM2O2zlW66i*D7x9vnFlb";
	std::string m_checkSigSeq;
	std::string m_relayIp;
	std::string m_signalIp;
	int m_stunPort;
	std::set<std::string> remotePeer;
	bool m_inRoom = false;

	bool bsendOffer = false;
	std::string lastSdp;
};
#endif
