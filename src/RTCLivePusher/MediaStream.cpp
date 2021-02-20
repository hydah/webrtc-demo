#include <utility>
#include "peerconnectionStream.h"
#include "DeviceManager.h"
#include "MediaStream.h"
#include "RTCLivePusher.h"

 MediaStreamImpl::MediaStreamImpl(const std::string& id, PeerConnectionStream *peerconnect)
    : m_id(id), m_peerconnect(peerconnect){}
 
 MediaStreamImpl::~MediaStreamImpl() 
 {
	 if (m_peerconnect) {

		 auto audioTrack = m_peerconnect->getAudioTrack();
		 if (audioTrack) {
			 audioTrack->RemoveSink(this);
		 }

		 auto videoTrack = m_peerconnect->getVideoTrack();
		 if (videoTrack) {
			 videoTrack->RemoveSink(&m_dataObserver);
		 }
	 }
  

   
 }


const char* MediaStreamImpl::id() const
{
   return m_id.c_str();
 }