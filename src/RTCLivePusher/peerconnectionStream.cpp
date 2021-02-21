#include "third_party/libyuv/include/libyuv.h"
#include "CapturerTrackSource.h"
#include "ExternalVideoFrameTrackSource.h"

#include "peerconnectionStream.h"
#include "MediaStream.h"
#include "RtcLogWrite.h"
#include "RTCLivePusher.h"

extern std::unique_ptr<rtc::Thread> g_worker_thread;
extern std::unique_ptr<rtc::Thread> g_signaling_thread;
extern rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
		g_peer_connection_factory;

const char g_kAudioLabel[] = "audio";
const char g_kVideoLabel[] = "video";
const char g_kStreamId[] = "stream_id";


void externalVideoFrame2YuvI420(void* buffer, int width, int height, uint64_t timestamp, ColorSpace color, ExternalVideoFrame& videoFrame)
{
	videoFrame.height = height;
	videoFrame.width = width;
	videoFrame.renderTimeMs = timestamp;
	videoFrame.rotation = 0;
	videoFrame.yStride = videoFrame.width;
	videoFrame.uStride = videoFrame.width / 2;
	videoFrame.vStride = videoFrame.width / 2;

	if (color == Color_YUVI420) {
		videoFrame.yBuffer = buffer;
		videoFrame.uBuffer = (char*)videoFrame.yBuffer + videoFrame.height*videoFrame.width;
		int size_uv = ((videoFrame.width + 1) >> 1) * ((videoFrame.height + 1) >> 1);
		videoFrame.vBuffer = (char*)videoFrame.uBuffer + size_uv;
	}
	else if (color == Color_RGB32) {
		libyuv::ABGRToI420((const uint8_t*)buffer, width * 4,
			(uint8_t*)videoFrame.yBuffer, videoFrame.yStride,
			(uint8_t*)videoFrame.uBuffer, videoFrame.uStride,
			(uint8_t*)videoFrame.vBuffer, videoFrame.vStride,
			videoFrame.width, videoFrame.height);
	}
	else if (color == Color_RGB24) {
		libyuv::RGB24ToI420((const uint8_t*)buffer, width * 3,
			(uint8_t*)videoFrame.yBuffer, videoFrame.yStride,
			(uint8_t*)videoFrame.uBuffer, videoFrame.uStride,
			(uint8_t*)videoFrame.vBuffer, videoFrame.vStride,
			videoFrame.width, videoFrame.height);
	}
}

void videoDataCallback(const uint8_t* data_y,
	const uint8_t* data_u,
	const uint8_t* data_v,
	const uint8_t* data_a,
	int stride_y,
	int stride_u,
	int stride_v,
	int stride_a,
	uint32_t width,
	uint32_t height,
	int64_t  render_time,
	void *context)
{
	PeerConnectionStream* pusher = (PeerConnectionStream*)context;
	pusher->OnFrame(data_y, data_u, data_v, data_a,
		stride_y, stride_u, stride_v, stride_a,
		width, height, render_time);
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

PeerConnectionStream::PeerConnectionStream(RTCLivePusher *pusher,bool isLocalStream):m_pusher(pusher),m_isLocalStream(isLocalStream)
{

}

PeerConnectionStream::~PeerConnectionStream()
{
	if (m_peerConnection) {
		LOG_INFO("delete peerconnetion");
		closeVideoSource();
		deletePeerConnection();
		LOG_INFO("delete peerconnetion end!");
	}

	m_audioTrack = nullptr;
	m_bigVideoTrack = nullptr;
	m_smallVideoTrack = nullptr;
	m_peerConnection = nullptr;

	if (m_uBuffer) {
		delete[] m_uBuffer;
		m_uBuffer = nullptr;
	}
}

void PeerConnectionStream::CreateMediaStream()
{
	m_meidaStream.reset(new MediaStreamImpl(rtc::CreateRandomUuid(), this));
	m_meidaStream->RegisterOnI420Frame(I420FRAMEREADY_CALLBACK(videoDataCallback, this));
	createPeerConnection();
}

bool PeerConnectionStream::createVideoTrack(bool capture)
{
	LOG_INFO("begin!");
	if (!m_meidaStream) {
		LOG_INFO("end! m_localStream is null");
		return false;
	}

	if (m_localVideoTrackSource) {
		LOG_INFO("end! m_localVideoTrackSource has create!");
		return true;
	}

	webrtc::MethodCall<PeerConnectionStream, void, bool> callCreate(this, &PeerConnectionStream::createVideoSource, std::move(capture));
	callCreate.Marshal(RTC_FROM_HERE, g_signaling_thread.get());

	if (!m_localVideoTrackSource) {
		LOG_INFO("end! createVcmVideoSource failed!");
		return false;
	}

	m_bigVideoTrack = g_peer_connection_factory->CreateVideoTrack(
		rtc::CreateRandomUuid(), m_localVideoTrackSource);
	
	if (!m_bigVideoTrack) {
		LOG_INFO("end! CreateVideoTrack failed!");
		return false;
	}
	m_bigVideoTrack->AddOrUpdateSink(m_meidaStream->videoObserver(),
		rtc::VideoSinkWants());

//	m_smallVideoTrack = g_peer_connection_factory->CreateVideoTrack(rtc::CreateRandomUuid(), m_smallVideoTrackSource);

//	if (!m_smallVideoTrack) {
//		LOG_INFO("end! CreateSmallVideoTrack failed!");
//		return false;
//	}

	LOG_INFO("end!");
	return true;
}

bool PeerConnectionStream::openAudioSource()
{
	LOG_INFO("begin!");
	if (!m_meidaStream) {
		LOG_INFO("end! m_localStream is null");
		return false;
	}

	if (m_audioTrack) {
		LOG_INFO("end! m_audioTrack is not null");
		return true;
	}

	m_audioTrack = g_peer_connection_factory->CreateAudioTrack(
		g_kAudioLabel,
		g_peer_connection_factory->CreateAudioSource(cricket::AudioOptions()));

	if (m_audioTrack) {
		m_audioTrack->AddSink(m_meidaStream->audioObserver());
	}
	LOG_INFO("end! m_audioTrack: " << m_audioTrack);
	return m_audioTrack != nullptr;
}

bool PeerConnectionStream::openVideoSource(bool capture)
{
	LOG_INFO("begin!");

	if (!m_meidaStream) {
		LOG_INFO("end! m_localStream is null");
		return false;
	}

	if (m_bigVideoTrack && m_videoTransceiver) {
		enableVideo(false);
	}
	else {

		if (m_bigVideoTrack)
			LOG_INFO("close videoTrack!");
		m_bigVideoTrack = nullptr;
		m_smallVideoTrack = nullptr;

		if (m_localVideoTrackSource) {
			LOG_INFO("close localVideoTrackSource!");
			closeVideoSource();
		}
	}

	if (!createVideoTrack(capture)) {
		LOG_ERROR("createVideoTrack error!");
		return false;
	}

	if (m_videoTransceiver)
		enableVideo(true);
	LOG_INFO("end!");
	return true;
}

void PeerConnectionStream::createVideoSource(bool capture)
{
	LOG_INFO("begin! capture: " << capture);
    m_localVideoTrackSource = webrtcEngine::CapturerTrackSource::Create(
        m_curWidth, m_curHeight, m_curFps, m_curVideoDeviceIndex);
    if (m_localVideoTrackSource == nullptr) {
        LOG_INFO("track source is null");
    }
//	m_smallVideoTrackSource = ExternalVideoFrameTrackSource::Create(m_curWidth / 2, m_curHeight / 2, m_curFps);
	LOG_INFO("end!");
}

void PeerConnectionStream::releaseVideoSource()
{
	LOG_INFO("begin!");
	m_localVideoTrackSource = nullptr;
	m_smallVideoTrackSource = nullptr;
	LOG_INFO("end!");
}

void PeerConnectionStream::closeVideoSource()
{
	LOG_INFO("begin!");
	webrtc::MethodCall<PeerConnectionStream, void> callStop(
		this, &PeerConnectionStream::releaseVideoSource);
	callStop.Marshal(RTC_FROM_HERE, g_signaling_thread.get());
	LOG_INFO("end!");
}

bool PeerConnectionStream::AddTransceiverAndCreateOfferForSend()
{
	LOG_INFO("begin!");
	if (!m_meidaStream) {
		LOG_ERROR("end! m_localStream is null");
		return false;
	}

	::webrtc::RtpTransceiverInit init;
	init.stream_ids.push_back(m_meidaStream->id());
	init.direction = ::webrtc::RtpTransceiverDirection::kSendOnly;
	::webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> ret;
	if (m_audioTrack) {
		LOG_INFO("add m_audioTrack");
		ret = m_peerConnection->AddTransceiver(m_audioTrack, init);
		m_audioTransceiver = ret.value();
	}
	else {
		LOG_ERROR("end! m_audioTrack is null");
		return false;
	}


    init.send_encodings.resize(1);
    init.send_encodings[0].max_bitrate_bps = m_maxVideoBitrate * 1000;
    init.send_encodings[0].max_framerate = m_curFps;
    if (m_bigVideoTrack) {
        LOG_INFO("add m_videoTrack");
        ret = m_peerConnection->AddTransceiver(m_bigVideoTrack, init);
        m_videoTransceiver = ret.value();
    }
    else {
        LOG_ERROR("end! m_videoTrack is null");
        return false;
    }
//    init.send_encodings[0].max_bitrate_bps = m_maxVideoBitrate * 1000 / 2;
//    init.send_encodings[0].max_framerate = m_curFps;
//    if (m_smallVideoTrack) {
//        LOG_INFO("add m_videoTrack");
//        ret = m_peerConnection->AddTransceiver(m_smallVideoTrack, init);
//        m_smallVideoTransceiver = ret.value();

//        //m_peerConnection->RemoveTrack(m_smallVideoTransceiver->sender());
//        //m_smallVideoTransceiver->StopStandard();
//    }
//    else {
//        LOG_ERROR("end! m_smallVideoTrack is null");
//        return false;
//    }


	webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;

	LOG_INFO("create offer");
	{
		std::unique_lock<std::mutex> locker(m_mutexOffer);
		m_peerConnection->CreateOffer(this, options);
		m_conOffer.wait(locker);
		if (m_Offer.empty()) {
			LOG_ERROR("m_answer is empty!!");
			return false;
		}
	}

	m_pusher->sendOffer(m_Offer);

	LOG_INFO("end!");
	return true;
}

bool PeerConnectionStream::createPeerConnection()
{
	LOG_INFO("begin!");
	RTC_DCHECK(g_peer_connection_factory.get() != nullptr);
	RTC_DCHECK(m_peerConnection.get() == nullptr);

	webrtc::PeerConnectionInterface::RTCConfiguration config_;

	// Add the stun server.
	webrtc::PeerConnectionInterface::IceServer stun_server;
	stun_server.uri = "stun:stun.l.google.com:19302";
	config_.servers.push_back(stun_server);
	config_.sdp_semantics = ::webrtc::SdpSemantics::kUnifiedPlan;
	config_.bundle_policy = ::webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
	config_.rtcp_mux_policy = webrtc::PeerConnectionInterface::kRtcpMuxPolicyRequire;
	config_.type = webrtc::PeerConnectionInterface::kAll;
	config_.tcp_candidate_policy = webrtc::PeerConnectionInterface::TcpCandidatePolicy::kTcpCandidatePolicyEnabled;
	config_.enable_dtls_srtp = true;

	m_peerConnection = g_peer_connection_factory->CreatePeerConnection(config_, nullptr, nullptr, this);
	LOG_INFO("end!");
	return m_peerConnection.get() != nullptr;
	
}

void PeerConnectionStream::deletePeerConnection()
{
	LOG_INFO("begin!");
	if (m_peerConnection) {
		LOG_INFO("come here");
		enableVideo(false);
		enableAudio(false);

		if (m_audioTransceiver) {
			//m_audioTransceiver->Stop();
			m_audioTransceiver->StopInternal();
			m_audioTransceiver = nullptr;
		}
		if (m_videoTransceiver) {
			//m_videoTransceiver->Stop();
			m_videoTransceiver->StopInternal();
			m_videoTransceiver = nullptr;
		}
		m_peerConnection->Close();
		m_peerConnection = nullptr;
	}
	LOG_INFO("end!");
}

void PeerConnectionStream::reGenPeerConnection()
{
	//要放在worker里调用
	LOG_INFO("begin!");
	deletePeerConnection();
	createPeerConnection();
	LOG_INFO("end!");
}

bool PeerConnectionStream::enableVideo(bool enable)
{
	LOG_INFO("begin! enable: " << enable);
	if (!m_videoTransceiver || !m_bigVideoTrack || !m_smallVideoTransceiver) {
		LOG_INFO("end! m_videoTransceiver or m_videoTrack is null");
		return false;
	}

	if (enable) {
		m_videoTransceiver->sender()->SetTrack(m_bigVideoTrack);
		m_smallVideoTransceiver->sender()->SetTrack(m_smallVideoTrack);
	}
	else {
		m_videoTransceiver->sender()->SetTrack(nullptr);
		m_smallVideoTransceiver->sender()->SetTrack(nullptr);
		m_bigVideoTrack = nullptr;
		m_smallVideoTrack = nullptr;
		if (m_localVideoTrackSource) {
			closeVideoSource();
		}
	}
	LOG_INFO("end!");
	return true;
}

bool PeerConnectionStream::enableAudio(bool enable)
{
	LOG_INFO("begin! enable: " << enable);
	if (!m_audioTransceiver || !m_audioTrack) {
		LOG_INFO("end! m_audioTransceiver or m_audioTrack is null");
		return false;
	}

	if (enable) {
		m_audioTransceiver->sender()->SetTrack(m_audioTrack);
	}
	else {
		m_audioTransceiver->sender()->SetTrack(nullptr);
		m_audioTrack = nullptr;
	}
	LOG_INFO("end!");
	return true;
}

int PeerConnectionStream::muteAudio(bool mute)
{
	LOG_INFO("begin! mute: " << mute);
	if (m_audioTrack) {
		m_audioTrack->set_enabled(!mute);
	}
	return 0;
}

int PeerConnectionStream::muteVideo(bool mute)
{
	LOG_INFO("begin! mute: " << mute);
	if (m_bigVideoTrack) {
		m_bigVideoTrack->set_enabled(!mute);
	}
	LOG_INFO("end!");
	return 0;
}

int PeerConnectionStream::enableVideoCapture(bool enable)
{
	LOG_INFO("begin! enable: " << enable);
	if (m_isLocalVideoEnable == enable) {
		LOG_INFO("repead");
		return false;
	}

	if (enable) {
		if (!openVideoSource(true)) {
			LOG_ERROR("openVideoSource failed!");
		}
	}
	else {
		enableVideo(false);
	}
	m_isLocalVideoEnable = enable;
	return 0;
}

int PeerConnectionStream::enableAudioCapture(bool enable)
{
	LOG_INFO("begin! enable: " << enable);
	if (m_isLocalAudioEnable == enable) {
		LOG_INFO("m_isLocalAudioEnable repeat ");
		return false;
	}

	if (enable) {
		if (!openAudioSource()) {
			LOG_ERROR("openLocalAudio failed!");
		}
	}
	else {
		enableAudio(false);
	}
	m_isLocalAudioEnable = enable;
	return 0;
}

bool PeerConnectionStream::publishStream()
{
	LOG_INFO("begin!");

    if (!m_bigVideoTrack && !openVideoSource(m_isLocalVideoEnable)) {
        LOG_ERROR("end! openVideoSource failed");
        return false;
    }

	if (!m_audioTrack && !openAudioSource()) {
		LOG_ERROR("end! openAudioSource failed");
		return false;
	}

	if (!AddTransceiverAndCreateOfferForSend()) {
		LOG_ERROR("end! AddTransceiverAndCreateOfferForSend error");
		return false;
	}
	return true;
}

int PeerConnectionStream::startPreview(WindowIdType winId)
{
	if (winId == nullptr) {
		LOG_ERROR("winId is null");
		return -1;
	}
	LOG_INFO("begin! winId: " << winId);
	if (!m_videoSink.setVideoWindow(winId)) {
		LOG_ERROR("setVideoWindow failed");
		return -2;
	}
	m_videoSink.StartRenderer();
	LOG_INFO("end!");
	return 0;
}

void PeerConnectionStream::stopPreview()
{
	LOG_INFO("begin!");
	m_videoSink.StopRenderer();
	LOG_INFO("end!");
}

void PeerConnectionStream::sendVideoBuffer(const char* data, int width, int height, ColorSpace color)
{
	if (data == nullptr || width <= 0 || height <= 0) {
		LOG_ERROR("invalidate data: " << data << " width: " << width << " height: " << height);
		return;
	}

	ExternalVideoFrameTrackSource* trackSource =
		dynamic_cast<ExternalVideoFrameTrackSource*>(m_smallVideoTrackSource.get());
	if (trackSource) {
		ExternalVideoFrame sampleBuffer;
		externalVideoFrame2YuvI420((void*)data, width, height, rtc::TimeMillis(), color, sampleBuffer);
		trackSource->pushExternalVideoFrame(&sampleBuffer);
	}
}

void PeerConnectionStream::setRemoteSdp(const std::string &sdp)
{
	LOG_INFO("remote sdp: " << sdp);

	webrtc::SdpParseError error;
	webrtc::SessionDescriptionInterface* session_description(
		webrtc::CreateSessionDescription("answer", sdp, &error));
	if (!session_description) {
		LOG_ERROR("Can't parse received session description message. "
			<< "SdpParseError was: " << error.description);
		return;
	}
	m_peerConnection->SetRemoteDescription(
		DummySetSessionDescriptionObserver::Create(), session_description);
}

rtc::scoped_refptr<webrtc::AudioTrackInterface> PeerConnectionStream::getAudioTrack()
{
	return m_audioTrack;
}

rtc::scoped_refptr<webrtc::VideoTrackInterface> PeerConnectionStream::getVideoTrack()
{
	return m_bigVideoTrack;
}

rtc::scoped_refptr<webrtc::VideoTrackInterface> PeerConnectionStream::getSmallVideoTrack()
{
	return m_smallVideoTrack;
}

std::string PeerConnectionStream::id() const
{
	return m_meidaStream->id();
}

void PeerConnectionStream::OnFrame(const uint8_t* data_y, const uint8_t* data_u, const uint8_t* data_v, const uint8_t* data_a, int stride_y, int stride_u, int stride_v, int stride_a, uint32_t width, uint32_t height, int64_t render_time)
{
	//planar yuv
	if (!m_uBuffer || m_curWidth != width || m_curHeight != height) {
		if (m_uBuffer)
			delete[] m_uBuffer;
		m_curWidth = width;  //width of video frame
		m_curHeight = height;  //height of video frame
		m_uBuffer = new uint8_t[width * height * 3 / 2];
	}
	int nYUVBufsize = 0;
	int nVOffset = 0;
	for (int i = 0; i < height; ++i) {
		memcpy(m_uBuffer + nYUVBufsize, data_y + i * stride_y, width);
		nYUVBufsize += width;
	}
	for (int i = 0; i < (height >> 1); ++i) {
		memcpy(m_uBuffer + nYUVBufsize, data_u + i * stride_u, width >> 1);
		nYUVBufsize += (width >> 1);
		memcpy(m_uBuffer + width * height * 5 / 4 + nVOffset, data_v + i * stride_v, width >> 1);
		nVOffset += (width >> 1);
	}

	m_videoSink.renderYuv(m_uBuffer, width, height);

	if (m_isLocalStream) {
		sendVideoBuffer((const char*)m_uBuffer, width, height, Color_YUVI420);
	}
}

void PeerConnectionStream::OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver, const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams)
{
	rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track =
		receiver->track();

	if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
		//有二个视频流，默认接收第一流
		std::string streamId = streams[0]->id();
		if (!m_bigVideoTrack) {
			m_bigVideoTrack = static_cast<webrtc::VideoTrackInterface*>(track.get());
			m_bigVideoTrack->AddOrUpdateSink(m_meidaStream->videoObserver(),
				rtc::VideoSinkWants());
		}

	}
	else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
		m_audioTrack = static_cast<webrtc::AudioTrackInterface*>(track.get());
		m_audioTrack->AddSink(m_meidaStream->audioObserver());
	}
}

void PeerConnectionStream::OnSuccess(webrtc::SessionDescriptionInterface* desc)
{

	{
		std::unique_lock<std::mutex> locker(m_mutexOffer);
		m_conOffer.notify_one();
		if (!desc->ToString(&m_Offer)) {
			LOG_ERROR("error ToString");
			m_Offer.clear();
			return;
		}
	}
	LOG_INFO("SetLocalDescription")
		m_peerConnection->SetLocalDescription(
			DummySetSessionDescriptionObserver::Create(), desc);

	LOG_INFO("success for " << desc->type() << " , sdp: " << m_Offer);
}

void PeerConnectionStream::OnFailure(webrtc::RTCError error)
{
	LOG_INFO("failure for Create sdp: " << error.message());
}

void PeerConnectionStream::OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state)
{
	LOG_INFO("new_state :" << (PEERCONNECTIONSTATE)new_state);
	switch (new_state)
	{
	case webrtc::PeerConnectionInterface::PeerConnectionState::kNew:
		break;
	case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
		break;
	case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
		break;
	case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
		break;
	case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
		break;
	case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
		break;
	default:
		break;
	}

}

void PeerConnectionStream::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state)
{
	LOG_INFO("new_state :" << new_state);
}

void PeerConnectionStream::OnIceCandidate(const webrtc::IceCandidateInterface* candidate)
{
	std::string candidateStr;
	if (!candidate->ToString(&candidateStr)) {
		LOG_ERROR("Failed to serialize candidate");
		return;
	}
	LOG_INFO("candidateStr: " << candidateStr.c_str());
}
