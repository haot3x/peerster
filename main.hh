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
class PeersterDialog;
class PrivateMessage;
class FileMetaData;

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


// ----------------------------------------------------------------------
class PeersterDialog : public QDialog
{
	Q_OBJECT;
    friend class PrivateMessage;
public:
	PeersterDialog(QWidget* parent = 0);
	~PeersterDialog();

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
    // File Sharing
    void onShareFileBtnClicked();
    void onRequestFileBtnClicked();
    void sendBlockRequest(const QString dest, const QString origin, const quint32 hopLimit, const QByteArray &blockRequest);


private:
	QGridLayout *layout;
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
    // file sharing
    QPushButton *shareFileBtn;
    QVector<FileMetaData*> filesMetas;
    QLineEdit *targetNID;
    QLineEdit *targetFID;
};

// ----------------------------------------------------------------------
class PrivateMessage: public QDialog
{
    Q_OBJECT;
    friend class PeersterDialog;

public:
    PrivateMessage(const QModelIndex& index, PeersterDialog* p2p);

public slots:
	void gotReturnPressed();

private:
    QString dest;
	QTextEdit *textview;
	QTextEdit *textedit;
    bool eventFilter(QObject *obj, QEvent *ev);
    PeersterDialog* upperP2P;
};

// ----------------------------------------------------------------------
// to store info about a sharing file 
class FileMetaData
{
public:
    FileMetaData(const QString fn);
    QString getFileNameWithPath() const {
        return fileNameWithPath;
    }
    QString getFileNameOnly() const {
        return fileNameOnly;
    }
    quint64 getSize() const {
        return size;
    }
    QByteArray getBlockList() const {
        return blockList;
    }
    QByteArray getBlockListHash() const {
        return blockListHash;
    }
    void splitFile(const QString outDir, const int blockSize);

private:
    QString fileNameWithPath;
    QString fileNameOnly;
    quint64 size;
    QByteArray blockList;
    QByteArray blockListHash; // File ID
};


#endif // PEERSTER_MAIN_HH
