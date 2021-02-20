#ifndef PEERCONNECTIONSTREAM_H
#define PEERCONNECTIONSTREAM_H

#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "pc/video_track_source.h"
#include <mutex>
#include "RTCVideoSink.h"

class MediaStreamImpl;
class RTCLivePusher;
class PeerConnectionStream : public webrtc::PeerConnectionObserver,
	                         public webrtc::CreateSessionDescriptionObserver {

public:
	PeerConnectionStream(RTCLivePusher *pusher,bool isLocalStream);
	~PeerConnectionStream();

	void CreateMediaStream();
	bool createVideoTrack(bool capture);
	bool openAudioSource();
	bool openVideoSource(bool capture);
	void createVideoSource(bool capture);
	void releaseVideoSource();
	void closeVideoSource();

	bool AddTransceiverAndCreateOfferForSend();
	bool createPeerConnection();
	void deletePeerConnection();
	void reGenPeerConnection();
	bool enableVideo(bool enable);
	bool enableAudio(bool enable);
	int muteAudio(bool mute);
	int muteVideo(bool mute);
	int enableVideoCapture(bool enable);
	int enableAudioCapture(bool enable);
	bool publishStream();
	int startPreview(WindowIdType winId);
	void stopPreview();

	void sendVideoBuffer(const char* data, int width, int height, ColorSpace color);
	void setRemoteSdp(const std::string &sdp);

	rtc::scoped_refptr<webrtc::AudioTrackInterface> getAudioTrack();
	rtc::scoped_refptr<webrtc::VideoTrackInterface> getVideoTrack();
	rtc::scoped_refptr<webrtc::VideoTrackInterface> getSmallVideoTrack();
	std::string id() const;


	void OnFrame(const uint8_t* data_y, 
		const uint8_t* data_u, 
		const uint8_t* data_v,
		const uint8_t* data_a, 
		int stride_y, 
		int stride_u, 
		int stride_v, 
		int stride_a, 
		uint32_t width, 
		uint32_t height, 
		int64_t render_time);

protected:
	// PeerConnectionObserver implementation.
	void OnSignalingChange(
		webrtc::PeerConnectionInterface::SignalingState new_state) override {}
	void OnAddStream(
		rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {}
	void OnRemoveStream(
		rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {}

	void OnAddTrack(
		rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
		const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>&
		streams) override;
	void OnRemoveTrack(
		rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}
	void OnDataChannel(
		rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
	void OnRenegotiationNeeded() override {}
	// remote
	void OnIceConnectionChange(
		webrtc::PeerConnectionInterface::IceConnectionState new_state) override {}
	virtual void OnConnectionChange(
		webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
	// local ice info
	void OnIceGatheringChange(
		webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
	// local candidate
	void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
	void OnIceConnectionReceivingChange(bool receiving) override {}

	// CreateSessionDescriptionObserver implementation.
	void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
	void OnFailure(webrtc::RTCError error) override;

private:
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> m_peerConnection;
	std::unique_ptr<MediaStreamImpl> m_meidaStream;
	// video source
	rtc::scoped_refptr<webrtc::VideoTrackSource> m_localVideoTrackSource = nullptr;
	rtc::scoped_refptr<webrtc::VideoTrackSource> m_smallVideoTrackSource = nullptr;
	rtc::scoped_refptr<::webrtc::RtpTransceiverInterface> m_audioTransceiver;
	rtc::scoped_refptr<::webrtc::RtpTransceiverInterface> m_videoTransceiver;
	rtc::scoped_refptr<::webrtc::RtpTransceiverInterface> m_smallVideoTransceiver;
	rtc::scoped_refptr<webrtc::AudioTrackInterface> m_audioTrack;
	rtc::scoped_refptr<webrtc::VideoTrackInterface> m_bigVideoTrack;
	rtc::scoped_refptr<webrtc::VideoTrackInterface> m_smallVideoTrack;

	//for create offer
	std::mutex m_mutexOffer;
	std::condition_variable m_conOffer;
	std::string m_Offer;
	bool m_isLocalStream;
	RtcVideoSink m_videoSink;
	RTCLivePusher *m_pusher;

	int m_curWidth = 640;
	int m_curHeight = 480;
	int m_curFps = 15;
	int m_maxVideoBitrate = 500;
	int m_curVideoDeviceIndex = 0;

	bool m_isLocalVideoEnable = false;
	bool m_isLocalAudioEnable = false;
	bool m_isExternalVideoEnabled = false;
	bool m_isExternalAudioEnabled = false;

	uint8_t *m_uBuffer = nullptr;
};
#endif
