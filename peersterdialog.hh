#ifndef PEERSTERDIALOG_HH
#define PEERSTERDIALOG_HH

#include "main.hh"

// ----------------------------------------------------------------------
class PeersterDialog : public QDialog {
	Q_OBJECT;
    friend class PrivateMessage;
public:
	PeersterDialog();
	~PeersterDialog();

public slots:
    void gotRecvMessage();

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
    QLabel *gossipTitle;
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
    //    < QString "Host"  , QString host      >
    //    < QString "Port"  , quint16 port      >
    //    < QString "Budget", int budget        >
    QQueue<QVariantMap> *gossipQueue;

    quint32 SeqNo;
    QTimer *timerForAck;
    QTimer *timerForRM;
    QTimer *timerForAntiEntropy;

    QStringList addrPortStrList;

    // store direct neighbors
    QList<Peer> *peerList;

    // list view for all NIDs
    QListView *originListView;
    QStringList originStrList;

    // store the new IP and port temp waiting for looking for update 
    // if they exist, they will be added to the direct neighbors list view
    QString newIP;
    quint16 newPort;

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

#endif
