#include <unistd.h>

#include <QDebug>

#include "main.hh"

// ---------------------------------------------------------------------
// udp socket constructor 
NetSocket::
NetSocket() {
	// Pick a range of four UDP ports to try to allocate by default,
	// computed based on my Unix user ID.
	// This makes it trivial for up to four Peerster instances per user
	// to find each other on the same host,
	// barring UDP port conflicts with other applications
	// (which are quite possible).
	// We use the range from 32768 to 49151 for this purpose.
	myPortMin = 32768 + (getuid() % 4096)*4;
	myPortMax = myPortMin + 3;
}

// ---------------------------------------------------------------------
// bind the socket with a port
bool NetSocket::
bind() {
	// Try to bind to each of the range myPortMin..myPortMax in turn.
	for (int p = myPortMin; p <= myPortMax; p++) {
		if (QUdpSocket::bind(p)) {
            myPort = p;
			qDebug() << "bound to UDP port " << p;
			return true;
		}
	}

	qDebug() << "Oops, no ports in my default range " << myPortMin
		<< "." << myPortMax << " available";
	return false;
}

// ---------------------------------------------------------------------
// PeersterDialog window constructor
PeersterDialog::
PeersterDialog() {
    // onion routing----------------------------------------------------
    seckey = QCA::KeyGenerator().createRSA(1024);
    if(seckey.isNull()) {
        qDebug() << "Failed to make private RSA key";
    }
    pubkey = seckey.toPublicKey();
    // check if the key can encrypt 
    if(!pubkey.canEncrypt()) {
        qDebug() << "Error: this kind of key cannot encrypt";
    }

    // gossip messaging-------------------------------------------------
    gossipTitle = new QLabel("Gossip Chat Room", this);
    // generate myOrigin node ID (NID)
    qsrand(time(0));
    myOrigin = new QString(tr("Tian") + QString::number(qrand()));

    // sequence number for gossip messages
    SeqNo = 1;

    //NetSocket sock;
    sockRecv = new NetSocket();
	if (!sockRecv->bind())
		exit(1);
    connect(sockRecv, SIGNAL(readyRead()),
        this, SLOT(gotRecvMessage()));

	setWindowTitle("Peerster: " + *myOrigin + " (" + sockRecv->myAddress().toString() + ":" + QString::number(sockRecv->getMyPort()) + ")");

    recvMessageMap = new QVariantMap(); 
    updateStatusMap = new QVariantMap(); 

    peerList = new QList<Peer>();

	// read-only text box where we display messages from everyone.
	textview = new QTextEdit(this);
	textview->setReadOnly(true);

    // list view to display DIRECT peers ipaddress and ports
    addrPortListView = new QListView();
    addrPortListView->setModel(new QStringListModel(addrPortStrList));
    addrPortListView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    addAddrPort = new QLineEdit();

    gossipQueue = new QQueue<QVariantMap>;

    // point to point messaging-----------------------------------------
    nextHopTable = new NextHopTable;
    updateRoutOriSeqMap = new QVariantMap();
    routMessSeqNo = 1;

    lastIP = new QHostAddress();
    lastPort = 0;

    // list view to display All dest (DIRECT and INDIRECT) origin list
    originListView = new QListView();
    originListView->setModel(new QStringListModel(originStrList));
    originListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    connect(originListView, SIGNAL(doubleClicked(const QModelIndex& )),
        this, SLOT(openPrivateMessageWin(const QModelIndex& )));
   
    // Process the arguments
    // If there are CLI arguments to set hostname:port or ipaddr:port, add them
    // If there is -noforward, set isNoForward up
    isNoForward = false;
    for (int i = 1; i < qApp->argc(); i++)
    {
        if ("-noforward" == tr(qApp->argv()[i])) 
        {
            qDebug() << "-noforward mode launched!";
            isNoForward = true;
            qDebug() << isNoForward;
        }
        else
        {
            QString inputAddrPort = qApp->argv()[i];
            QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
    
            QHostInfo::lookupHost(addr, 
                this, SLOT(lookedUpBeforeInvoke(QHostInfo)));
        }
    }

    // add one local ports
    for (quint16 q = sockRecv->getMyPortMin(); q <= sockRecv->getMyPortMax() - 2; q++)
    {
        if (q != sockRecv->getMyPort())
        {
            Peer peer(QHostInfo::localHostName(), QHostAddress("127.0.0.1"), q);
            peerList->append(peer);
            addrPortStrList.append(QHostAddress("127.0.0.1").toString() + ":" + QString::number(q));
            ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);
        }
    }

    // send Routing Messages to neighbor peers
    broadcastRM();

    // Input chat message from text edit
	textedit= new QTextEdit(this);
    textedit->installEventFilter(this);

    // Set labels
    destListLabel = new QLabel("All dest(DoubleClick)", this);
    neighborListLabel = new QLabel("    Direct neighbors", this);

    // file sharing-----------------------------------------------------
    shareFileBtn = new QPushButton("Share File...", this);
    shareFileBtn->setAutoDefault(false);
    requestFileBtn = new QPushButton("Request a block", this);
    requestFileBtn->setAutoDefault(false);
    targetNID = new QLineEdit();
    targetFID = new QLineEdit();
    searchLabel= new QLabel("Search", this);
    searchKeyWords= new QLineEdit();
    searchFileBtn = new QPushButton("Search", this);
    searchFileBtn->setAutoDefault(false);
    searchQueue = new QQueue<QPair<QString, quint32> >;
    searchTimer = NULL;
    // search results list view
    searchResultsListView = new QListView();
    searchResultsStringList = new QStringList();
    searchResultsStringListModel = new QStringListModel(*searchResultsStringList);
    searchResultsListView->setModel(searchResultsStringListModel);
    searchResultsListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // connect signal and slot
    connect(shareFileBtn, SIGNAL(clicked()), this, SLOT(onShareFileBtnClicked()));
    connect(requestFileBtn, SIGNAL(clicked()), this, SLOT(onRequestFileBtnClicked()));
    connect(searchFileBtn, SIGNAL(clicked()), this, SLOT(onSearchFileBtnClicked()));
    connect(searchResultsListView, SIGNAL(doubleClicked(const QModelIndex& )), this, SLOT(downloadFile(const QModelIndex& )));
    // if tmp_blocks does not exist, create the temp directory
    if (!QDir(QObject::tr("tmp_blocks/")).exists())
            QDir().mkdir(QObject::tr("tmp_blocks/"));
    if (!QDir(QObject::tr("recv_blocks/")).exists())
            QDir().mkdir(QObject::tr("recv_blocks/"));

	// Lay out the widgets to appear in the main window.----------------
	layout = new QGridLayout();
    // first column 
    layout->addWidget(gossipTitle, 0, 0);
	layout->addWidget(textview, 1, 0, 13, 1);
	layout->addWidget(textedit, 14, 0, 1, 1);
    // second column
    layout->addWidget(destListLabel, 0, 17);
    layout->addWidget(originListView, 1, 17, 4, 1);
    layout->addWidget(neighborListLabel, 5, 17);
    layout->addWidget(addAddrPort, 6, 17); 
    layout->addWidget(addrPortListView, 7, 17, -1, 1);
    // firth column
	layout->addWidget(shareFileBtn, 0, 19);
	layout->addWidget(targetNID, 1, 19);
	layout->addWidget(targetFID, 2, 19);
	layout->addWidget(requestFileBtn, 3, 19);
    layout->addWidget(searchLabel, 5, 19);
    layout->addWidget(searchKeyWords, 6, 19);
    layout->addWidget(searchFileBtn, 7, 19);
    layout->addWidget(searchResultsListView, 8, 19, -1, 1);

    textedit->setFocus();

	setLayout(layout);

    // timer -----------------------------------------------------------
    // Anti-entropy
    // Set timer to send status message 
    timerForAntiEntropy = new QTimer(this);
    connect(timerForAntiEntropy, SIGNAL(timeout()), 
        this, SLOT(antiEntropy()));
    timerForAntiEntropy->start(10000);

    // Periodically send Routing messages
    timerForRM = new QTimer(this);
    connect(timerForRM , SIGNAL(timeout()), 
        this, SLOT(broadcastRM()));
    timerForRM->start(10000);
    timerForAck = NULL;

    // Add hostname:port or ipaddr:port 
    connect(addAddrPort, SIGNAL(returnPressed()),
        this, SLOT(addrPortAdded()));

}

// ---------------------------------------------------------------------
// launch a new window for private message
void PeersterDialog::openPrivateMessageWin(const QModelIndex& index) {
    pm = new PrivateMessage(index, this);
    pm->exec();
} 

// ---------------------------------------------------------------------
// destructor for PeersterDialog
PeersterDialog::
~PeersterDialog() {
    for (QVector<FileMetaData*>::iterator it = filesMetas.begin(); it < filesMetas.end(); it++)
       delete *it;
    for (QVector<FileMetaData*>::iterator it = recvFilesMetas.begin(); it < recvFilesMetas.end(); it++)
       delete *it;
}

// ---------------------------------------------------------------------
// delete special element
bool PeersterDialog::
updateGossipQueue(const QString origin, const quint32 seqNo, const QString host, const quint16 port)  {
    bool isACK = false;
    if (!gossipQueue->isEmpty()) {
        // a new queue for iterating the original queue
        QQueue<QVariantMap> *newQueue = new QQueue<QVariantMap> ;
        while (!gossipQueue->isEmpty()) {
            QVariantMap myGossip = gossipQueue->dequeue();

                if (myGossip.value("Origin").toString() == origin &&
                    myGossip.value("SeqNo").toInt() == (int)seqNo &&
                    myGossip.value("Host").toString() == host &&
                    myGossip.value("Port").toInt() == port)
                    isACK = true; // not enqueue the element
                else 
                    // enqueue the new queue
                    newQueue->enqueue(myGossip);
        }
        delete gossipQueue;
        gossipQueue = newQueue;
    } else {
        delete timerForAck;
        timerForAck = NULL;
    }
    return isACK;
}

// ---------------------------------------------------------------------
// every time the Peester trying to send its new message, it enqueue it into
// gossipQueue. Then this function will be called periodically until the Budget
// exceeds 10 or Peerster received an ACK (status message with a sequence number
// +1)
void PeersterDialog::
updateGossipQueue() {
    if (!gossipQueue->isEmpty()) {
        // resend all the pairs in the queue and add their budget

        // a new queue for iterating the original queue
        QQueue<QVariantMap> *newQueue = new QQueue<QVariantMap > ;
        while (!gossipQueue->isEmpty()) {
            QVariantMap myGossip = gossipQueue->dequeue();
            if (myGossip.value("Budget").toInt() > 10) {
            // if the budget exceeds 10, do nothing and do not store into the new queue
            // do not waste time sending my gossip message to a guy not responding to me more than 10 times
            } else {
                myGossip.insert("Budget", myGossip.value("Budget").toInt() + 1);
                // send search message
                sendGossipMsg(myGossip.value("Origin").toString(), 
                    myGossip.value("SeqNo").toInt(),
                    QHostAddress(myGossip.value("Host").toString()),
                    myGossip.value("Port").toInt());

                // enqueue the new queue
                newQueue->enqueue(myGossip);
            }
        }
        delete gossipQueue;
        gossipQueue = newQueue;
    } else {
        delete timerForAck;
        timerForAck = NULL;
    }
}

// ---------------------------------------------------------------------
// look up the host 
// it should be called every time Peerster wants to add a new direct neighbor,
// because it have to transform hostname to IP address. So the only types stored
// in the peerList and direct neighbor list view are IP address.
void PeersterDialog::
lookedUp(const QHostInfo& host) {
    // Check whether there are hosts
    if (host.error() != QHostInfo::NoError) {
        // qDebug() << "Lookup failed:" << host.errorString();
        return ;
    } 
    foreach (const QHostAddress &address, host.addresses()) {
        qDebug() << "Found address:" << address.toString();

        // QString inputAddrPort = addAddrPort->text();
        // QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));

        QString inputAddrPort = address.toString() + tr(":") + QString::number(newPort);
        QString addr = address.toString(); 
        quint16 port = newPort;
        
        // If the port is specified
        if (port != 0) {
            if (!addrPortStrList.contains(inputAddrPort)) {
                // update it to the peer list
                Peer peer(addr, address, port);
                peerList->append(peer);

                // update it to the list view
                // qDebug() << "FFFFFFFFFFFFFFFFFFFFFFFFFFFF";
                addrPortStrList.append(address.toString() + ":" + QString::number(port));
                ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);
                
                // broadcast when there are new neighbors
                broadcastRM();
            }
        }
    }
}

// ---------------------------------------------------------------------
// for look up addr:port pairs given by the arguments "./peerster hostname:port"
// before running the program
void PeersterDialog::
lookedUpBeforeInvoke(const QHostInfo& host) {
    static int times = 1;
    // Check whether there are hosts
    if (host.error() != QHostInfo::NoError) {
        qDebug() << "Lookup failed:" << host.errorString();
        return ;
    }
    foreach (const QHostAddress &address, host.addresses()) {
        qDebug() << "Found address:" << address.toString();

        QString inputAddrPort = qApp->argv()[times];
        QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
        quint16 port = inputAddrPort.right(inputAddrPort.length() - inputAddrPort.lastIndexOf(":") - 1).toInt();
        
        // If the port is specified
        if (port != 0) {
            // update it to the peer list
            Peer peer(addr, address, port);
            peerList->append(peer);

            // update it to the list view
            addrPortStrList.append(address.toString() + ":" + QString::number(port));
            ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);

        }
    }
}
    
// --------------------------------------------------------------------
// it is called when input IP:port in the line edit and press return
void PeersterDialog::
addrPortAdded() {
    QString inputAddrPort = addAddrPort->text();
    newIP = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
    newPort = inputAddrPort.right(inputAddrPort.length() - inputAddrPort.lastIndexOf(":") - 1).toInt();
    // qDebug() << addr << ":" << port;
    
    QHostInfo::lookupHost(newIP, 
        this, SLOT(lookedUp(QHostInfo)));

    addAddrPort->clear();
} 

// --------------------------------------------------------------------
// Broadcast routing messages to all peer neighbors
void PeersterDialog::
broadcastRM() {
    for (int i = 0; i < peerList->size(); i++) {
        QStringList nidStrList;
        QStringList newKeyStrList;
        sendRoutingMsg(*myOrigin, routMessSeqNo, peerList->at(i).getIP(), peerList->at(i).getPort(), nidStrList, newKeyStrList);
    }
    routMessSeqNo ++;
}

// --------------------------------------------------------------------
// send status messages periodically
void PeersterDialog::
antiEntropy() {
    if (updateStatusMap->contains(*myOrigin)) {
        // randomly pick up a peer from peerlist
        Peer destPeer = peerList->at(qrand()%(peerList->size()));

        // send the message
        sendStatusMsg(*myOrigin, updateStatusMap->value(*myOrigin).toInt() + 1, destPeer.getIP(), destPeer.getPort());
        qDebug() << "antiEntropy triggered";
    }
}

// ---------------------------------------------------------------------
// In gossip messaging, when return is pressed, send the message
void PeersterDialog::
gotReturnPressed() {
    // Build the gossip chat message 
    QVariantMap *rumorMessage = new QVariantMap();
    rumorMessage->insert("ChatText", textedit->toPlainText());
    rumorMessage->insert("Origin", *myOrigin);
    rumorMessage->insert("SeqNo", SeqNo);
    
    // Pick up a destination from peer list
    Peer destPeer = peerList->at(qrand()%(peerList->size()));

    // Update recvMessageMap
    recvMessageMap->insert(rumorMessage->value("Origin").toString() + "[Ori||Seq]" + rumorMessage->value("SeqNo").toString(), *rumorMessage);

    // Update updateStatusMap
    updateStatusMap->insert(rumorMessage->value("Origin").toString(), rumorMessage->value("SeqNo").toInt());
    
    // enqueue the sending message
    QVariantMap myGossip;
    myGossip.insert("Origin", *myOrigin);
    myGossip.insert("SeqNo", SeqNo);
    myGossip.insert("Host", destPeer.getIP().toString());
    myGossip.insert("Port", destPeer.getPort());
    myGossip.insert("Budget", 1);
    gossipQueue->enqueue(myGossip);

    // Send the message
    // sendGossipMsg(rumorMessage->value("Origin").toString(),  rumorMessage->value("SeqNo").toInt(), destPeer.getIP(), destPeer.getPort());
    updateGossipQueue();
    if (timerForAck != NULL) {
        delete timerForAck;
        timerForAck = NULL;
    } else {
        timerForAck= new QTimer(this);
        connect(timerForAck, SIGNAL(timeout()), this, SLOT(updateGossipQueue()));
        // repeat the search once per second
        timerForAck->start(2000);
    }

    textview->append("me > " + textedit->toPlainText());
    textedit->clear();
    SeqNo++;

    delete rumorMessage;
}

// ---------------------------------------------------------------------
// 
void PeersterDialog::
sendGossipMsg(const QString origin, const quint32 seqNo, const QHostAddress host, const quint16 port) {
    if (isNoForward == false) {
        QString OriSeq = origin + tr("[Ori||Seq]") + QString::number(seqNo);
        QVariantMap *message = new QVariantMap(recvMessageMap->value(OriSeq).toMap());

        // if it is a forwarding message, add lastIP, lastPort
        if (message->value("Origin").toString() != *myOrigin) {
            message->insert(tr("LastIP"), lastIP->toString());
            message->insert(tr("LastPort"), lastPort);
        }
        
        // Serialize 
        QByteArray *bytearrayToSend = new QByteArray();
        QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
        bytearrayStreamOut << (*message);

        // Send the datagram 
        qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
        if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
        //qDebug() << "send gossip message";

        delete message;
        delete bytearrayToSend;
    }
}

// ---------------------------------------------------------------------
//
void PeersterDialog::
sendStatusMsg(const QString origin, const quint32 seqNo, const QHostAddress host, const quint16 port) {
    if (isNoForward == false) {
        QVariantMap wantValue;
        wantValue.insert(origin, seqNo);
        QVariantMap *message = new QVariantMap();
        message->insert("Want", wantValue);
        
        // Serialize 
        QByteArray *bytearrayToSend = new QByteArray();
        QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
        bytearrayStreamOut << (*message);

        // Send the datagram 
        qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
        if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
        // qDebug() << "send status message";

        delete message;
        delete bytearrayToSend;
    }
}
 
// ---------------------------------------------------------------------
//
void PeersterDialog::
sendRoutingMsg(const QString origin, const quint32 seqNo, const QHostAddress host, const quint16 port, QStringList nidStrList, QStringList keyStrList) {
    QVariantMap *message = new QVariantMap();
    message->insert("Origin", origin);
    message->insert("SeqNo", seqNo);

    // if it is a forwarding message, add lastIP, lastPort
    if (message->value("Origin").toString() != *myOrigin) {
        message->insert(tr("LastIP"), lastIP->toString());
        message->insert(tr("LastPort"), lastPort);
    }
    
    // NID list
    nidStrList.append(*myOrigin);
    message->insert(("Path"), nidStrList);

    // key list 
    keyStrList.append(pubkey.toPEM());
    message->insert(("PathKeys"), keyStrList);

    // Serialize 
    QByteArray *bytearrayToSend = new QByteArray();
    QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
    bytearrayStreamOut << (*message);

    // Send the datagram 
    qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
    //qDebug() <<"send routing message";

    delete message;
    delete bytearrayToSend;
}


// ----------------------------------------------------------------------
// respond to messages received
void PeersterDialog::
gotRecvMessage() {
    while (sockRecv->hasPendingDatagrams()) {
        // read datagram into an instance of QByteArray
        QByteArray *bytearrayRecv = new QByteArray();
        bytearrayRecv->resize(sockRecv->pendingDatagramSize());
        QHostAddress senderAddr;
        quint16 senderPort;
        qint64 int64Status = sockRecv->readDatagram(bytearrayRecv->data(), bytearrayRecv->size(), 
            &senderAddr, &senderPort);
        if (int64Status == -1) qDebug() << "Recv error!"; 

        // Whether should it be added to peer list as a new peer?
        bool containsPeer = false;
        for (int i = 0; i < peerList->size(); i++) {
            if (peerList->at(i).getIP() == senderAddr && peerList->at(i).getPort() == senderPort) {
                containsPeer = true;
            }
        }
        if (containsPeer == false) {
            // update it to the direct neighbors
            newIP = senderAddr.toString();
            newPort = senderPort;
            QHostInfo::lookupHost(senderAddr.toString(), 
                this, SLOT(lookedUp(QHostInfo)));

        }

        // record the last ip and port info
        *lastIP = senderAddr;
        lastPort = senderPort;

        // deserialize
        QVariantMap recvMessage;
        QDataStream bytearrayStreamIn(bytearrayRecv, QIODevice::ReadOnly);
        bytearrayStreamIn >> recvMessage;
        // qDebug() << "recv the map: " << recvMessage;

        delete bytearrayRecv;

        // if it is a status message
        if (recvMessage.contains("Want")) {
            // qDebug() << "Received new SM from " << senderPort;
            QMapIterator <QString, QVariant> iter(recvMessage.value("Want").toMap());
            iter.next();
            QString recvOrigin = iter.key();
            quint32 recvSeqNo = iter.value().toInt();
            // qDebug() << "Want recvOrigin = " << recvOrigin;
            // qDebug() << "Want recvSeqNo= " << recvSeqNo;
            
            if (updateStatusMap->contains(recvOrigin)) {
                quint32 mySeqNo = updateStatusMap->value(recvOrigin).toInt();
                if (mySeqNo + 1 == recvSeqNo) {
                    // if it is a ACK then stop
                    // qDebug() << updateGossipQueue(recvOrigin, recvSeqNo-1, senderAddr.toString(), senderPort);
                    if (updateGossipQueue(recvOrigin, recvSeqNo, senderAddr.toString(), senderPort)) {
                        // qDebug() << "Should end!";
                        return;
                    } else {
                        // if it is not a ACK
                        if (qrand()%2 == 0) {
                            // Random pick up a peer from peerlist
                            Peer destPeer = peerList->at(qrand()%(peerList->size()));

                            // forward
                            sendGossipMsg(recvOrigin, updateStatusMap->value(recvOrigin).toInt(), destPeer.getIP(), destPeer.getPort());
                        } else 
                            return; // cease totally
                    }
                }
                else if (mySeqNo + 1 > recvSeqNo) {    
                    // You need new message
                    // Send my gossip message one by one
                    for (quint32 i = recvSeqNo; i <= mySeqNo; i++) {
                       sendGossipMsg(recvOrigin, i, senderAddr, senderPort);
                    }
                }
                else if (mySeqNo + 1 < recvSeqNo) {
                    // I need new message
                    // Send my status message 
                    sendStatusMsg(recvOrigin, mySeqNo + 1, senderAddr, senderPort);
                }
            }  else // not contain orgin 
                sendStatusMsg(recvOrigin, 1, senderAddr, senderPort);
        }

        // it is a Gossip Message (GM) 
        if (recvMessage.contains("ChatText") && recvMessage.contains("Origin") && recvMessage.contains("SeqNo")) {
            // qDebug() << "received new GM from " << senderPort;
            QString recvOrigin = recvMessage.value("Origin").toString();
            quint32 recvSeqNo = recvMessage.value("SeqNo").toInt();

            // debug
            // textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());

            //ACK
            sendStatusMsg(recvOrigin, recvSeqNo + 1, senderAddr, senderPort);

            // update the nextHopTable
            if (recvOrigin != *myOrigin) {
                updateRoutOriSeqMap->insert(recvOrigin, recvSeqNo);
                nextHopTable->insert(recvOrigin, QPair<QHostAddress, quint16>(senderAddr, senderPort));
                // Add it to the All dest list view if not exist
                if (!originStrList.contains(recvOrigin)) {
                    originStrList.append(recvOrigin);
                    ((QStringListModel*) originListView->model())->setStringList(originStrList);
                }
            }

            // NAT punching If there new possible neighbors identified by lastIp and lastPort, then add them to direct neighbor
            /* deprecated for onion routing
            if (recvMessage.contains("LastIP") && recvMessage.contains("LastPort")) {
                QString recvLastIP = recvMessage.value("LastIP").toString();
                quint16 recvLastPort = recvMessage.value("LastPort").toInt();
                QString ipaddr_port = recvLastIP + ":" + QString::number(recvLastPort);
                bool containsPeer = false;
                for (int i = 0; i < peerList->size(); i++) {
                    if (peerList->at(i).getIP() == QHostAddress(recvLastIP) && peerList->at(i).getPort() == recvLastPort)
                        containsPeer = true;
                }
                if (!addrPortStrList.contains(ipaddr_port) && containsPeer == false && (QHostAddress(recvLastIP) != QHostAddress::LocalHost || recvLastPort != sockRecv->getMyPort())) {
                    // LOOKUP
                    newIP = recvLastIP;
                    newPort = recvLastPort;
                    QHostInfo::lookupHost(newIP, this, SLOT(lookedUp(QHostInfo)));

                }
            }
            */

            // if I have contacted this NID
            if (updateStatusMap->contains(recvOrigin)) {
                quint32 mySeqNo = updateStatusMap->value(recvOrigin).toInt();
                if ( mySeqNo + 1 == recvSeqNo) {
                    // It is an exact new message for me
                    // update message 
                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));

                    textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());
                    
                    // broadcast message
                    for (int i = 0; i < peerList->size(); i++) {
                        Peer destPeer = peerList->at(i);
                        sendGossipMsg(recvOrigin, recvSeqNo, destPeer.getIP(), destPeer.getPort());
                    }
                } else {
                    // I have not received previous messages
                    // send my status message
                    sendStatusMsg(recvOrigin, mySeqNo + 1, senderAddr, senderPort);
                }
            // if I have not contacted this NID
            } else { // not contain origin 
                if ( 1 == recvSeqNo )  {
                    // It is an exact new message for me
                    // receive and update the message
                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));

                    // if it is a gossip message then display
                    textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());
                    
                    // broadcast message
                    for (int i = 0; i < peerList->size(); i++) {
                        Peer destPeer = peerList->at(i);
                        // forward
                        sendGossipMsg(recvOrigin, recvSeqNo, destPeer.getIP(), destPeer.getPort());
                    }
                } else {
                    // I have not received previous messages
                    // Send my status message
                    sendStatusMsg(recvOrigin, 1, senderAddr, senderPort);
                    textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());
                }
            }
        }

        // It is a Routing Message (RM)
        if (!recvMessage.contains("ChatText") && recvMessage.contains("Origin") && recvMessage.contains("SeqNo")) {
            qDebug() << "received routing message from " << senderAddr << senderPort;
            QString recvOrigin = recvMessage.value("Origin").toString();
            quint32 recvSeqNo = recvMessage.value("SeqNo").toInt();
            //qDebug() << "NID: " << recvOrigin;


            if (recvOrigin != *myOrigin) {
                // If there new possible neighbors identified by lastIp and lastPort, then add them to direct neighbor
                /* deprecated for onion routing 
                if (recvMessage.contains("LastIP") && recvMessage.contains("LastPort")) {
                    QString recvLastIP = recvMessage.value("LastIP").toString();
                    quint16 recvLastPort = recvMessage.value("LastPort").toInt();
                    QString ipaddr_port = recvLastIP + ":" + QString::number(recvLastPort);
                    bool containsPeer = false;
                    for (int i = 0; i < peerList->size(); i++) {
                        if (peerList->at(i).getIP() == QHostAddress(recvLastIP) && peerList->at(i).getPort() == recvLastPort)
                            containsPeer = true;
                    }
                    if (!addrPortStrList.contains(ipaddr_port) && containsPeer == false && (QHostAddress(recvLastIP) != QHostAddress::LocalHost || recvLastPort != sockRecv->getMyPort())) {
                        // LOOKUP
                        newIP = recvLastIP;
                        newPort = recvLastPort;
                        QHostInfo::lookupHost(newIP, this, SLOT(lookedUp(QHostInfo)));

                    }
                }
                */

                quint32 myRoutSeqNo = updateRoutOriSeqMap->value(recvOrigin).toInt();
                //qDebug() << "recvSeqNo=" << recvSeqNo; 
                if (updateRoutOriSeqMap->contains(recvOrigin) && myRoutSeqNo == recvSeqNo) {// It is the routing message I have received
                    //qDebug() << "I have received before";
                    return;
                } else { // It is a new routing message I have not heard of
                    if (recvMessage.contains("PathKeys") )
                        //qDebug() << "Key number:" << QString::number(recvMessage.value("PathKeys").toStringList().size());
                    // TODO  Exchange my routing message 
                    //qDebug() << "good";

                    // update the nextHopTable
                    updateRoutOriSeqMap->insert(recvOrigin, recvSeqNo);
                    nextHopTable->insert(recvOrigin, QPair<QHostAddress, quint16>(senderAddr, senderPort));
                    // update Path and PathKeys for onion routing 
                    if (recvMessage.contains("Path") && recvMessage.contains("PathKeys") ){
                        nextHopTable->insertPathNIDs(recvOrigin, recvMessage.value("Path").toStringList());
                        nextHopTable->insertPathKeys(recvOrigin, recvMessage.value("PathKeys").toStringList());
                    }
                    // Add it to the All dest list view if not exist
                    if (!originStrList.contains(recvOrigin)) {
                        originStrList.append(recvOrigin);
                        ((QStringListModel*) originListView->model())->setStringList(originStrList);
                    }
                    // broadcast message
                    for (int i = 0; i < peerList->size(); i++) {
                        Peer destPeer = peerList->at(i);
                        QStringList newKeyStrList(recvMessage.value("PathKeys").toStringList());
                        QStringList nidStrList(recvMessage.value("Path").toStringList());
                        sendRoutingMsg(recvOrigin, recvSeqNo, destPeer.getIP(), destPeer.getPort(), nidStrList, newKeyStrList);
                    }
                }
            }
        }

        // It is a private message | block request | block reply | search reply
        if (recvMessage.contains("Dest") && recvMessage.contains("HopLimit") ) {
            QString dest = recvMessage.value("Dest").toString();
            // If it is sent to me
            if (dest  == *myOrigin) {
                if (recvMessage.contains("ChatText"))
                    // if it is a private message
                    textview->append(senderAddr.toString() + ":" + QString::number(senderPort) + " > " + recvMessage.value("ChatText").toString() + recvMessage.value("HopLimit").toString());
                if (recvMessage.contains("Origin") && recvMessage.contains("BlockRequest")) {
                    // if it is a block request message
                    QByteArray requestHash = recvMessage.value("BlockRequest").toByteArray();
                    // find file in local filesMetas
                    for (QVector<FileMetaData*>::iterator it = filesMetas.begin(); it < filesMetas.end(); it++) {
                        if ((*it)->contains(requestHash)) {
                            // if I have the file, pack the file into QByteArray data
                            QFile readF((*it)->getFilePath(requestHash)); 
                            // qDebug() << (*it)->getFilePath(requestHash);
                            if (readF.open(QIODevice::ReadOnly)) {
                                QByteArray data = readF.readAll();
                                readF.close();
                                // hash the data
                                QCA::Hash shaHash("sha256");
                                shaHash.update(data);
                                QCA::MemoryRegion hashA = shaHash.final();
                                QByteArray dataHash = hashA.toByteArray();
                                // qDebug() << QCA::arrayToHex(dataHash);
                                // send the file
                                sendBlockReply(
                                    recvMessage.value("Origin").toString(),
                                    *myOrigin, 
                                    10, 
                                    dataHash,
                                    data);
                            }
                        }
                    }
                }

                // if it is a block reply message
                if (recvMessage.contains("Origin") && recvMessage.contains("BlockReply") && recvMessage.contains("Data")) {
                    // TODO should i use BlockReply?
                    QString origin = recvMessage.value("Origin").toString();
                    // check the package with hash
                    QByteArray data(recvMessage.value("Data").toByteArray());
                    QCA::MemoryRegion hashA;
                    QCA::Hash shaHash("sha256");
                    shaHash.update(data);
                    hashA = shaHash.final();
                    QByteArray replyHash(recvMessage.value("BlockReply").toByteArray());
                    if (hashA.toByteArray() == replyHash) {
                        //textview->append("Recv " + QCA::arrayToHex(replyHash));
                        // check info and request all files and combine
                        for (QVector<FileMetaData*>::iterator it = recvFilesMetas.begin(); it < recvFilesMetas.end(); it++) {
                            if ((*it)->contains(replyHash)) {
                                QString sequence;
                                QString fileName;
                                if ((*it)->getBlockListHash() == replyHash) {
                                    // if it is a meta file, fill the recvFilesMetas and request the resting files
                                    (*it)->fillBlockList(data);
                                    sequence = tr("meta");
                                    fileName = tr("recv_blocks/") + (*it)->getFileNameOnly() + tr("_") + (sequence);
                                    (*it)->setMetaFilePath(fileName);

                                    // request the resting sub files
                                    for (int i = 0; i < (*it)->getSubFilesNum(); ++i)
                                        sendBlockRequest(origin, *myOrigin, 10, (*it)->getBlockList().mid(i*32, 32));
                                    
                                } else {
                                    // if it is a sub file 
                                    // write the sub file 
                                    // textview->append(QString::number((*it)->getBlockList().indexOf(replyHash)));
                                    sequence = QString::number((*it)->getBlockList().indexOf(replyHash)/32);
                                    fileName = tr("recv_blocks/") + (*it)->getFileNameOnly() + tr("_") + (sequence);
                                    (*it)->setSubFilePath((*it)->getBlockList().indexOf(replyHash)/32, fileName);
                                } 
                                // write to file
                                QFile writeFile(fileName);
                                if (writeFile.open(QIODevice::WriteOnly)) {
                                    writeFile.write(data.data(), data.size());
                                    writeFile.close();
                                }
                                // if all files are presented then combine them into one
                                bool completeFlag = true;
                                for (int i = 0; i < (*it)->getSubFilesNum(); ++i) {
                                    if ((*it)->getSubFilePath(i).indexOf("recv_blocks") == -1) completeFlag = false;
                                    // qDebug() << completeFlag;
                                }
                                if (completeFlag == true) {
                                    (*it)->uniteFile(tr("recv_blocks/"), 8*1024);
                                    textview->append("downloaded to recv_blocks/" + (*it)->getFileNameOnly());
                                }
                            }
                        }
                    }
                }

                // if it is a search reply message
                if (recvMessage.contains("Origin") && recvMessage.contains("SearchReply") && recvMessage.contains("MatchNames") && recvMessage.contains("MatchIDs")) {
                    QVariantList matchNames = recvMessage.value("MatchNames").toList();
                    QVariantList matchIDs = recvMessage.value("MatchIDs").toList();
                    QString origin = recvMessage.value("Origin").toString();
                    for (int i = 0; i < matchNames.size(); ++i) {
                        // check whether I have received the information
                        bool recvFlag = false;
                        for (QVector<FileMetaData*>::iterator it = recvFilesMetas.begin(); it < recvFilesMetas.end(); it++)
                            if ((*it)->contains(matchIDs.at(i).toByteArray())) recvFlag = true;
                        if (recvFlag == false) {
                            // update recv files meta information
                            // qDebug() << "search reply debug" << matchIDs.at(i).toByteArray();
                            recvFilesMetas.append(new FileMetaData(matchNames.at(i).toString(), matchIDs.at(i).toByteArray(), origin));
                            // update search results list view
                            searchResultsStringList->append(matchNames.at(i).toString());
                            ((QStringListModel*) searchResultsListView->model())->setStringList(*searchResultsStringList);
                        }
                    }
                }
            } else if (recvMessage.value("HopLimit").toInt() > 0 && isNoForward == false) { 
                // if it is not sent to me and -noforward is not set
                recvMessage.insert("HopLimit", (quint32)(recvMessage.value("HopLimit").toInt()-1));
                recvMessage.insert("ChatText", recvMessage.value("ChatText").toString()+tr("|")+*myOrigin);

                // serialize 
                QByteArray *bytearrayToSend = new QByteArray();
                QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
                bytearrayStreamOut << recvMessage;

                // send the datagram 
                if (nextHopTable->contains(dest)) {
                    QHostAddress host = nextHopTable->value(dest).first;
                    quint16 port = nextHopTable->value(dest).second;

                    qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
                    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
                    qDebug() << "PM" << " from " << sockRecv->getMyPort() <<" has been sent to " << port << "| size: " << int64Status;
                }
                delete bytearrayToSend;
            }
        }

        // if it is a encrypted onion private message
        if (recvMessage.contains("Envelop") && recvMessage.contains("EncryptedKey") && recvMessage.contains("IV") ) {
            //qDebug() << "#### encrypted message received";

            // decrypt key
            QCA::SecureArray envelopSA(recvMessage.value("EncryptedKey").toByteArray());
            QCA::SecureArray decrypt;
            if (0 == seckey.decrypt(envelopSA, &decrypt, QCA::EME_PKCS1_OAEP)) 
                qDebug() << "Error decrypting";
            QCA::SymmetricKey envelopKey (decrypt.toByteArray());

            // decrypt envelop
            QCA::InitializationVector iv(recvMessage.value("IV").toByteArray());
            QCA::Cipher dc(QString("aes128"),QCA::Cipher::CBC,
                       QCA::Cipher::DefaultPadding,
                       QCA::Decode,
                       envelopKey, iv);
            //printf("One step decryption using AES128: %s\n",
            //   QCA::SecureArray(dc.process(cipherText)).data() );
            QByteArray envelopBA(QCA::SecureArray(dc.process(QCA::SecureArray(recvMessage.value("Envelop").toByteArray()))).toByteArray());

            // deserialize
            QVariantMap envelop;
            QDataStream bs(&envelopBA, QIODevice::ReadOnly);
            bs >> envelop;
            // TODO why it is vacant?
            //qDebug() << envelop.size();
            if (envelop.contains("Dest")) {
                // it is not sent to me 
                textview->append("Onion Routing> Forward Envelop");

                QString dest = envelop.value("Dest").toString();
                if (nextHopTable->contains(dest)) {
                    QHostAddress host = nextHopTable->value(dest).first;
                    quint16 port = nextHopTable->value(dest).second;

                    // serialize
                    QByteArray ba;
                    QDataStream ds(&ba, QIODevice::WriteOnly);
                    ds << envelop;

                    qint64 int64Status = sockRecv->writeDatagram(ba, host, port);
                    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
                    qDebug() << "PM" << " from " << sockRecv->getMyPort() <<" has been sent to " << port << "| size: " << int64Status;
                }
            } else if (envelop.contains("ChatText")) {
                textview->append("Onion Routing> " + envelop.value("ChatText").toString());
            }
        }

        // if it is a search request message
        if (recvMessage.contains("Origin") && recvMessage.contains("Search") && recvMessage.contains("Budget")) {
            QString origin = recvMessage.value("Origin").toString();
            QString search = recvMessage.value("Search").toString();
            quint32 budget = recvMessage.value("Budget").toInt(); 
            // search names of the files stored locally
            // find file in local filesMetas
            QVariantList matchNames;
            QVariantList matchIDs;
            bool isFound = false;
            for (QVector<FileMetaData*>::iterator it = filesMetas.begin(); it < filesMetas.end(); it++) {
                if ((*it)->contains(search)) {
                    isFound = true;
                    matchNames.push_back((*it)->getFileNameOnly());
                    matchIDs.push_back((*it)->getBlockListHash());
                    // qDebug() << "matchIDs " << matchIDs;
                }
            }
            // send search reply if files are found
            if (isFound == true && search != "")
                sendSearchReply(origin, *myOrigin, 10, search, matchNames, matchIDs);

            // forward to random neighbors if budget allows
            quint32 newBudget = budget - 1;
            if (newBudget > 0) {
                int nodes; // number of nodes to send
                if (newBudget >= (quint32)peerList->size()){
                    nodes = peerList->size();
                } else 
                    nodes = newBudget;
                // randomly pick up nodes of peers 
                int nPeers[nodes];
                int i = 0;
                while (i != nodes) {
                    nPeers[i] = qrand()%peerList->size();
                    bool repeatFlag = false;
                    for (int j = 0; j < i; ++j)
                        if (nPeers[i] == nPeers[j]) repeatFlag = true;
                    if (repeatFlag == false) ++i;
                }
                // send search message
                quint32 budgetPerNode;
                quint32 restBudget = 0;
                if ((float)newBudget/nodes <= 1) 
                    budgetPerNode = 1;
                else {
                    budgetPerNode = newBudget/nodes;
                    restBudget = newBudget%nodes;
                }
                for ( i = 0; i < nodes; ++i) {
                    Peer destPeer = peerList->at(nPeers[i]);
                    quint32 myBudget = budgetPerNode;
                    if (restBudget > 0) {
                        ++myBudget;
                        --restBudget;
                    }
                    sendSearchRequest(origin, search, myBudget, QHostAddress(destPeer.getIP()), destPeer.getPort());
                }
            }
        }
    }
}

// ---------------------------------------------------------------------
// whether file name contains the key words?
bool FileMetaData::
contains(QString keyWords) const { // overload for search key words
        QStringList list = keyWords.split(QRegExp("\\s+"));
        bool contains = false;
        for (int i = 0; i < list.size(); ++i)
        {
            // all comparisons are in lowercase
            if (fileNameOnly.toLower().indexOf(QString(list.at(i).toLocal8Bit().constData()).toLower()) != -1) contains = true;
        }
        return contains;
}

// ---------------------------------------------------------------------
// catch the returnPressed event to send the message
bool PeersterDialog::eventFilter(QObject *obj, QEvent *event) {
    if (obj == textedit && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyevent = static_cast<QKeyEvent *>(event);
        if (keyevent->key() == Qt::Key_Return) {
            // if the content is null then the message would not be sent.
            if(textedit->toPlainText() == "")
                return true;
            else
                gotReturnPressed();
            return true;
        } else
        return false;
    }

    return false;
}

// ---------------------------------------------------------------------
// respond to double click on search results and download the file
void PeersterDialog::
downloadFile(const QModelIndex& index) {
    FileMetaData *downloadTaget = recvFilesMetas.at(index.row());
    sendBlockRequest(downloadTaget->getOriginNID(), *myOrigin, 10, downloadTaget->getBlockListHash());
}

// ---------------------------------------------------------------------
// private message window
PrivateMessage::
PrivateMessage(const QModelIndex& index, PeersterDialog* p2p) {
    upperP2P = p2p;
    //qDebug() << *(upperP2P->updateStatusMap);
    dest = (((QStringListModel*) index.model())->stringList()).at(index.row());
	setWindowTitle("Private Message: " + dest);

	// Read-only text box where we display messages from everyone.
	textview = new QTextEdit(this);
	textview->setReadOnly(true);

    textedit = new QTextEdit(this);
    textedit->installEventFilter(this);

	// Lay out the widgets to appear in the main window.
	QVBoxLayout *layout = new QVBoxLayout();
	layout->addWidget(textview);
	layout->addWidget(textedit);
    textedit->setFocus();
	setLayout(layout);
}

// --------------------------------------------------------------------
// private message got return pressed
bool PrivateMessage::
eventFilter(QObject *obj, QEvent *event) {
    if (obj == textedit && event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyevent = static_cast<QKeyEvent *>(event);
        if (keyevent->key() == Qt::Key_Return) {
            // if the content is null then the message would not be sent.
            if(textedit->toPlainText() == "")
                return true;
            else
                gotReturnPressed();
            return true;
        } else
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------
// send private message
void PrivateMessage::
gotReturnPressed() {
    // get destination IP:port
    QHostAddress host = upperP2P->nextHopTable->value(dest).first;
    quint16 port = upperP2P->nextHopTable->value(dest).second;

    QVariantMap privateMessageMap;
    if (upperP2P->nextHopTable->hasPath(dest) && QCA::isSupported("aes128-cbc-pkcs7")) {
        // if it can be encrypted with onions
        QStringList path = upperP2P->nextHopTable->getPathNIDs(dest);
        QStringList keys = upperP2P->nextHopTable->getPathKeys(dest);
        privateMessageMap.insert("ChatText", textedit->toPlainText());
        //qDebug() << textedit->toPlainText();
        do {
            // serialize 
            QByteArray ba;
            QDataStream bs(&ba, QIODevice::WriteOnly);
            bs << privateMessageMap;
            //qDebug() << "ba.size()=" << ba.size();

            // encrypt ChatText with symmetric key
            QCA::SymmetricKey textKey(16);
            QCA::InitializationVector iv(16);
            QCA::Cipher cipher(QString("aes128"),QCA::Cipher::CBC,
                       QCA::Cipher::DefaultPadding,
                       QCA::Encode,
                       textKey, iv);
            QCA::SecureArray u = cipher.update(ba);
            if (!cipher.ok()) {
              printf("Update failed\n");
            }
            QCA::SecureArray f = cipher.final();
            if (!cipher.ok()) {
              printf("Final failed\n");
            }
            QCA::SecureArray cipherText = u.append(f);
            // add cipher text to privateMessageMap
            privateMessageMap.clear();
            privateMessageMap.insert("Envelop", cipherText.toByteArray());
            privateMessageMap.insert("IV", iv.toByteArray());

            // encrypt textKey with asymmetric key
            QCA::SecureArray sa(textKey.toByteArray());
            //qDebug() << "sa.size()=" << sa.size();
            QCA::ConvertResult conversionResult;
            QCA::PublicKey pk = QCA::PublicKey::fromPEM(keys.first(), &conversionResult);
            //qDebug() << "MAX:" << pk.maximumEncryptSize(QCA::EME_PKCS1_OAEP);
            keys.pop_front();
            if (! QCA::ConvertGood == conversionResult)
                qDebug() << "Public key read failed";
            QCA::SecureArray cipheredKey = pk.encrypt(sa, QCA::EME_PKCS1_OAEP);
            if (cipheredKey.isEmpty()) 
                qDebug() << "Error encrypting";
            //qDebug() << "cipheredKey.size()=" << cipheredKey.size();
            // add ciphered key to privateMessageMap
            privateMessageMap.insert("EncryptedKey", cipheredKey.toByteArray());

            if (keys.length() != 0) {
                privateMessageMap.insert("Dest", path.first());
                path.pop_front();
            } 
        } while (keys.length());
        
    } else {
        // if it can not be encrypted with onions
        // it will be sent as plaintext
        privateMessageMap.insert("Dest", dest);
        privateMessageMap.insert("ChatText", textedit->toPlainText());
        privateMessageMap.insert("HopLimit", (quint32)10);
    }

    // Serialize 
    //qDebug() << "privateMessageMap: " << privateMessageMap.size();
    QByteArray *bytearrayToSend = new QByteArray();
    QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
    bytearrayStreamOut << privateMessageMap;
    //qDebug() << "bytearrayToSend: " << bytearrayToSend->size();

    // Send the datagram 

    qint64 int64Status = upperP2P->sockRecv->writeDatagram(*bytearrayToSend, host, port);
    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
    qDebug() << "PM" << " from " << upperP2P->sockRecv->getMyPort() <<" has been sent to " << port << "| size: " << int64Status;

    textview->append("me > " + textedit->toPlainText());
    textedit->clear();
    delete bytearrayToSend;
}

// ---------------------------------------------------------------------
// send block request message
void PeersterDialog::
sendBlockRequest(const QString dest, const QString origin, const quint32 hopLimit, const QByteArray &blockRequest) {
    QVariantMap *message = new QVariantMap();

    message->insert(tr("Dest"), dest);
    message->insert(tr("Origin"), origin);
    message->insert(tr("HopLimit"), hopLimit);
    message->insert(tr("BlockRequest"), blockRequest);
    
    // Serialize 
    QByteArray *bytearrayToSend = new QByteArray();
    QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
    bytearrayStreamOut << (*message);

    // Send the datagram 
    QHostAddress host = nextHopTable->value(dest).first;
    quint16 port = nextHopTable->value(dest).second;

    qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 

    //textview->append("send block request");

    delete message;
    delete bytearrayToSend;
}

// ---------------------------------------------------------------------
// reply block request
void PeersterDialog::
sendBlockReply(const QString dest, const QString origin, const quint32 hopLimit, const QByteArray &blockReply, const QByteArray &data) {
    QVariantMap *message = new QVariantMap();

    message->insert(tr("Dest"), dest);
    message->insert(tr("Origin"), origin);
    message->insert(tr("HopLimit"), hopLimit);
    message->insert(tr("BlockReply"), blockReply);
    message->insert(tr("Data"), data);
    
    // Serialize 
    QByteArray *bytearrayToSend = new QByteArray();
    QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
    bytearrayStreamOut << (*message);

    // Send the datagram 
    QHostAddress host = nextHopTable->value(dest).first;
    quint16 port = nextHopTable->value(dest).second;

    qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 

    //textview->append("reply block request");

    delete message;
    delete bytearrayToSend;
}

// ---------------------------------------------------------------------
// when searchFileBtn is clicked on 
void PeersterDialog::
onSearchFileBtnClicked() {
    QPair<QString, quint32> kv(searchKeyWords->text(), 1);
    searchQueue->enqueue(kv);
    updateSearchQueue();
    if (searchTimer != NULL) {
        delete searchTimer;
        searchTimer = NULL;
    } else {
        searchTimer = new QTimer(this);
        connect(searchTimer, SIGNAL(timeout()), this, SLOT(updateSearchQueue()));
        // repeat the search once per second
        searchTimer->start(1000);
    }
}

// ---------------------------------------------------------------------
// the search timer will call this periodically to send requests and clean the queue. If the queue finally goes empty, the timer will be deleted
void PeersterDialog::
updateSearchQueue() {
    if (!searchQueue->isEmpty()) {
        // resend all the pairs in the queue and add their budget

        // a new queue for iterating the original queue
        QQueue<QPair<QString, quint32> > *newQueue = new QQueue<QPair<QString, quint32> > ;
        while (!searchQueue->isEmpty()) {
            QPair<QString, quint32> searchRequest = searchQueue->dequeue();
            if (searchRequest.second > 100) {
            // if the budget exceeds 100, do nothing and do not store into the new queue
            } else {
                const quint32 newBudget = 2*searchRequest.second;
                int nodes; // number of nodes to send
                if (newBudget >= (quint32)peerList->size()) nodes = peerList->size();
                else nodes = newBudget;
                // randomly pick up nodes of peers 
                int nPeers[nodes];
                int i = 0;
                while (i != nodes) {
                    nPeers[i] = qrand()%peerList->size();
                    bool repeatFlag = false;
                    for (int j = 0; j < i; ++j)
                        if (nPeers[i] == nPeers[j]) repeatFlag = true;
                    if (repeatFlag == false) ++i;
                }
                // send search message
                for ( i = 0; i < nodes; ++i) {
                    Peer destPeer = peerList->at(nPeers[i]);
                    sendSearchRequest(*myOrigin, searchRequest.first, newBudget, QHostAddress(destPeer.getIP()), destPeer.getPort());
                }
                // enqueue the new queue
                newQueue->enqueue(QPair<QString, quint32>(searchRequest.first, newBudget));
            }
        }
        delete searchQueue;
        searchQueue = newQueue;
        /* debug
        static int debug;
        qDebug() << debug++;
        */
    } else {
        delete searchTimer;
        searchTimer = NULL;
    }
}

// ---------------------------------------------------------------------
// send search request message with dest *host* and *port*
void PeersterDialog::
sendSearchRequest(const QString origin, const QString search, const quint32 budget, QHostAddress host, quint16 port) {
    /* debug
    qDebug() << origin;
    qDebug() << search;
    qDebug() << budget;
    qDebug() << host;
    qDebug() << port;
    */
    
    QVariantMap *message = new QVariantMap();

    message->insert(tr("Origin"), origin);
    message->insert(tr("Search"), search);
    message->insert(tr("Budget"), budget);
    
    // Serialize 
    QByteArray *bytearrayToSend = new QByteArray();
    QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
    bytearrayStreamOut << (*message);

    // Send the datagram 
    qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 

    textview->append("send search");

    delete message;
    delete bytearrayToSend;
}

// ---------------------------------------------------------------------
// send search reply message
void PeersterDialog::
sendSearchReply(const QString dest, const QString origin, const quint32 hopLimit, const QString searchReply, const QVariantList matchNames, const QVariantList matchIDs) {
    QVariantMap *message = new QVariantMap();

    message->insert(tr("Dest"), dest);
    message->insert(tr("Origin"), origin);
    message->insert(tr("HopLimit"), hopLimit);
    message->insert(tr("SearchReply"), searchReply);
    message->insert(tr("MatchNames"), matchNames);
    message->insert(tr("MatchIDs"), matchIDs);
    
    // Serialize 
    QByteArray *bytearrayToSend = new QByteArray();
    QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
    bytearrayStreamOut << (*message);

    // Send the datagram 
    QHostAddress host = nextHopTable->value(dest).first;
    quint16 port = nextHopTable->value(dest).second;

    qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 

    // textview->append("reply search");

    delete message;
    delete bytearrayToSend;
}

// ---------------------------------------------------------------------
// respond to share button click
void PeersterDialog::
onShareFileBtnClicked() {
    // open a dialog
    QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Add files"), QDir::currentPath(), tr("All files (*.*)"));
    // add files to filesMetas 
    for (int i = 0; i < fileNames.size(); ++i) {
        filesMetas.append(new FileMetaData(fileNames.at(i).toLocal8Bit().constData()));
    }
}

// ---------------------------------------------------------------------
// respond to request button click
void PeersterDialog:: 
onRequestFileBtnClicked() {
    QCA::init();
    QByteArray blockRequest(QCA::hexToArray(targetFID->text()));
    sendBlockRequest(targetNID->text(), *myOrigin, 10, blockRequest);
}

// ---------------------------------------------------------------------
// constructor for FileMetaData when given a file name
FileMetaData::
FileMetaData(const QString fn): 
    fileNameWithPath(fn), 
    fileNameOnly(QDir(fileNameWithPath).dirName()) { 
    // split the file to ./tmp_blocks/
    splitFile(QObject::tr("tmp_blocks/"), 8*1024);
    // qDebug() << getSubFilesNum();
}


// ---------------------------------------------------------------------
// combine sub files into a unite one
void FileMetaData::
uniteFile(const QString outDir, const int blockSize) {
    QFile unitedFile(outDir + getFileNameOnly());
    unitedFile.open(QIODevice::WriteOnly);
    QDataStream unitedFlow(&unitedFile);
    char buffer[blockSize];
    int part = 0;
    while(QFile::exists(outDir + getFileNameOnly() + QObject::tr("_") + QString::number(part))){
        QFile qfOut(outDir + getFileNameOnly() + QObject::tr("_") + QString::number(part));
        qfOut.open(QIODevice::ReadOnly);
        QDataStream fout(&qfOut);
        int readSize = fout.readRawData(buffer, blockSize);
        qfOut.close();
        unitedFlow.writeRawData(buffer, readSize);
        QFile::remove(outDir + getFileNameOnly() + QObject::tr("_") + QString::number(part));
        ++part;
    }
    unitedFile.close();
}

// ---------------------------------------------------------------------
// split the file into blocks stored in outDir
void FileMetaData::
splitFile(const QString outDir, const int blockSize) {
    QFile qf(fileNameWithPath);
    if (!qf.open(QIODevice::ReadOnly)) {
        qDebug() << "No such file";
        return;
    }
    size = qf.size();
    QDataStream fin(&qf);
    char buffer[blockSize];
    int part = 0;
    QCA::Hash shaHash("sha256");
    do {
        // split one by one
        int readSize = fin.readRawData(buffer, blockSize);
        QFile qfOut(outDir + fileNameOnly + QObject::tr("_") + QString::number(part));
        if (qfOut.open(QIODevice::WriteOnly)) {
            QDataStream fout(&qfOut);
            fout.writeRawData(buffer, readSize);
            qfOut.close();
        }
        // save path to subFileNameList;
        subFileNameList.append((outDir + fileNameOnly + QObject::tr("_") + QString::number(part)));
        // hash
        QFile qfHash(outDir + fileNameOnly + QObject::tr("_") + QString::number(part));
        if (qfHash.open(QIODevice::ReadOnly)) {
            QByteArray data = qfHash.readAll();
            shaHash.update(data);
            //shaHash.update(&qfHash);
            QCA::MemoryRegion hashA = shaHash.final();
            qDebug() << QCA::arrayToHex(hashA.toByteArray());
            // save to blockList
            blockList.append(hashA.toByteArray());
            // test 
            // qDebug() << "path:" << getFilePath(hashA.toByteArray());
            qfHash.close();
            shaHash.clear();
        }
        ++part;
    } while (!fin.atEnd());
    qf.close();
    // calculate the hash of blockList
    //shaHash.update(blockList);
    //QCA::MemoryRegion hashA = shaHash.final();
    //shaHash.clear();
    //qDebug() << "meta : " << QCA::arrayToHex(hashA.toByteArray());
    QFile qfOut(outDir + fileNameOnly + QObject::tr("_meta"));
    metaFileName = outDir + fileNameOnly + QObject::tr("_meta");
    if (qfOut.open(QIODevice::WriteOnly)) {
        //QDataStream fout(&qfOut);
        //fout << blockList;
        qfOut.write(blockList.data(), blockList.size()); // use this because stream would add extra 4 bytes
        qfOut.close();
    }
    // test the file's hash 
    QFile readMeta(metaFileName);
    QByteArray hashAll;
    if (readMeta.open(QIODevice::ReadOnly)) {
        shaHash.update(&readMeta);
        hashAll = shaHash.final().toByteArray();
        qDebug() << QCA::arrayToHex(hashAll);
        readMeta.close();
    }
    blockListHash.append(hashAll);
    // qDebug() << "path:" << getFilePath(hashA.toByteArray());

}


// ---------------------------------------------------------------------
// main program entry
int main(int argc, char **argv) {
    // support crypto
    QCA::Initializer qcainit;
	// Initialize Qt toolkit
	QApplication app(argc,argv);
    if(!QCA::isSupported("aes128-cbc-pkcs7")) {
        QMessageBox msgBox;
        msgBox.setText("aes128-cbc-pkcs7 is not supported. Please install qca and qca-ossl.");
        msgBox.exec();
        exit(-1);
    }

    PeersterDialog dialog;

	// Enter the Qt main loop; everything else is event driven
	return dialog.exec();
}
