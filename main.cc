#include <unistd.h>

#include <QApplication>
#include <QDebug>

#include "main.hh"

NetSocket::NetSocket()
{
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

bool NetSocket::bind()
{
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

PeersterDialog::PeersterDialog(QWidget* parent)
{
    // Initial next-hop routing table
    nextHopTable = new QHash<QString, QPair<QHostAddress, quint16> >();
    // generate rand seed
    qsrand(time(0));
    // generate my Origin
    randomOriginID = qrand();
    myOrigin = new QString(QHostInfo::localHostName() + QString::number(randomOriginID));
    // Initialize SeqNo. Wait for the first message to send
    SeqNo = 1;
    routMessSeqNo = 1;
    // Initialize recvMessageMap <Origin+"[Ori||Seq]"+SeqNo, Messsage>
    // Used to store all the coming messages 
    recvMessageMap = new QVariantMap(); 
    updateStatusMap = new QVariantMap(); 
    updateRoutOriSeqMap = new QVariantMap();
    // Used to record the ack to stop resending
    ackHist = new QVector<QString>();
    // Used to record peer information
    peerList = new QList<Peer>();

	setWindowTitle("Peerster: " + *myOrigin);

    // set the last recv pairs
    lastIP = new QHostAddress();
    lastPort = 0;

	// Read-only text box where we display messages from everyone.
	textview = new QTextEdit(this);
	textview->setReadOnly(true);
    textview->append("meta for test : 59780356851e52fd6a9ec2abf0a459a16cbb48ff1b7ea51da7476ff0c61fe94e");

    // Input peer information
    addAddrPort = new QLineEdit();
    // List view to display DIRECT peers ipaddress and ports
    addrPortListView = new QListView();
    addrPortListView->setModel(new QStringListModel(addrPortStrList));
    addrPortListView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // List view to display All dest (DIRECT and INDIRECT) origin list
    originListView = new QListView();
    originListView->setModel(new QStringListModel(originStrList));
    originListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    connect(originListView, SIGNAL(doubleClicked(const QModelIndex& )),
        this, SLOT(openPrivateMessageWin(const QModelIndex& )));
    
    //NetSocket sock;
    sockRecv = new NetSocket();
	if (!sockRecv->bind())
		exit(1);
    connect(sockRecv, SIGNAL(readyRead()),
        this, SLOT(gotRecvMessage()));
    
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
    // destListLabel.setText("All destinations (DoubleClick to talk)");
    neighborListLabel = new QLabel("    Direct neighbors", this);
    //neighborListLabel.setText("Direct neighbors");

    // file sharing
    shareFileBtn = new QPushButton("Share File...", this);
    shareFileBtn->setAutoDefault(false);
    requestFileBtn = new QPushButton("Request File", this);
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
    connect(originListView, SIGNAL(doubleClicked(const QModelIndex& )), this, SLOT(downloadFile(const QModelIndex& )));

	// Lay out the widgets to appear in the main window.
	layout = new QGridLayout();
    // first column 
	layout->addWidget(textview, 0, 0, 13, 1);
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

    // Add hostname:port or ipaddr:port 
    connect(addAddrPort, SIGNAL(returnPressed()),
        this, SLOT(addrPortAdded()));

}

void PeersterDialog::openPrivateMessageWin(const QModelIndex& index)
{
    //dest = (((QStringListModel*) index.model())->stringList()).at(index.row());
    pm = new PrivateMessage(index, this);
    pm->exec();
} 

PeersterDialog::~PeersterDialog()
{
    delete textview;
	delete textedit;
    delete addAddrPort;
    delete addrPortListView;
    delete recvMessageMap;
    delete updateStatusMap;
    delete myOrigin;
    delete peerList;

    delete layout;
    delete sockRecv;
    delete shareFileBtn;
    delete targetNID;
    delete targetFID;
    for (QVector<FileMetaData*>::iterator it = filesMetas.begin(); it < filesMetas.end(); it++)
       delete *it;

}

// ---------------------------------------------------------------------
void PeersterDialog::
lookedUp(const QHostInfo& host)
{
    // Check whether there are hosts
    if (host.error() != QHostInfo::NoError)
    {
        qDebug() << "Lookup failed:" << host.errorString();
        return ;
    }
    foreach (const QHostAddress &address, host.addresses())
    {
        qDebug() << "Found address:" << address.toString();

        QString inputAddrPort = addAddrPort->text();
        QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
        quint16 port = inputAddrPort.right(inputAddrPort.length() - inputAddrPort.lastIndexOf(":") - 1).toInt();
        
        // If the port is specified
        if (port != 0)
        {
            // update it to the peer list
            Peer peer(addr, address, port);
            peerList->append(peer);

            // update it to the list view
            addrPortStrList.append(address.toString() + ":" + QString::number(port));
            ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);
            
            // broadcast when there are new neighbors
            broadcastRM();

            addAddrPort->clear();
        }
    }
}

// for look up addr:port pairs given by the arguments
void PeersterDialog::lookedUpBeforeInvoke(const QHostInfo& host)
{
    static int times = 1;
    // Check whether there are hosts
    if (host.error() != QHostInfo::NoError)
    {
        qDebug() << "Lookup failed:" << host.errorString();
        return ;
    }
    foreach (const QHostAddress &address, host.addresses())
    {
        qDebug() << "Found address:" << address.toString();

        QString inputAddrPort = qApp->argv()[times];
        QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
        quint16 port = inputAddrPort.right(inputAddrPort.length() - inputAddrPort.lastIndexOf(":") - 1).toInt();
        
        // If the port is specified
        if (port != 0)
        {
            // update it to the peer list
            Peer peer(addr, address, port);
            peerList->append(peer);

            // update it to the list view
            addrPortStrList.append(address.toString() + ":" + QString::number(port));
            ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);

        }
    }
}
    

void PeersterDialog::addrPortAdded()
{
    QString inputAddrPort = addAddrPort->text();
    QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
    // qDebug() << addr << ":" << port;
    
    QHostInfo::lookupHost(addr, 
        this, SLOT(lookedUp(QHostInfo)));
} 

// Broadcast routing messages to all peer neighbors
void PeersterDialog::broadcastRM()
{
    for (int i = 0; i < peerList->size(); i++)
    {
        QString fwdInfo = *myOrigin + "[Ori||Seq]" + QString::number(routMessSeqNo)
            + "[-ADDRIS>]" + peerList->at(i).getIP().toString()
            + "[-PORTIS>]" + QString::number(peerList->at(i).getPort())
            + "[-METYPE>]" + "RM";
        fwdMessage(fwdInfo);
    }
    routMessSeqNo ++;
}

void PeersterDialog::antiEntropy()
{
    if (updateStatusMap->contains(*myOrigin))
    {
        // randomly pick up a peer from peerlist
        Peer destPeer = peerList->at(qrand()%(peerList->size()));

        // send the message
        QString fwdInfo = *myOrigin + "[Ori||Seq]" + QString::number(updateStatusMap->value(*myOrigin).toInt() + 1)
            + "[-ADDRIS>]" + destPeer.getIP().toString()
            + "[-PORTIS>]" + QString::number(destPeer.getPort())
            + "[-METYPE>]" + "SM";
        fwdMessage(fwdInfo);
        qDebug() << "antiEntropy triggered!!!";
    }
}


void PeersterDialog::gotReturnPressed()
{

    // Initially, just echo the string locally.
    // Insert some networking code here...
   
    // Exercise 3. Tian added networking code here
    // Build the gossip chat message 
    QVariantMap *rumorMessage = new QVariantMap();
    rumorMessage->insert("ChatText", textedit->toPlainText());
    rumorMessage->insert("Origin", QHostInfo::localHostName() + QString::number(randomOriginID));
    rumorMessage->insert("SeqNo", SeqNo);
    
    // Pick up a destination from peer list
    Peer destPeer = peerList->at(qrand()%(peerList->size()));

    // Update recvMessageMap
    recvMessageMap->insert(rumorMessage->value("Origin").toString() + "[Ori||Seq]" + rumorMessage->value("SeqNo").toString(), *rumorMessage);

    // Update updateStatusMap
    updateStatusMap->insert(rumorMessage->value("Origin").toString(), rumorMessage->value("SeqNo").toInt());
    
    // Send the message
    QString fwdInfo = rumorMessage->value("Origin").toString() + "[Ori||Seq]" + rumorMessage->value("SeqNo").toString() 
        + "[-ADDRIS>]" + destPeer.getIP().toString()
        + "[-PORTIS>]" + QString::number(destPeer.getPort())
        + "[-METYPE>]" + "GM";
    fwdMessage(fwdInfo);

    textview->append("me > " + textedit->toPlainText());
    textedit->clear();
    SeqNo++;
}

void PeersterDialog::
fwdMessage(QString fwdInfo)
{
    // qDebug() << fwdInfo;
    // Parse fwdInfo
    QString OriSeq = fwdInfo.left(fwdInfo.lastIndexOf("[-ADDRIS>]"));
    // qDebug() << "OriSeq: " << OriSeq;
    QHostAddress host(fwdInfo.mid(fwdInfo.lastIndexOf("[-ADDRIS>]") + 10, fwdInfo.lastIndexOf("[-PORTIS>]") - fwdInfo.lastIndexOf("[-ADDRIS>]")-10)); 
    //qDebug() << "host:" << host;
    quint16 port = fwdInfo.mid(fwdInfo.lastIndexOf("[-PORTIS>]") + 10, fwdInfo.lastIndexOf("[-METYPE>]") - fwdInfo.lastIndexOf("[-PORTIS>]")-10).toInt(); 
    //qDebug() << "port:" << port;
    QString mType = fwdInfo.right(2);
    QString OriginSM = fwdInfo.left(fwdInfo.lastIndexOf("[Ori||Seq]"));
    if (OriginSM == "") exit(-1);
    quint32 SeqNoSM = fwdInfo.mid(fwdInfo.lastIndexOf("[Ori||Seq]")+10, fwdInfo.lastIndexOf("[-ADDRIS>]") - fwdInfo.lastIndexOf("[Ori||Seq]") - 10).toInt();
    //qDebug() << "OriginSM: " << OriginSM;
    //qDebug() << "SeqNoSM: " << SeqNoSM;
        
    QVariantMap *message;
    if (mType == "GM" && isNoForward == false)
    {
        message = new QVariantMap(recvMessageMap->value(OriSeq).toMap());

        // if it is a forwarding message, add lastIP, lastPort
        if (message->value("Origin").toString() != *myOrigin)
        {
            message->insert(tr("LastIP"), lastIP->toString());
            message->insert(tr("LastPort"), lastPort);

            //qDebug() << "GGGGGGGGGGGGGGGGGG" << *message;
        }
        
        // Serialize 
        QByteArray *bytearrayToSend = new QByteArray();
        QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
        bytearrayStreamOut << (*message);

        // Send the datagram 
        qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
        if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
        // qDebug() << (*message);
        qDebug() << mType << " from " << sockRecv->getMyPort() <<" has been sent to " << port << "| size: " << int64Status;
        //qDebug() << "send the map: " << *message;

        if (!(ackHist->contains(OriginSM + "[Ori||Seq]" + QString::number(SeqNoSM + 1))))
        {
            // Set timer to receive the remote peer's acknowledgment
            QSignalMapper *mapper = new QSignalMapper(this);
            timerForAck= new QTimer(this);
            timerForAck->setSingleShot(true);
            connect(timerForAck, SIGNAL(timeout()), mapper, SLOT(map()));
            connect(mapper, SIGNAL(mapped(QString)), this, SLOT(fwdMessage(QString)));
            mapper->setMapping(timerForAck, fwdInfo);
            timerForAck->start(2000);
        }
        else
            ackHist->remove(ackHist->indexOf(OriginSM + "[Ori||Seq]" + QString::number(SeqNoSM + 1)));
    }
    else if (mType == "SM" && isNoForward == false)
    {
        QVariantMap wantValue;
        wantValue.insert(OriginSM, SeqNoSM);
        //qDebug() << wantValue;
        message = new QVariantMap();
        message->insert("Want", wantValue);
        
        // Serialize 
        QByteArray *bytearrayToSend = new QByteArray();
        QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
        bytearrayStreamOut << (*message);

        // Send the datagram 
        qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
        if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
        // qDebug() << (*message);
        qDebug() << mType << " from " << sockRecv->getMyPort() <<" has been sent to " << port << "| size: " << int64Status;
    }
    else if (mType == "RM")
    {
        message = new QVariantMap();
        message->insert("Origin", OriginSM);
        message->insert("SeqNo", SeqNoSM);

        // if it is a forwarding message, add lastIP, lastPort
        if (message->value("Origin").toString() != *myOrigin)
        {
            message->insert(tr("LastIP"), lastIP->toString());
            message->insert(tr("LastPort"), lastPort);
            //qDebug() << "RRRRRRRRRRRRRRRR" << *message;
        }
    
        // Serialize 
        QByteArray *bytearrayToSend = new QByteArray();
        QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
        bytearrayStreamOut << (*message);

        // Send the datagram 
        qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
        if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
        qDebug() << mType << " from " << sockRecv->getMyPort() <<" has been sent to " << port << "| size: " << int64Status;

    }
}


// ----------------------------------------------------------------------
// respond to messages received
void PeersterDialog::
gotRecvMessage()
{
    while (sockRecv->hasPendingDatagrams())
    {
        // read datagram into an instance of QByteArray
        QByteArray *bytearrayRecv = new QByteArray();
        bytearrayRecv->resize(sockRecv->pendingDatagramSize());
        QHostAddress senderAddr;
        quint16 senderPort;
        qint64 int64Status = sockRecv->readDatagram(bytearrayRecv->data(), bytearrayRecv->size(), 
            &senderAddr, &senderPort);
        if (int64Status == -1) exit(1); 

        // Whether should it be added to peer list as a new peer?
        bool containsPeer = false;
        for (int i = 0; i < peerList->size(); i++)
        {
            if (peerList->at(i).getIP() == senderAddr && peerList->at(i).getPort() == senderPort)
                containsPeer = true;
        }
        if (containsPeer == false)
        {
            // update it to the peer list
            Peer peer(senderAddr.toString(), senderAddr, senderPort);
            peerList->append(peer);

            // update it to the list view
            addrPortStrList.append(senderAddr.toString() + ":" + QString::number(senderPort));
            ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);
        }

        // record the last ip and port info
        *lastIP = senderAddr;
        lastPort = senderPort;

        // de-serialize
        QVariantMap recvMessage;
        QDataStream bytearrayStreamIn(bytearrayRecv, QIODevice::ReadOnly);
        bytearrayStreamIn >> recvMessage;
        // qDebug() << "recv the map: " << recvMessage;

        // treat acknowledgment (namely, status message) and gossip chat message differently.
        // If it is an status message (SM), namely acknowledgment
        if (recvMessage.contains("Want")) 
        {
            qDebug() << "Received new SM from " << senderPort;
            QMapIterator <QString, QVariant> iter(recvMessage.value("Want").toMap());
            iter.next();
            QString recvOrigin = iter.key();
            quint32 recvSeqNo = iter.value().toInt();
            // qDebug() << "Want recvOrigin = " << recvOrigin;
            // qDebug() << "Want recvSeqNo= " << recvSeqNo;

            ackHist->append(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo));
            
            //qDebug() << "ackHist" << ackHist->at(0);
            if (updateStatusMap->contains(recvOrigin))
            {
                quint32 mySeqNo = updateStatusMap->value(recvOrigin).toInt();
                if (mySeqNo + 1 == recvSeqNo) 
                {
                    if (qrand()%2 == 0)
                    {
                        // Random pick up a peer from peerlist
                        Peer destPeer = peerList->at(qrand()%(peerList->size()));

                        // forward
                        QString fwdInfo = recvOrigin + "[Ori||Seq]" + updateStatusMap->value(recvOrigin).toString()
                                + "[-ADDRIS>]" + destPeer.getIP().toString() 
                                + "[-PORTIS>]" + QString::number(destPeer.getPort())
                                + "[-METYPE>]" + "GM";
                        fwdMessage(fwdInfo);
                    }
                    else {; } // Cease
                    //qDebug() << "pop pop haha ";
                }
                else if (mySeqNo + 1 > recvSeqNo)
                {    
                    // You need new message
                    // Send my gossip message one by one
                    for (quint32 i = recvSeqNo; i <= mySeqNo; i++)
                    {
                       QString fwdInfo2 = recvOrigin + "[Ori||Seq]" + QString::number(i)
                          + "[-ADDRIS>]" + senderAddr.toString()  
                          + "[-PORTIS>]" + QString::number(senderPort) 
                          + "[-METYPE>]" + "GM"; 
                       fwdMessage(fwdInfo2);
                    }
                }
                else if (mySeqNo + 1 < recvSeqNo)
                {
                    // I need new message
                    // Send my status message 
                    QString fwdInfo2 = recvOrigin + "[Ori||Seq]" + QString::number(mySeqNo + 1) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo2);
                }
            } 
            else // not contain orgin 
            {
                QString fwdInfo2 = recvOrigin + "[Ori||Seq]" + QString::number(1) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
                fwdMessage(fwdInfo2);
            }
        }
        if (recvMessage.contains("ChatText") && recvMessage.contains("Origin") && recvMessage.contains("SeqNo")) // It is a Gossip Message (GM) 
        {
            qDebug() << "received new GM from " << senderPort;
            QString recvOrigin = recvMessage.value("Origin").toString();
            quint32 recvSeqNo = recvMessage.value("SeqNo").toInt();

            //ACK
            QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo+1) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
            fwdMessage(fwdInfo);
            // update the nextHopTable
            if (recvOrigin != *myOrigin)
            {
                updateRoutOriSeqMap->insert(recvOrigin, recvSeqNo);
                nextHopTable->insert(recvOrigin, *(new QPair<QHostAddress, quint16>(senderAddr, senderPort)));
                // Add it to the All dest list view if not exist
                if (!originStrList.contains(recvOrigin))
                {
                    originStrList.append(recvOrigin);
                    ((QStringListModel*) originListView->model())->setStringList(originStrList);
                }
            }

            // NAT punching If there new possible neighbors identified by lastIp and lastPort, then add them to direct neighbor
            // LOOKUP
            if (recvMessage.contains("LastIP") && recvMessage.contains("LastPort"))
            {
                QString recvLastIP = recvMessage.value("LastIP").toString();
                quint16 recvLastPort = recvMessage.value("LastPort").toInt();
                QString ipaddr_port = recvLastIP + ":" + QString::number(recvLastPort);
                bool containsPeer = false;
                for (int i = 0; i < peerList->size(); i++)
                {
                    if (peerList->at(i).getIP() == QHostAddress(recvLastIP) && peerList->at(i).getPort() == recvLastPort)
                        containsPeer = true;
                }
                /*
                qDebug() << "AAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
                qDebug() << "AAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
                qDebug() << "AAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
                qDebug() << !addrPortStrList.contains(ipaddr_port);
                qDebug() << (containsPeer == false) ;
                qDebug() << (QHostAddress(recvLastIP) != QHostAddress::LocalHost);
                qDebug() <<  (recvLastPort != sockRecv->getMyPort());
                */
                if (!addrPortStrList.contains(ipaddr_port) && containsPeer == false && (QHostAddress(recvLastIP) != QHostAddress::LocalHost || recvLastPort != sockRecv->getMyPort()))
                {
                    // update it to the peer list
                    Peer peer(tr("Unknown Host"), QHostAddress(recvLastIP), recvLastPort);
                    peerList->append(peer);

                    // update it to the list view
                    addrPortStrList.append(ipaddr_port);
                    ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);
                }
            }

            if (updateStatusMap->contains(recvOrigin))
            {
                quint32 mySeqNo = updateStatusMap->value(recvOrigin).toInt();
                if ( mySeqNo + 1 == recvSeqNo)
                {
                    // It is an exact new message for me
                    // update message 
                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));

                    textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());
                    
                    // broadcast message
                    for (int i = 0; i < peerList->size(); i++)
                    {
                        Peer destPeer = peerList->at(i);
                        // forward
                        QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo)
                                + "[-ADDRIS>]" + destPeer.getIP().toString() 
                                + "[-PORTIS>]" + QString::number(destPeer.getPort())
                                + "[-METYPE>]" + "GM";
                        fwdMessage(fwdInfo);
                    }
                }
                else
                {
                    // I have not received previous messages
                    // send my status message
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(mySeqNo + 1) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);
                }
            }
            else // not contain origin
            {
                if ( 1 == recvSeqNo) 
                {
                    // It is an exact new message for me
                    // receive and update the message
                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));

                    // if it is a gossip message then display
                    textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());
                    
                    // broadcast message
                    for (int i = 0; i < peerList->size(); i++)
                    {
                        Peer destPeer = peerList->at(i);
                        // forward
                        QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo)
                                + "[-ADDRIS>]" + destPeer.getIP().toString() 
                                + "[-PORTIS>]" + QString::number(destPeer.getPort())
                                + "[-METYPE>]" + "GM";
                        fwdMessage(fwdInfo);
                    }
                }
                else
                {
                    // I have not received previous messages
                    // Send my status message
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(1) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);
                }
            }
        }
        if (!recvMessage.contains("ChatText") && recvMessage.contains("Origin") && recvMessage.contains("SeqNo")) // It is a Routing Message (RM)
        {
            qDebug() << "received RM from " << senderPort;
            QString recvOrigin = recvMessage.value("Origin").toString();
            quint32 recvSeqNo = recvMessage.value("SeqNo").toInt();


            if (recvOrigin != *myOrigin)
            {
                    // If there new possible neighbors identified by lastIp and lastPort, then add them to direct neighbor
                    // LOOKUP
                    if (recvMessage.contains("LastIP") && recvMessage.contains("LastPort"))
                    {
                        QString recvLastIP = recvMessage.value("LastIP").toString();
                        quint16 recvLastPort = recvMessage.value("LastPort").toInt();
                        QString ipaddr_port = recvLastIP + ":" + QString::number(recvLastPort);
                        bool containsPeer = false;
                        for (int i = 0; i < peerList->size(); i++)
                        {
                            if (peerList->at(i).getIP() == QHostAddress(recvLastIP) && peerList->at(i).getPort() == recvLastPort)
                                containsPeer = true;
                        }
                        if (!addrPortStrList.contains(ipaddr_port) && containsPeer == false && (QHostAddress(recvLastIP) != QHostAddress::LocalHost || recvLastPort != sockRecv->getMyPort()))
                        {
                            // update it to the peer list
                            Peer peer(tr("Unknown Host"), QHostAddress(recvLastIP), recvLastPort);
                            peerList->append(peer);

                            // update it to the list view
                            addrPortStrList.append(ipaddr_port);
                            ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);
                        }
                    }


                    quint32 myRoutSeqNo = updateRoutOriSeqMap->value(recvOrigin).toInt();
                    if (updateRoutOriSeqMap->contains(recvOrigin) && myRoutSeqNo == recvSeqNo) // It is the routing message I have received
                    {}
                    else // It is a new routing message I have not heard of
                    {
                        //  Exchange my routing message 
                        /*
                        QString fwdInfo = *myOrigin + "[Ori||Seq]" + QString::number(1)
                            + "[-ADDRIS>]" + senderAddr.toString()
                            + "[-PORTIS>]" + QString::number(senderPort)
                            + "[-METYPE>]" + "RM";
                        fwdMessage(fwdInfo);
                        */

                        // update the nextHopTable
                        updateRoutOriSeqMap->insert(recvOrigin, recvSeqNo);
                        nextHopTable->insert(recvOrigin, *(new QPair<QHostAddress, quint16>(senderAddr, senderPort)));
                        // Add it to the All dest list view if not exist
                        if (!originStrList.contains(recvOrigin))
                        {
                            originStrList.append(recvOrigin);
                            ((QStringListModel*) originListView->model())->setStringList(originStrList);
                        }
                        // broadcast message
                        for (int i = 0; i < peerList->size(); i++)
                        {
                            Peer destPeer = peerList->at(i);
                            QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo)
                                    + "[-ADDRIS>]" + destPeer.getIP().toString() 
                                    + "[-PORTIS>]" + QString::number(destPeer.getPort())
                                    + "[-METYPE>]" + "RM";
                            fwdMessage(fwdInfo);
                        }
                    }
            }
        }
        if (recvMessage.contains("Dest") && recvMessage.contains("HopLimit") )  // It is a private message | block request | block reply | search reply
        {
            QString dest = recvMessage.value("Dest").toString();
            if (dest  == *myOrigin) // If it is sent to me
            {
                if (recvMessage.contains("ChatText"))
                    // if it is a private message
                    textview->append(senderAddr.toString() + ":" + QString::number(senderPort) + " > " + recvMessage.value("ChatText").toString());
                if (recvMessage.contains("Origin") && recvMessage.contains("BlockRequest"))
                {
                    // if it is a block request message
                    QByteArray requestHash = recvMessage.value("BlockRequest").toByteArray();
                    textview->append(senderAddr.toString() + ":" + QString::number(senderPort) + " > " + (QCA::arrayToHex(recvMessage.value("BlockRequest").toByteArray())));
                    // find file in local filesMetas
                    for (QVector<FileMetaData*>::iterator it = filesMetas.begin(); it < filesMetas.end(); it++)
                    {
                        if ((*it)->contains(requestHash)) 
                        {
                            // if I have the file, pack the file into QByteArray data
                            QFile readF((*it)->getFilePath(requestHash)); 
                            qDebug() << (*it)->getFilePath(requestHash);
                            if (readF.open(QIODevice::ReadOnly))
                            {
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
                if (recvMessage.contains("Origin") && recvMessage.contains("BlockReply") && recvMessage.contains("Data"))
                {
                    // if it is a block reply message
                    // check the package with hash
                    QByteArray data(recvMessage.value("Data").toByteArray());


                    QCA::MemoryRegion hashA;
                        QCA::Hash shaHash("sha256");
                        shaHash.update(data);
                        hashA = shaHash.final();
                        //textview->append(QCA::arrayToHex(hashA.toByteArray()));
                    /*
                    QFile readFile("tmp");
                    if (readFile.open(QIODevice::ReadOnly))
                    {
                        QCA::Hash shaHash("sha256");
                        shaHash.update(&readFile);
                        readFile.close();
                        hashA = shaHash.final();
                    }
                    */
                    QByteArray replyHash(recvMessage.value("BlockReply").toByteArray());
                    if (hashA.toByteArray() == replyHash)
                    {
                        textview->append("Recv directly a block successfully, saved as " + QCA::arrayToHex(replyHash));

                        // write to file 
                        QFile writeFile(QCA::arrayToHex(replyHash));
                        if (writeFile.open(QIODevice::WriteOnly))
                        {
                            QDataStream fout(&writeFile);
                            fout << data;
                            writeFile.close();
                        }
                    }
                }
                if (recvMessage.contains("Origin") && recvMessage.contains("SearchReply") && recvMessage.contains("MatchNames") && recvMessage.contains("MatchIDs"))
                {
                    // if it is a search reply message
                    // TODO
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
                            recvFilesMetas.append(new FileMetaData(matchNames.at(i).toString(), matchIDs.at(i).toByteArray(), origin));
                            // update search results list view
                            searchResultsStringList->append(matchNames.at(i).toString());
                            ((QStringListModel*) searchResultsListView->model())->setStringList(*searchResultsStringList);
                        }
                    }
                }
            }
            else if (recvMessage.value("HopLimit").toInt() > 0 && isNoForward == false) // If -noforward is not launched
            {
                recvMessage.insert("HopLimit", (quint32)(recvMessage.value("HopLimit").toInt()-1));

                // Serialize 
                QByteArray *bytearrayToSend = new QByteArray();
                QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
                bytearrayStreamOut << recvMessage;

                // Send the datagram 
                if (nextHopTable->contains(dest))
                {
                    QHostAddress host = nextHopTable->value(dest).first;
                    quint16 port = nextHopTable->value(dest).second;

                    qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
                    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
                    qDebug() << "PM" << " from " << sockRecv->getMyPort() <<" has been sent to " << port << "| size: " << int64Status;
                }
            }
        }
        // if it is a search request message
        if (recvMessage.contains("Origin") && recvMessage.contains("Search") && recvMessage.contains("Budget"))
        {
            QString origin = recvMessage.value("Origin").toString();
            QString search = recvMessage.value("Search").toString();
            quint32 budget = recvMessage.value("Budget").toInt(); 
            // search names of the files stored locally
            // find file in local filesMetas
            QVariantList matchNames;
            QVariantList matchIDs;
            bool isFound = false;
            for (QVector<FileMetaData*>::iterator it = filesMetas.begin(); it < filesMetas.end(); it++)
            {
                if ((*it)->contains(search))
                {
                    isFound = true;
                    matchNames.push_back((*it)->getFileNameOnly());
                    matchIDs.push_back((*it)->getBlockListHash());
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
                if ((float)newBudget/nodes <= 1) budgetPerNode = 1;
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
bool PeersterDialog::eventFilter(QObject *obj, QEvent *event)
{
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
// respond to double click on search results and download the file
void PeersterDialog::
downloadFile(const QModelIndex& index)
{
    // TODO
    FileMetaData *downloadTaget = recvFilesMetas.at(index.row());
    sendBlockRequest(downloadTaget->getOriginNID(), *myOrigin, 10, downloadTaget->getBlockListHash());
    textview->append("download");
}

// ---------------------------------------------------------------------
// private message window
PrivateMessage::
PrivateMessage(const QModelIndex& index, PeersterDialog* p2p)
{
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

bool PrivateMessage::eventFilter(QObject *obj, QEvent *event)
{
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
void PrivateMessage::gotReturnPressed()
{
    QVariantMap privateMessageMap;
    privateMessageMap.insert("Dest", dest);
    privateMessageMap.insert("ChatText", textedit->toPlainText());
    privateMessageMap.insert("HopLimit", (quint32)10);

    // Serialize 
    QByteArray *bytearrayToSend = new QByteArray();
    QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
    bytearrayStreamOut << privateMessageMap;

    // Send the datagram 
    QHostAddress host = upperP2P->nextHopTable->value(dest).first;
    quint16 port = upperP2P->nextHopTable->value(dest).second;

    qint64 int64Status = upperP2P->sockRecv->writeDatagram(*bytearrayToSend, host, port);
    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
    qDebug() << "PM" << " from " << upperP2P->sockRecv->getMyPort() <<" has been sent to " << port << "| size: " << int64Status;

    textview->append("me > " + textedit->toPlainText());
    textedit->clear();
}

// ---------------------------------------------------------------------
// send block request message
void PeersterDialog::
sendBlockRequest(const QString dest, const QString origin, const quint32 hopLimit, const QByteArray &blockRequest)
{
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

    textview->append("send block request");

    delete message;
    delete bytearrayToSend;
}

// ---------------------------------------------------------------------
// reply block request
void PeersterDialog::
sendBlockReply(const QString dest, const QString origin, const quint32 hopLimit, const QByteArray &blockReply, const QByteArray &data)
{
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

    textview->append("reply block request");

    delete message;
    delete bytearrayToSend;
}

// ---------------------------------------------------------------------
// when searchFileBtn is clicked on 
void PeersterDialog::
onSearchFileBtnClicked()
{
    QPair<QString, quint32> kv(searchKeyWords->text(), 1);
    searchQueue->enqueue(kv);
    // sendSearchRequest(*myOrigin, searchKeyWords->text(), 2);
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
updateSearchQueue() 
{
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
sendSearchRequest(const QString origin, const QString search, const quint32 budget, QHostAddress host, quint16 port)
{
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
sendSearchReply(const QString dest, const QString origin, const quint32 hopLimit, const QString searchReply, const QVariantList matchNames, const QVariantList matchIDs)
{
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

    textview->append("reply search");

    delete message;
    delete bytearrayToSend;
}

// ---------------------------------------------------------------------
// respond to share button click
void PeersterDialog::
onShareFileBtnClicked()
{
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
onRequestFileBtnClicked()
{
    QCA::init();
    QByteArray blockRequest(QCA::hexToArray(targetFID->text()));
    sendBlockRequest(targetNID->text(), *myOrigin, 10, blockRequest);
}

// ---------------------------------------------------------------------
// constructor for FileMetaData when given a file name
FileMetaData::
FileMetaData(const QString fn): 
    fileNameWithPath(fn), 
    fileNameOnly(QDir(fileNameWithPath).dirName())
{ 
    // if tmp_blocks does not exist, create the temp directory
    if (!QDir(QDir::currentPath() + QObject::tr("/tmp_blocks/")).exists())
            QDir().mkdir(QDir::currentPath() + QObject::tr("/tmp_blocks/"));
    // split the file to currentPath/tmp_blocks/
    splitFile(QDir::currentPath()+QObject::tr("/tmp_blocks/"), 8*1024);
    // qDebug() << getSubFilesNum();
}


// ---------------------------------------------------------------------
// split the file into blocks stored in outDir
void FileMetaData::
splitFile(const QString outDir, const int blockSize)
{
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
        QDataStream fout(&qfOut);
        fout << blockList;
        qfOut.close();
    }
    // test the file's hash 
    QFile readMeta(metaFileName);
    QByteArray hashAll;
    if (readMeta.open(QIODevice::ReadOnly))
    {
        shaHash.update(&readMeta);
        hashAll = shaHash.final().toByteArray();
        qDebug() << QCA::arrayToHex(hashAll);
        readMeta.close();
    }
    blockListHash.append(hashAll);
    // qDebug() << "path:" << getFilePath(hashA.toByteArray());

    // debug: unite file for checking
    /*
    QFile unitedFile(outDir + QDir(fileNameWithPath).dirName() + tr("_united"));
    unitedFile.open(QIODevice::WriteOnly);
    QDataStream unitedFlow(&unitedFile);
    part = 0;
    while(QFile::exists(outDir + QDir(fileNameWithPath).dirName() + tr("_") + QString::number(part))){
        QFile qfOut(outDir + QDir(fileNameWithPath).dirName() + tr("_") + QString::number(part++));
        qfOut.open(QIODevice::ReadOnly);
        QDataStream fout(&qfOut);
        int readSize = fout.readRawData(buffer, blockSize);
        qfOut.close();
        unitedFlow.writeRawData(buffer, readSize);
    }
    unitedFile.close();
    */
}

// ---------------------------------------------------------------------
// main program entry
int main(int argc, char **argv)
{
    // support crypto
    QCA::Initializer qcainit;
	// Initialize Qt toolkit
	QApplication app(argc,argv);

    PeersterDialog dialog;

	// Enter the Qt main loop; everything else is event driven
	return dialog.exec();
}

