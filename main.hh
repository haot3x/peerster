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

 
class ChatDialog : public QDialog
{
	Q_OBJECT

public:
	ChatDialog();

public slots:
	void gotReturnPressed();
    void gotRecvMessage();
    void fwdMessage(QString fwdInfo);

private:
	QTextEdit *textview;
	QTextEdit *textedit;
    NetSocket *sockRecv;
    bool eventFilter(QObject *obj, QEvent *ev);
    int randomOriginID;
    QVariantMap *recvMessageMap;
    QVariantMap *updateStatusMap;
    quint32 SeqNo;
    QTimer *timerForAck;
    QVector<QString> *ackHist; // Acknowledgement, namely Status Message, History
};

   

#endif // PEERSTER_MAIN_HH
