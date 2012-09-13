#ifndef PEERSTER_MAIN_HH
#define PEERSTER_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QUdpSocket>
#include <QPushButton>// Tian added for click to send message
#include <QKeySequence>// Tian added for shortcuts
#include <QShortcut>// Tian added for shortcuts
#include <QString>
#include <QDataStream>
#include <QVariant>
#include <QHostAddress>
#include <QKeyEvent>
#include <QHostInfo>
#include <QtGlobal>
#include <QDateTime>
#include <QTimer>
#include <QVector>
#include <QSignalMapper>
#include <QThread>
#include <QMapIterator>
#include <QLineEdit>
#include <QListView>
#include <QStringListModel>
#include <QList>
#include <QCoreApplication>

class NetSocket : public QUdpSocket
{
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

class Peer 
{
public:
    Peer(QString h, QHostAddress i, quint16 p): hostname(h), ipaddr(i), port(p) {}
    QString hostname;
    QHostAddress ipaddr;
    quint16 port;
};


class ChatDialog : public QDialog
{
	Q_OBJECT

public:
	ChatDialog();

public slots:
	void gotReturnPressed();
    void gotRecvMessage();
    void fwdMessage(QString fwdInfo);
    void antiEntropy();
    void addrPortAdded();
    void lookedUp(const QHostInfo& host);
    void lookedUpBeforeInvoke(const QHostInfo& host);

private:
	QTextEdit *textview;
	QTextEdit *textedit;
    QLineEdit *addAddrPort;
    QListView *addrPortListView;
    NetSocket *sockRecv;
    bool eventFilter(QObject *obj, QEvent *ev);
    int randomOriginID;
    QVariantMap *recvMessageMap;
    QVariantMap *updateStatusMap;
    quint32 SeqNo;
    QString *myOrigin;
    QTimer *timerForAck;
    QTimer *timerForAntiEntropy;
    QVector<QString> *ackHist; // Acknowledgement, namely Status Message, History
    QStringList addrPortStrList;
    QList<Peer> *peerList;
};

#endif // PEERSTER_MAIN_HH
