#ifndef __STOP_AND_WAIT_H__
#define __STOP_AND_WAIT_H__
#include <interfaces/packet.h>

typedef enum ErrorHandler{
	NO_ERROR,
	IO_ERROR,
	NO_LINK,
	HAVE_BEACON,
}ErrorHandler;

typedef enum ProtocolEstablishmentEvent{
	initialise_link,
	check_link_availability,
	desconnect_link,
}ProtocolEstablishmentEvent;

typedef enum ProtocolControlEvent{
	control_packet,
	beacon_send,
	beacon_recv,
}ProtocolControlEvent;

ErrorHandler protocol_establishment_routine (ProtocolEstablishmentEvent event, Control * c, Status * s);
ErrorHandler protocol_control_routine (ProtocolControlEvent event, Control * c, Status * s);
ErrorHandler StopAndWait(Control * c, Status * s);

#endif