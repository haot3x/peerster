#ifndef PEER_HH
#define PEER_HH

#include "main.hh"

// ----------------------------------------------------------------------
// Peer class for storage
class Peer {
public:
    Peer(QString h, QHostAddress i, quint16 p): hostname(h), ipaddr(i), port(p) {}
    QString getHostname() const {
        return hostname;
    }
    QHostAddress getIP() const {
        return ipaddr;
    }
    quint16 getPort() const {
        return port;
    }

private:
    QString hostname;
    QHostAddress ipaddr;
    quint16 port;
};


#endif
