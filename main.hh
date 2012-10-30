#ifndef PEERSTER_MAIN_HH
#define PEERSTER_MAIN_HH

// ----------------------------------------------------------------------
// include header
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
#include <QQueue>


// ----------------------------------------------------------------------
// class declaration
class NetSocket;
class Peer;
class PeersterDialog;
class PrivateMessage;
class FileMetaData;

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

// ----------------------------------------------------------------------
class PeersterDialog : public QDialog {
	Q_OBJECT;
    friend class PrivateMessage;
public:
	PeersterDialog();
	~PeersterDialog();

public slots:
    void gotRecvMessage();

    // deprecated
    void fwdMessage(QString fwdInfo);

    // gossip messaging
	void gotReturnPressed();
    void sendStatusMsg(const QString origin, const quint32 seqNo, const QHostAddress host, const quint16 port);
    void sendGossipMsg(const QString origin, const quint32 seqNo, const QHostAddress host, const quint16 port);
    void antiEntropy();
    void addrPortAdded();
    void lookedUp(const QHostInfo& host);
    void lookedUpBeforeInvoke(const QHostInfo& host);
    void updateGossipQueue();
    bool updateGossipQueue(const QString origin, const quint32 seqNo, const QString host, const quint16 port); // delete special element

    // point to point messaging
    void sendRoutingMsg(const QString origin, const quint32 seqNo, const QHostAddress host, const quint16 port);
    void broadcastRM();
    void openPrivateMessageWin(const QModelIndex&);

    // file sharing
    void onShareFileBtnClicked();
    void onRequestFileBtnClicked();
    void sendBlockRequest(const QString dest, const QString origin, const quint32 hopLimit, const QByteArray &blockRequest);
    void sendBlockReply(const QString dest, const QString origin, const quint32 hopLimit, const QByteArray &blockReply, const QByteArray &data);
    void onSearchFileBtnClicked();
    void sendSearchRequest(const QString origin, const QString search, const quint32 budget, QHostAddress host, quint16 port);
    void sendSearchReply(const QString dest, const QString origin, const quint32 hopLimit, const QString searchReply, const QVariantList matchNames, const QVariantList matchIDs);

    // updateSearchQueue 
    // update the QQueue<QPair<QString, quint32> > *searchQueue for sending search request <QString keyWords, quint32 budget>
    // it will be called once a second until every element is gone when its the budget exceeds 100
    void updateSearchQueue();
    void downloadFile(const QModelIndex&);


private:
    // gossip messaging -------------------------------------------------
	QGridLayout *layout;
    // send message when returen pressed
    bool eventFilter(QObject *obj, QEvent *ev); 
    QTextEdit *textview;
	QTextEdit *textedit;

    // Input peer information
    QLineEdit *addAddrPort;

    QListView *addrPortListView;
    NetSocket *sockRecv;
    QString *myOrigin; // my NID (node identifier)

    // recvMessageMap: store all the coming messages 
    //              ======================
    //   QVariantMap<QString, QVariantMap>
    //              +-------+------------+
    //              |NID.Seq| message    |
    //              |NID.Seq| message    |
    //                       ...
    //              |NID.Seq| message    |
    //              ======================
    QVariantMap *recvMessageMap;

    // updateStatusMap: store NID and seqence NO of gossip messages
    //              ==============
    //   QVariantMap<QString, int>
    //              +-------+----+
    //              | NID   | Seq|
    //              | NID   | Seq|
    //                   ...
    //              | NID   | Seq|
    //              ==============
    QVariantMap *updateStatusMap;

    // queue for my gossip messages having been sent to others without receiving acknowledgment 
    // QVariantMap 
    //    < QString "Origin", QString origin    >
    //    < QString "SeqNo" , quint32 seqNo     >
    //    < QString "Host"  , QString host >
    //    < QString "Port"  , quint16 port      >
    //    < QString "Budget", int budget        >
    QQueue<QVariantMap> *gossipQueue;

    quint32 SeqNo;
    QTimer *timerForAck;
    QTimer *timerForRM;
    QTimer *timerForAntiEntropy;

    // Used to record the ack to stop resending
    QVector<QString> *ackHist; // Acknowledgement, namely Status Message, History
    QStringList addrPortStrList;

    // store direct neighbors
    QList<Peer> *peerList;

    // list view for all NIDs
    QListView *originListView;
    QStringList originStrList;




    // point to point messaging -----------------------------------------
    bool isNoForward;
	PrivateMessage *pm;
    // updateRoutOriSeqMap: store Origin and SeqNo of incoming message
    //              ==================
    //   QVariantMap<QString, quint32>
    //              +-------+--------+
    //              |Node ID| SeqNo  |
    //              |Node ID| SeqNo  |
    //                  ...
    //              |Node ID| SeqNo  |
    //              ==================
    QVariantMap *updateRoutOriSeqMap;
    quint32 routMessSeqNo;

    // nextHopTable: next hop table is used to store all direct neighbors
    //        =========================================
    //   QHash<QString , QPair<QHostAddress, quint16> >
    //        +--------+-------------------+----------+
    //        |Node ID |        Node IP    | Node Port|
    //        |Node ID |        Node IP    | Node Port|
    //         ...
    //        |Node ID |        Node IP    | Node Port|
    //        =========================================
    QHash<QString, QPair<QHostAddress, quint16> > *nextHopTable;

    // information about the last node who send me the message
    QHostAddress* lastIP;
    quint16 lastPort;

    QLabel *destListLabel;
    QLabel *neighborListLabel;

    // file sharing in column 3 -----------------------------------------
    QPushButton *shareFileBtn;
    QPushButton *requestFileBtn;
    QVector<FileMetaData*> filesMetas;
    QVector<FileMetaData*> recvFilesMetas;
    QLineEdit *targetNID;
    QLineEdit *targetFID;
    QLabel *searchLabel;
    QLineEdit *searchKeyWords;
    QPushButton *searchFileBtn;
    QQueue<QPair<QString, quint32> > *searchQueue; // queue for sending search request <QString keyWords, quint32 budget>
    QTimer *searchTimer;
    // list view for the search results
    QListView *searchResultsListView;
    QStringList *searchResultsStringList;
    QStringListModel *searchResultsStringListModel;
    QVariantMap *searchResults;
};

// ----------------------------------------------------------------------
// private message window
class PrivateMessage: public QDialog {
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
class FileMetaData {
public:
    FileMetaData(const QString fn); // init according to a file path
    FileMetaData(const QString name, const QByteArray FID, const QString NID):
        fileNameOnly(name), blockListHash(FID), originNID(NID) {}; // init according recv info 
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
    int getSubFilesNum() const {
        return blockList.size() / 32;
    }
    bool contains(QByteArray hash) { // contains FID ?
        if (blockList.indexOf(hash) == -1 && hash != blockListHash) return false;
        else return true;
    }
    bool contains(QString keyWords) const ; // overload for search key words
    QString getFilePath(QByteArray hash) const {
        if ( hash == blockListHash) 
            return metaFileName;
        else 
            return subFileNameList.at((blockList.indexOf(hash)/32));
    }
    QString getOriginNID() const {
        return originNID;
    }
    void fillBlockList(QByteArray &data) {
        blockList = data;
        //qDebug() << "getSubFilesNum " << getSubFilesNum();
        // subFileNameList.reserve(getSubFilesNum());
        for (int i = 0; i < getSubFilesNum(); ++i )
            subFileNameList.append("");
    }
    QString getSubFilePath(int index) const {
        return subFileNameList.at(index);
    }
    void setSubFilePath(const int index, QString path) {
        //qDebug() << index << " " << path;
        subFileNameList.replace(index, path);
    }
    void setMetaFilePath(QString path) {
        metaFileName = path;
    }
    void uniteFile(const QString outDir, const int blockSize);

private:
    void splitFile(const QString outDir, const int blockSize);

private:
    QString fileNameWithPath;
    QString fileNameOnly;
    quint64 size;
    QByteArray blockList;
    QByteArray blockListHash; // File ID
    QStringList subFileNameList; // path of blockHash0, blockHash1, .., blockHashN
    QString metaFileName; // hash file path of File ID
    QString originNID;
};


#endif // PEERSTER_MAIN_HH
