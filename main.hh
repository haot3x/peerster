#ifndef PEERSTER_MAIN_HH
#define PEERSTER_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QUdpSocket>
#include <QPushButton>
#include <QKeySequence>
#include <QShortcut>
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
#include <QVBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QtCrypto>
#include <QIODevice>


class NetSocket;
class Peer;
class TabDialog;
class GossipMessaging;
class GossipMessagingEntry;
class PointToPointMessaging;
class FileSharing;
class PointToPointMessagingEntry;
class PrivateMessage;

// ----------------------------------------------------------------------
// upd socket
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

// ----------------------------------------------------------------------
// Peer class for storage
class Peer 
{
public:
    Peer(QString h, QHostAddress i, quint16 p): hostname(h), ipaddr(i), port(p) {}
    QString hostname;
    QHostAddress ipaddr;
    quint16 port;
};

// ----------------------------------------------------------------------
// main window
class TabDialog : public QDialog
{
    Q_OBJECT;

public:
    TabDialog(QWidget* parent = 0);
    ~TabDialog();

private:
    QTabWidget *tabWidget;
    FileSharing *fs;
    PointToPointMessagingEntry *p2pEntry;
    GossipMessagingEntry *gmEntry;
};


// ----------------------------------------------------------------------
// Gossip Messging Tab Contents
class GossipMessaging : public QWidget 
{
	Q_OBJECT;

public:
	GossipMessaging(QWidget* parent = 0);
    ~GossipMessaging();

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

// ----------------------------------------------------------------------
class PointToPointMessagingEntry : public QWidget
{
    Q_OBJECT;

public:
    PointToPointMessagingEntry(QWidget* parent = 0);

private:
    QPushButton* switchButton;
    PointToPointMessaging * p2p;
    QVBoxLayout* layout;
    
public slots:
    void switchButtonClicked();
};


// ----------------------------------------------------------------------
class GossipMessagingEntry : public QWidget
{
    Q_OBJECT;

public:
    GossipMessagingEntry(QWidget* parent = 0);

private:
    QPushButton* switchButton;
    GossipMessaging* gm;
    QVBoxLayout* layout;
    
public slots:
    void switchButtonClicked();
};

// ----------------------------------------------------------------------
class FileSharing : public QWidget 
{
	Q_OBJECT;
public:
	FileSharing(QWidget* parent = 0);
	~FileSharing();

public slots:
    void onShareFileBtnClicked();
    void splitFile(const QString fin, const QString outDir, const int blockSize);
    /*
	void gotReturnPressed();
    void gotRecvMessage();
    void fwdMessage(QString fwdInfo);
    void antiEntropy();
    void broadcastRM();
    void addrPortAdded();
    void lookedUp(const QHostInfo& host);
    void lookedUpBeforeInvoke(const QHostInfo& host);
    void openPrivateMessageWin(const QModelIndex&);
    */

private:
    QPushButton *shareFileBtn;
    QGridLayout *layout;
    
    /*
    NetSocket *sockRecv;
    int randomOriginID;
    QVariantMap *recvMessageMap;
    QVariantMap *updateStatusMap;
    QVariantMap *updateRoutOriSeqMap;
    quint32 SeqNo;
    quint32 routMessSeqNo;
    QString *myOrigin;
    QTimer *timerForAck;
    QTimer *timerForRM;
    QTimer *timerForAntiEntropy;
    QVector<QString> *ackHist; // Acknowledgement, namely Status Message, History
    QStringList addrPortStrList;
    QList<Peer> *peerList;

    QListView *originListView;
    QStringList originStrList;

    QHash<QString, QPair<QHostAddress, quint16> > *nextHopTable;

    QHostAddress* lastIP;
    quint16 lastPort;
    */
};


// ----------------------------------------------------------------------
class PointToPointMessaging : public QWidget 
{
	Q_OBJECT;
    friend class PrivateMessage;
public:
	PointToPointMessaging(QWidget* parent = 0);
	~PointToPointMessaging();

public slots:
	void gotReturnPressed();
    void gotRecvMessage();
    void fwdMessage(QString fwdInfo);
    void antiEntropy();
    void broadcastRM();
    void addrPortAdded();
    void lookedUp(const QHostInfo& host);
    void lookedUpBeforeInvoke(const QHostInfo& host);
    void openPrivateMessageWin(const QModelIndex&);

private:
    bool isNoForward;
    bool eventFilter(QObject *obj, QEvent *ev);
	PrivateMessage *pm;
    QTextEdit *textview;
	QTextEdit *textedit;
    QLineEdit *addAddrPort;
    QListView *addrPortListView;
    NetSocket *sockRecv;
    int randomOriginID;
    QVariantMap *recvMessageMap;
    QVariantMap *updateStatusMap;
    QVariantMap *updateRoutOriSeqMap;
    quint32 SeqNo;
    quint32 routMessSeqNo;
    QString *myOrigin;
    QTimer *timerForAck;
    QTimer *timerForRM;
    QTimer *timerForAntiEntropy;
    QVector<QString> *ackHist; // Acknowledgement, namely Status Message, History
    QStringList addrPortStrList;
    QList<Peer> *peerList;

    QListView *originListView;
    QStringList originStrList;

    QHash<QString, QPair<QHostAddress, quint16> > *nextHopTable;

    QHostAddress* lastIP;
    quint16 lastPort;
};

// ----------------------------------------------------------------------
class PrivateMessage: public QDialog
{
    Q_OBJECT;
    friend class PointToPointMessaging;

public:
    PrivateMessage(const QModelIndex& index, PointToPointMessaging* p2p);

public slots:
	void gotReturnPressed();

private:
    QString dest;
	QTextEdit *textview;
	QTextEdit *textedit;
    bool eventFilter(QObject *obj, QEvent *ev);
    PointToPointMessaging* upperP2P;
};


#endif // PEERSTER_MAIN_HH
