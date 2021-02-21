#include "api/create_peerconnection_factory.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "system_wrappers/include/field_trial.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "api/video_codecs/video_encoder.h"

#include <mutex>
#include <cpp-httplib/httplib.h>
#include <util/url.hpp>
#include <util/timer.h>
#include "RTCLivePusher.h"
#include "RtcLogWrite.h"
#include "DeviceManager.h"
#include "MediaStream.h"
#include "peerconnectionStream.h"

#define SEND_SDP_RES               0x02
#define QUIT_ROOM_RES              0x08
#define CLOSE_PEER                 0x09
#define CLOSE_PEER_RES             0x0a
#define NOTIFY_CREATE_PEER_CONNECTION 0x10
#define ON_WS_INIT_OK              0x13
#define ON_CREATE_ROOM_RESULT      0x14
#define ON_QUALITY_REPORT          0x15
#define ON_QUALITY_REPORT_RES      0x16
#define PUSH_SPEC_USER_LIST_REQ    0x17


static const char* PLAY_STREAM_URL = "http://rtcapi.xueersi.com/rtclive/v1/publish";
static const char* STOP_STREAM_URL = "http://rtcapi.xueersi.com/rtclive/v1/unpublish";
static const char* SCHEDULE_URL = "http://rtcapi.xueersi.com/rtclive/v1/services";

const int kWidth = 1280;
const int kHeight = 720;
const int kFps = 15;
const int kBitrate = 500;
const int kSendSEISeconds = 5;
static const uint8_t uuid_live_start_time[16] =
{
	0xa8, 0x5f, 0xe4, 0xe9, 0x1b, 0x69, 0x11, 0xe8,
	0x85, 0x82, 0x00, 0x50, 0xc2, 0x49, 0x00, 0x48
};


std::unique_ptr<rtc::Thread> g_worker_thread;
std::unique_ptr<rtc::Thread> g_signaling_thread;
rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
g_peer_connection_factory;
log_callback g_logCallBack = nullptr;
void *logObject = nullptr;


static const char *s_webrtcRttMult = "WebRTC-RttMult/Enabled-1.0/"
"WebRTC-Video-ResolutionDegradation/Enabled/"
"WebRTC-Audio-OpusMinPacketLossRate/Enabled-20/";
	                                  //"WebRTC-SpsPpsIdrIsH264Keyframe/Enabled/";

#define SDK_VERSION "1.0.0"

#define BEGIN_ASYN_THREAD_CALL_1(p1)	if (nullptr != m_runLoop) {	m_runLoop->AddRunner(task::Clouser([this, p1]() {
#define BEGIN_ASYN_THREAD_CALL_2(p1,p2)	if (nullptr != m_runLoop) {	m_runLoop->AddRunner(task::Clouser([this, p1, p2]() {
#define BEGIN_ASYN_THREAD_CALL			if (nullptr != m_runLoop) {	m_runLoop->AddRunner(task::Clouser([this]() {
#define BEGIN_SYN_THREAD_CALL			if (nullptr != m_runLoop) {	m_runLoop->AddSynRunner(task::Clouser([&]() {
#define END_THREAD_CALL		}));}


// long SSL_ctrl(SSL *s, int cmd, long larg, void *parg)
// {
// 	return 0;
// }


std::string GetEnvVarOrDefault(const char* env_var_name,
	const char* default_value)
{
	std::string value;
	const char* env_var = getenv(env_var_name);
	if (env_var)
		value = env_var;

	if (value.empty())
		value = default_value;

	return value;
}

void audioDataCallback(const void* audio_data,
	int bits_per_sample,
	int sample_rate,
	int number_of_channels,
	int number_of_frames,
	void *context)
{
	RTCLivePusher* pusher = (RTCLivePusher*)context;
	pusher->receiveAudioFrame(audio_data, bits_per_sample, sample_rate, number_of_channels, number_of_frames);
}

class DummySetSessionDescriptionObserver
	: public webrtc::SetSessionDescriptionObserver
{
public:
	static DummySetSessionDescriptionObserver* Create()
	{
		return new rtc::RefCountedObject<DummySetSessionDescriptionObserver>();
	}
	virtual void OnSuccess() { LOG_INFO("set remote or local Description sucesss"); }
	virtual void OnFailure(webrtc::RTCError error)
	{
		LOG_ERROR("set remote or local Description failed type: " << ToString(error.type()) << " message: " << error.message());
	}

protected:
	DummySetSessionDescriptionObserver() {}
	~DummySetSessionDescriptionObserver() {}
};

std::mutex g_spcMutex;
std::map<unsigned long long, std::unique_ptr<RTCLivePusher>> g_allSpcs;

IRTCLivePusher* createRTCLivePusher()
{
	LOGER;
	std::unique_ptr<RTCLivePusher> spc = std::make_unique<RTCLivePusher>();
	std::lock_guard<std::mutex> locker(g_spcMutex);
	unsigned long long uId = (unsigned long long)spc.get();
	g_allSpcs[(unsigned long long)spc.get()] = std::move(spc);
	return g_allSpcs[uId].get();
}

void destroyRTCLivePusher(IRTCLivePusher* rtcLivePusher) {
	LOG_INFO("rtcLivePusher: " << rtcLivePusher);
	if (rtcLivePusher) {
		std::lock_guard<std::mutex> locker(g_spcMutex);
		g_allSpcs.erase((unsigned long long)rtcLivePusher);
	}
		
}

void rtcRegisterLogFunc(log_callback fn, void *object /*= nullptr*/)
{
	g_logCallBack = fn;
	logObject = object;
}

//#include "gtest/gtest.h"

RTCLivePusher::RTCLivePusher():m_curWidth(kWidth),m_curHeight(kHeight),
                               m_curFps(kFps),m_maxVideoBitrate(kBitrate)
{
	LOGER;

// 	testing::InitGoogleTest();
// 	
// 	RUN_ALL_TESTS();

	initFunctionMap();
	initPeerConnection();
	openLocalStream();
	m_runLoop = task::Runloop::Create();
	m_websocket = new websocketConnect(this);
	m_websocket->init();
    m_websocket->connect("bk.rtc.qq.com", 8687);//qcloud
	//m_websocket->connect("127.0.0.1", 7681);
}

RTCLivePusher::~RTCLivePusher()
{
	LOG_INFO("begin!");
	BEGIN_SYN_THREAD_CALL
		
	END_THREAD_CALL
	LOG_INFO("delete m_runLoop begin!");
	if (m_runLoop) {
		m_runLoop->Stop();
		m_runLoop = nullptr;
	}
	m_localStream = nullptr;
	LOG_INFO("delete m_runLoop end!");

	if (m_uBuffer) {
		delete[] m_uBuffer;
		m_uBuffer = nullptr;
	}

	webrtcEngine::AudioDeviceManager::Release();
	webrtcEngine::VideoDeviceManager::releaseManager();

	g_peer_connection_factory = nullptr;
	g_signaling_thread.reset();
	g_worker_thread.reset();

	if (m_websocket) {
		delete m_websocket;
	}
	LOG_INFO("end!");
}

IVideoDeviceManager* RTCLivePusher::getVideoDeviceManager()
{
	return m_videoManager;
}

IPlayoutManager* RTCLivePusher::getPlayoutManager()
{
	return &m_audioPlayOutManager;
}

IMicManager* RTCLivePusher::getMicManager()
{
	return &m_audioRecoderManager;
}

void RTCLivePusher::setPushParam(const RTCPushConfig& config)
{
	LOG_INFO("begin,width: " << config.width << " height: " << config.height << " fps: " << config.fps << " bitrate: " << config.bitrate);

	int newWidth = config.width;
	int newHeight = config.height;
	int newFps = config.fps;
	int newVideoBitrate = config.bitrate;

	if (newWidth <= 0 || newWidth > 1920) {
		newWidth = kWidth;
	}

	if (newHeight <= 0 || newHeight > 1080) {
		newHeight = kHeight;
	}

	if (newFps <= 0 || newFps > 30) {
		newFps = kFps;
	}

	if (newVideoBitrate <= 0 || newVideoBitrate > 3000) {
		newVideoBitrate = kBitrate;
	}

	bool widthHeightChanged = m_curWidth != newWidth || m_curHeight != newHeight;
	bool fpsChanged = m_curFps != newFps;
	bool videoBitrateChanged = m_maxVideoBitrate != newVideoBitrate;
	m_curWidth = newWidth;
	m_curHeight = newHeight;
	m_curFps = newFps;
	m_maxVideoBitrate = newVideoBitrate;

	if (widthHeightChanged || fpsChanged) {
		LOG_INFO("widthHeightChanged: " << widthHeightChanged << " fpsChanged:" << fpsChanged);
		setVideoFormat();
	}

	if (fpsChanged || videoBitrateChanged) {
		LOG_INFO("videoBitrateChanged: " << videoBitrateChanged << " fpsChanged:" << fpsChanged);
		setSendVideoFpsAndBitrate();
	}

	LOG_INFO("end,width: " << m_curWidth << " height: " << m_curHeight << " fps: " << m_curFps << " bitrate: " << m_maxVideoBitrate);
}

int RTCLivePusher::registerRTCLivePushEventHandler(IRTCLivePusherEvent *eventHandler)
{
	LOG_INFO("eventHandler: " << eventHandler);
	this->m_eventHandler = eventHandler;
	return 0;
}

int RTCLivePusher::startPreview(WindowIdType winId)
{
	m_localStream->startPreview(winId);
	return 0;
}

void RTCLivePusher::stopPreview()
{
	m_localStream->stopPreview();
}

int RTCLivePusher::startPush(const char* posturl,const char* streamId)
{

	if (streamId == nullptr || posturl == nullptr) {
		LOG_ERROR("streamId is null");
		return -1;
	}
	m_streamId = streamId;
	m_postUrl = posturl;
	LOG_INFO("begin! streamId: " << streamId);
	BEGIN_ASYN_THREAD_CALL
		//getPlayListFromSchudle();
		publishStream();
	END_THREAD_CALL
	LOG_INFO("end!");
	return 0;
}

void RTCLivePusher::stopPush()
{
	LOG_INFO("begin!");
	BEGIN_ASYN_THREAD_CALL
		stopStream();
	END_THREAD_CALL
	LOG_INFO("end!");
}


int RTCLivePusher::setMirror(bool isMirror)
{
	LOG_INFO("begin! isMirror: " << isMirror);
	//m_videoSink.setMirror(isMirror);
	LOG_INFO("end!");
	return 0;
}

int RTCLivePusher::muteAudio(bool mute)
{
	return m_localStream->muteAudio(mute);
}

int RTCLivePusher::muteVideo(bool mute)
{
	return m_localStream->muteVideo(mute);
}

int RTCLivePusher::enableAudioVolumeEvaluation(int interval)
{
	LOG_INFO("begin!");

	LOG_INFO("end!");
	return 0;
}

void RTCLivePusher::sendVideoBuffer(const char* data, int width, int height, ColorSpace color)
{

}

void RTCLivePusher::sendAudioBuff(const char* data, int len, int channel, int samplesPerSec)
{
	if (data == nullptr || len <= 0 || channel <= 0 || channel > 2 || samplesPerSec <= 0) {
		LOG_ERROR("invalidate data: " << data << " channel: " << channel << " samplesPerSec: " << samplesPerSec);
		return;
	}

// 	if (!m_isExternalAudioEnabled)
// 		return;
	//webrtcEngine::AudioDeviceManager::instance()->sendRecordedBuffer((const uint8_t*)data, len, 16, samplesPerSec, channel);
}

int RTCLivePusher::enableVideoCapture(bool enable)
{
	return m_localStream->enableVideoCapture(enable);
}

int RTCLivePusher::enableAudioCapture(bool enable)
{
	return m_localStream->enableAudioCapture(enable);
}

bool RTCLivePusher::enableExternalVideoSource(bool enable)
{
// 	LOG_INFO("begin,enable: " << enable);
// 
// 	BEGIN_ASYN_THREAD_CALL_1(enable)
// 		LOG_INFO("enable: " << enable);
// 		if (m_isExternalVideoEnabled == enable) {
// 			LOG_INFO("m_isExternalVideoEnabled repeat!");
// 			return;
// 		}
// 		if (enable) {
// 			if (!openVideoSource(false)) {
// 				LOG_ERROR("openExternalVideoSource failed");
// 				return;
// 			}
// 			m_isLocalVideoEnable = false;
// 		}
// 		else {
// 			enableVideo(false);
// 		}
// 		m_isExternalVideoEnabled = enable;
// 		
// 	END_THREAD_CALL
// 
// 	LOG_INFO("end");
	return true;
}

bool RTCLivePusher::enableExternalAudioSource(bool enable)
{
	LOG_INFO("begin,enable: " << enable);

// 	BEGIN_ASYN_THREAD_CALL_1(enable)
// 		LOG_INFO("enable: " << enable);
// 		if (m_isExternalAudioEnabled == enable) {
// 			LOG_INFO("m_isExternalAudioEnabled repeat!");
// 			return;
// 		}
// 		webrtcEngine::AudioDeviceManager::instance()->setExternalAudioMode(enable);
// 		m_isExternalAudioEnabled = enable;
// 		if (enable) {
// 			m_isLocalAudioEnable = false;
// 		}
// 	END_THREAD_CALL
// 
// 	LOG_INFO("end");
	return true;
}

int RTCLivePusher::sendMessage(const char* msg, int len)
{
	//test
	int64_t currentTime = getServerTime();

	if (currentTime == -1) {
		LOG_ERROR("getServerTime error");
		return -1;
	}

	uint8_t seiInfo[50] = { 0 };
	int index = 0;
	seiInfo[index++] = 0x0;
	seiInfo[index++] = 0x0;
	seiInfo[index++] = 0x0;
	seiInfo[index++] = 0x1;
	seiInfo[index++] = 0x06;
	seiInfo[index++] = 0x05;
	seiInfo[index++] = 0x18;
	memcpy(&seiInfo[index], uuid_live_start_time, sizeof(uuid_live_start_time) / sizeof(uint8_t));
	index += sizeof(uuid_live_start_time) / sizeof(uint8_t);
	webrtc::ByteWriter<int64_t>::WriteBigEndian(&seiInfo[index], currentTime);
	index += sizeof(int64_t);
	seiInfo[index++] = 0x80;

	rtc::CopyOnWriteBuffer buffer;
	buffer.SetData(seiInfo, index);

	LOG_INFO("begin!" << " size: " << buffer.size());
 // 	BEGIN_ASYN_THREAD_CALL_1(buffer)
// 		webrtcEngine::CapturerTrackSource *capture = dynamic_cast<webrtcEngine::CapturerTrackSource *>(m_localVideoTrackSource.get());
// 		if (capture) {
// 			//bool ret = capture->sendSEIMessage((const char*)buffer.cdata(), buffer.size());
// 			//LOG_INFO("is captureVideoSource ret: " << ret << " size: " << buffer.size());
// 			return;
// 		}
// 
// 		ExternalVideoFrameTrackSource *external = dynamic_cast<ExternalVideoFrameTrackSource*>(m_localVideoTrackSource.get());
// 		if (external) {
// 			//bool ret = external->sendSEIMessage((const char*)buffer.cdata(), buffer.size());
// 			//LOG_INFO("is externalVideoSource ret: " << ret << " size: " << buffer.size());
// 			return;
// 		}
// 	END_THREAD_CALL
// 	LOG_INFO("end!");
	return 0;
}

int64_t RTCLivePusher::getServerTime() const {
	if (m_serverTimeFromSchudle == 0)
		return -1;
	return m_serverTimeFromSchudle + chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count() - m_localTimeUTC;
}

void RTCLivePusher::getPlayListFromSchudle()
{
	LOG_INFO("begin!");

	m_serverTimeFromSchudle = 0;

	nlohmann::json answerReq;
	answerReq["streamid"] = m_streamId;

	std::string reqUrl = SCHEDULE_URL;
	http::url url = http::ParseHttpUrl(reqUrl);
	std::unique_ptr<httplib::Client> cli;
	if (url.protocol == "https") {
		return;
	}
	else {
		cli.reset(new httplib::Client(url.host.c_str(), url.port, 5));
	}

	LOG_INFO("answerReq: " << answerReq.dump().c_str() << " reqUrl: " << reqUrl);
	long long startTime = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
	auto res = cli->Post(url.path.c_str(), answerReq.dump().c_str(), "application/json; charset=utf-8");

	if (res) {
		LOG_INFO("respond: " << res->body);
	}

	if (res && res->status == 200) {
		nlohmann::json jsonResponse = nlohmann::json::parse(res->body);
		try {

			nlohmann::json &arryList = jsonResponse["d"]["address"];
			if (!arryList.is_array() || arryList.size() == 0) {
				LOG_ERROR("end,address is not array or size = 0!");
				return;
			}

			if (!jsonResponse["d"]["online"].is_null()) {
				if (!jsonResponse["d"]["online"].get<bool>()) {
					LOG_ERROR("stream offline startStreamNotFoundTimer");
				}
			}

			{
				//在这里取服务器时间
				m_localTimeUTC = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
				if (!jsonResponse["d"]["timestamp"].is_null()) {
					m_serverTimeFromSchudle = jsonResponse["d"]["timestamp"].get<int64_t>() / 1000000;
					//m_serverTimeFromSchudle += m_localTimeUTC - startTime;
					m_timeError = m_localTimeUTC - startTime;
					if (m_timeError > 500) {
						LOG_ERROR("time error so big: " << m_timeError << "reset to -1");
						m_timeError = -1;
					}
				}
				else {
					m_timeError = -1;
				}

				LOG_INFO("m_timeError: " << m_timeError);
			}
		}
		catch (std::exception& e) {
			m_timeError = -1;
			LOG_ERROR("josn format is error: " << e.what());
		}

	}
	else {
		//上报调度失败日志
		LOG_ERROR("end!schudle return error!");
		m_timeError = -1;
		return;
	}
	LOG_INFO("end!");
}

bool RTCLivePusher::publishStream()
{
	LOG_INFO("begin!");

	return m_localStream->publishStream();
}

void RTCLivePusher::stopStream()
{
	LOG_INFO("begin!");
	//reGenPeerConnection();
	if (m_sessionId.empty()) {
		LOG_INFO("end! m_sessionId is empty!");
		return;
	}

	nlohmann::json req;
	req["streamurl"] = m_pushStreamUrl;
	req["sessionid"] = m_sessionId.c_str();
	std::string reqUrl = STOP_STREAM_URL;
	http::url url = http::ParseHttpUrl(reqUrl);
	std::unique_ptr<httplib::Client> cli;
	if (url.protocol == "https") {
		//cli.reset(new httplib::SSLClient(url.host.c_str(), url.port, 3));
		//((httplib::SSLClient*)cli.get())->enable_server_certificate_verification(false);
		LOG_ERROR("https not supported");
		return;
	}
	else {
		cli.reset(new httplib::Client(url.host.c_str(), url.port, 3));
	}

	LOG_INFO("req: " << req.dump().c_str() << " reqUrl: " << reqUrl);

	auto res = cli->Post(url.path.c_str(), req.dump().c_str(), "application/json; charset=utf-8");

	if (res)
		LOG_INFO("respond: " << res->body);

	if (res && res->status == 200) {
		//sucess
	}

	m_sessionId.clear();

	LOG_INFO("end!");
}

void RTCLivePusher::setSendVideoFpsAndBitrate()
{
// 	if (!m_localStream) {
// 		LOG_WARN("m_localStream is null");
// 		return;
// 	}
// 	LOG_INFO("m_curFps: " << m_curFps << " m_maxVideoBitrate: " << m_maxVideoBitrate);
// 	BEGIN_ASYN_THREAD_CALL
// 		if (!m_videoTransceiver) {
// 			LOG_ERROR("m_videoTransceiver is null");
// 			return;
// 		}
// 	    LOG_INFO("m_curFps: " << m_curFps << " m_maxVideoBitrate: " << m_maxVideoBitrate);
// 		webrtc::RtpParameters parametersToModify = m_videoTransceiver->sender()->GetParameters();
// 		for (auto &encoding : parametersToModify.encodings) {
// 			encoding.max_framerate = m_curFps;
// 			encoding.max_bitrate_bps = m_maxVideoBitrate * 1000;
// 		}
// 		m_videoTransceiver->sender()->SetParameters(parametersToModify);
// 	END_THREAD_CALL
}

bool RTCLivePusher::setVideoFormat()
{
// 	LOG_INFO("begin,m_curWidth: " << m_curWidth << " m_curHeight:" << m_curHeight << " m_curFps: " << m_curFps);
// 	if (!m_localStream) {
// 		LOG_ERROR("end,m_localStream is null");
// 		return false;
// 	}
// 	BEGIN_ASYN_THREAD_CALL
// 	    bool ret = g_signaling_thread->Invoke<bool>(RTC_FROM_HERE, [&] {
// 		if (!m_localVideoTrackSource) {
// 			LOG_ERROR("m_localVideoTrackSource is null");
// 			return false;
// 		}
// 		LOG_INFO("begin,m_curWidth: " << m_curWidth << " m_curHeight:" << m_curHeight << " m_curFps: " << m_curFps);
// 		webrtcEngine::CapturerTrackSource *capture = dynamic_cast<webrtcEngine::CapturerTrackSource *>(m_localVideoTrackSource.get());
// 
// 		if (capture) {
// 			LOG_INFO("is captureVideoSource");
// 			return capture->setVideoFormat(m_curWidth, m_curHeight, m_curFps);
// 		}
// 
// 		ExternalVideoFrameTrackSource *external = dynamic_cast<ExternalVideoFrameTrackSource*>(m_localVideoTrackSource.get());
// 
// 		if (external) {
// 			LOG_INFO("is externalVideoSource");
// 			external->setVideoFormat(m_curWidth, m_curHeight, m_curFps);
// 			return true;
// 		}
// 		LOG_ERROR("return false");
// 		return false;
// 		});
// 	LOG_INFO("ret :" << ret);
// 	END_THREAD_CALL
// 
// 	LOG_INFO("end");
	return true;
}

void RTCLivePusher::setVideoDeviceIndex(int videoDeviceIndex)
{
	LOG_INFO("videoDeviceIndex: " << videoDeviceIndex);
	m_curVideoDeviceIndex = videoDeviceIndex;
}

bool RTCLivePusher::setLocalDevice(const char* deviceName)
{
// 	if (!deviceName) {
// 		LOG_ERROR("deviceName is null ");
// 		return false;
// 	}
// 	LOG_INFO("deviceName: " << deviceName);
// 	
// 	if (m_isExternalVideoEnabled) {
// 		LOG_ERROR("m_isExternalVideoEnabled: " << m_isExternalVideoEnabled);
// 		return false;
// 	}
// 
// 	if (!m_localVideoTrackSource) {
// 		if (!createVideoTrack(true)) {
// 			LOG_ERROR("createVideoTrack error!");
// 			return false;
// 		}
// 	}
// 
// 	return g_signaling_thread->Invoke<bool>(RTC_FROM_HERE, [&] {
// 		if (!m_localVideoTrackSource)
// 			return false;
// 		webrtcEngine::CapturerTrackSource* capture = dynamic_cast<webrtcEngine::CapturerTrackSource*>(m_localVideoTrackSource.get());
// 		if (!capture) {
// 			LOG_ERROR("m_localVideoTrackSource is not a webrtcEngine::CapturerTrackSource");
// 			return false;
// 		}
// 		return capture->setDevice(deviceName);
// 	});

	return true;
}

void RTCLivePusher::receiveAudioFrame(const void* audio_data, int bits_per_sample, int sample_rate, int number_of_channels, int number_of_frames)
{
	if (m_eventHandler) {
		m_eventHandler->onCaptureAudioFrame((const char*)audio_data,number_of_frames * bits_per_sample * number_of_channels ,number_of_channels,sample_rate);
	}
}

void RTCLivePusher::initFunctionMap()
{
	m_functionMap[ON_WS_INIT_OK] = &RTCLivePusher::onWsInitOK;
	m_functionMap[ON_CREATE_ROOM_RESULT] = &RTCLivePusher::onCreateRoomRes;
	m_functionMap[PUSH_SPEC_USER_LIST_REQ] = &RTCLivePusher::onPushSpecUserList;
	m_functionMap[NOTIFY_CREATE_PEER_CONNECTION] = &RTCLivePusher::onCreatePeerConnect;
	m_functionMap[SEND_SDP_RES] = &RTCLivePusher::onAnswerSdp;
}

void RTCLivePusher::createRoom()
{
	nlohmann::json roomInfo;

	roomInfo["tag_key"] = "on_create_room";
	roomInfo["openid"] = m_userId.c_str();
	roomInfo["tinyid"] = m_tinyId.c_str();
	roomInfo["version"] = "4.6.2";
	roomInfo["ua"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/85.0.4183.121 Safari/537.36";
	nlohmann::json &data = roomInfo["data"];
	data["openid"] = m_userId.c_str();
	data["tinyid"] = m_tinyId.c_str();
	data["useStrRoomid"] = false;
	data["roomid"] = m_roomId.c_str();
	data["sdkAppID"] = m_appId.c_str();
	data["socketid"] = m_socketId.c_str();
	data["userSig"] = m_userSig.c_str();
	data["relayip"] = m_relayIp.c_str();
	data["dataport"] = 9000;
	data["checkSigSeq"] = m_checkSigSeq.c_str();
	data["pstnBizType"] = 0;
	data["stunport"] = m_stunPort;
	data["role"] = "user";
	data["jsSdkVersion"] = "4602";
	data["sdpSemantics"] = "unified-plan";
	data["browserVersion"] = "Chrome/85.0.4183.121";
	data["closeLocalMedia"] = true;
	data["trtcscene"] = 1;
	data["trtcrole"] = 20;
	data["isAuxUser"] = 0;

	std::string content = roomInfo.dump();
	m_websocket->writeData(content.c_str(), content.length());
}

void RTCLivePusher::sendPeerSdp(const std::string &srcTinyid, const std::string& sdp,const std::string& type)
{
	if (!m_inRoom) {
		LOG_ERROR("not join room!");
		return;
	}

	nlohmann::json PeerInfo;

	PeerInfo["tag_key"] = "on_peer_sdp";
	PeerInfo["openid"] = m_userId.c_str();
	PeerInfo["tinyid"] = m_tinyId.c_str();
	if(type  == "answer")
		PeerInfo["srctinyid"] = srcTinyid.c_str();
	else {
		PeerInfo["srctinyid"] = 0;
	}
	PeerInfo["version"] = "4.6.2";
	PeerInfo["ua"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/85.0.4183.121 Safari/537.36";
	nlohmann::json &data = PeerInfo["data"];
	data["sdp"] = sdp.c_str();
	data["type"] = type;

	std::string content = PeerInfo.dump();
	m_websocket->writeData(content.c_str(), content.length());
}

void RTCLivePusher::sendQualityReport()
{
	nlohmann::json PeerInfo;

	PeerInfo["tag_key"] = "on_quality_report";
	PeerInfo["openid"] = m_userId.c_str();
	PeerInfo["tinyid"] = m_tinyId.c_str();
	PeerInfo["version"] = "4.6.2";
	PeerInfo["ua"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/85.0.4183.121 Safari/537.36";
	nlohmann::json &data = PeerInfo["data"];

	data["cpudevice"] = "Win32";
	data["cpunumber"] = 12;
	data["ostype"] = "Win32";
	data["roomid"] = m_roomId.c_str();
	data["sdkAppId"] = m_appId.c_str();
	data["serverip"] = m_relayIp.c_str();
	data["socketid"] = m_socketId.c_str();
	data["tinyid"] = m_tinyId.c_str();


	std::string content = PeerInfo.dump();
	//m_websocket->writeData(content.c_str(), content.length());

	if (!bsendOffer && !lastSdp.empty()) {
		sendPeerSdp("", lastSdp, "offer");
		bsendOffer = true;
	}
}

void RTCLivePusher::onWsMessage(const char *data, int len)
{
	nlohmann::json jsonMessage = nlohmann::json::parse(data);

	try {

		int cmd = jsonMessage["cmd"].get<int>();
		auto it = m_functionMap.find(cmd);
		if (it == m_functionMap.end()) {
			LOG_ERROR("can't find cmd " << cmd);
			return;
		}
		(this->*it->second)(jsonMessage);

	}
	catch (std::exception& e) {
		LOG_ERROR("josn format is error: " << e.what());
	}
}

void RTCLivePusher::onWsInitOK(const nlohmann::json &param)
{
	try {
		const nlohmann::json &content = param["content"];
		m_userId = content["openid"].get <std::string>();
		m_socketId = content["socketid"].get <std::string>();
		m_tinyId = content["tinyid"].get <std::string>();
		m_checkSigSeq = content["checkSigSeq"].get<std::string>();
		m_relayIp = content["relayip"].get<std::string>();
		m_signalIp = content["signalip"].get<std::string>();
		m_stunPort = content["stunport"].get<int>();
		LOG_INFO("userid :" << m_userId << " socketid:" << m_socketId << " tinyid:" << m_tinyId);
		createRoom();
	}
	catch (std::exception& e) {
		LOG_ERROR("josn format is error: " << e.what());
	}

}

void RTCLivePusher::onCreateRoomRes(const nlohmann::json &param)
{
	try {
		const nlohmann::json &content = param["content"];
		m_inRoom = content["ret"].get<int>() == 0;

		m_eventHandler->onPusherEvent(m_inRoom ? RTCLIVE_ENTERROOM : RTCLIVE_LEAVEROOM);

//		OutputDebugStringA(param.dump().c_str());
//		OutputDebugStringA("\n");
	}
	catch (std::exception& e) {
		LOG_ERROR("josn format is error: " << e.what());
	}
}

void RTCLivePusher::onPushSpecUserList(const nlohmann::json &param)
{
	//OutputDebugStringA(param.dump().c_str());

	sendQualityReport();
}

void RTCLivePusher::onCreatePeerConnect(const nlohmann::json &param)
{
	try {
		const nlohmann::json &content = param["content"];
		std::string remoteSdp = content["remoteoffer"]["sdp"].get<std::string>();
		std::string srcTinyId = content["srctinyid"].get<std::string>();

		webrtc::SdpParseError error;
		webrtc::SessionDescriptionInterface* session_description(
			webrtc::CreateSessionDescription("offer", remoteSdp, &error));
		if (!session_description) {
			LOG_ERROR("Can't parse received session description message. "
				<< "SdpParseError was: " << error.description);
			return;
		}

		auto it = remotePeer.find(srcTinyId);
		if (it != remotePeer.end()) {
			//reGenPeerConnection();
		}
		else {
			remotePeer.insert(srcTinyId);
		}

		//m_peerConnection->SetRemoteDescription(DummySetSessionDescriptionObserver::Create(), session_description);

		webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;

// 		LOG_INFO("create answer------------------------");
// 		{
// 			std::unique_lock<std::mutex> locker(m_mutexOffer);
// 			m_peerConnection->CreateAnswer(this, options);
// 			m_conOffer.wait(locker);
// 			if (m_answer.empty()) {
// 				LOG_ERROR("m_answer is empty!!");
// 				return;
// 			}
// 		}
		//sendPeerSdp(srcTinyId,m_answer,"anwser");
	}
	catch (std::exception& e) {
		LOG_ERROR("josn format is error: " << e.what());
	}
}

void RTCLivePusher::onAnswerSdp(const nlohmann::json &param)
{
	const nlohmann::json &content = param["content"];
	m_localStream->setRemoteSdp(content["sdp"].get<std::string>());
}

void RTCLivePusher::sendOffer(const std::string &offer)
{
	lastSdp = offer;
	bsendOffer = false;
}

bool RTCLivePusher::initPeerConnection()
{
	LOG_INFO("begin!");
	if (g_peer_connection_factory == nullptr) {

		//rtc::LogMessage::LogToDebug(rtc::LoggingSeverity::LS_VERBOSE);
		rtc::LogMessage::SetLogToStderr(false);
		webrtc::field_trial::InitFieldTrialsFromString(s_webrtcRttMult);

		g_worker_thread = rtc::Thread::CreateWithSocketServer();
		g_worker_thread->Start();
		g_signaling_thread = rtc::Thread::Create();
		g_signaling_thread->Start();

		if (!webrtcEngine::AudioDeviceManager::instance()->init(
			g_worker_thread.get(),nullptr)) {
			return false;
		}

		g_peer_connection_factory = webrtc::CreatePeerConnectionFactory(
			g_worker_thread.get(), g_worker_thread.get(), g_signaling_thread.get(),
			webrtcEngine::AudioDeviceManager::instance()->getADM(),
			webrtc::CreateBuiltinAudioEncoderFactory(),
			webrtc::CreateBuiltinAudioDecoderFactory(),
			webrtc::CreateBuiltinVideoEncoderFactory(),
			webrtc::CreateBuiltinVideoDecoderFactory(), nullptr, nullptr);
	}

	LOG_INFO("end!");
	return true;
}

bool RTCLivePusher::openLocalStream()
{
	LOG_INFO("begin!");
	if (m_localStream) {
		LOG_INFO("end!");
		return true;
	}

	m_localStream = new rtc::RefCountedObject<PeerConnectionStream>(this,true);
	//m_localStream.reset(new MediaStreamImpl(rtc::CreateRandomUuid(), this));
	//webrtcEngine::AudioDeviceManager::instance()->registerObserver(m_localStream.get());
	webrtcEngine::AudioDeviceManager::instance()->startPlayOut();
	m_localStream->CreateMediaStream();

	m_videoManager = new VideoManagerImpl(m_localStream);
	LOG_INFO("end!");
	return true;
}
