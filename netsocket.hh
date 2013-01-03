#ifndef NETSOCKET_HH
#define NETSOCKET_HH

#include "main.hh"

// ----------------------------------------------------------------------
// upd socket
class NetSocket : public QUdpSocket {
	Q_OBJECT

public:
	NetSocket();
    int getMyPortMin() { return myPortMin;}
    int getMyPortMax() { return myPortMax;}
    int getMyPort() { return myPort;}

	// Bind this socket to a Peerster-specific default port.
	bool bind();

private:
	int myPort, myPortMin, myPortMax;
};

#endif
