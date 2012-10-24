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
    QLabel *destListLabel = new QLabel("All dest(DoubleClick)", this);
    // destListLabel.setText("All destinations (DoubleClick to talk)");
    QLabel *neighborListLabel = new QLabel("    Direct neighbors", this);
    //neighborListLabel.setText("Direct neighbors");

    // file sharing
    shareFileBtn = new QPushButton("Share File...", this);
    targetNID = new QLineEdit();
    targetFID = new QLineEdit();
    connect(shareFileBtn, SIGNAL(clicked()), this, SLOT(onShareFileBtnClicked()));

	// Lay out the widgets to appear in the main window.
	layout = new QGridLayout();
    // first column 
	layout->addWidget(textview, 0, 0, 13, 1);
	layout->addWidget(textedit, 14, 0, 1, 1);
    // second column
    layout->addWidget(destListLabel, 0, 17, 1, 1);
    layout->addWidget(originListView, 1, 17, -1, 1);
    // third column
    layout->addWidget(neighborListLabel, 0, 18, 1, 1);
    layout->addWidget(addAddrPort, 1, 18, 1, 1); 
    layout->addWidget(addrPortListView, 2, 18, -1, 1);
    // firth column
	layout->addWidget(shareFileBtn, 0, 19);
	layout->addWidget(targetNID, 1, 19);
	layout->addWidget(targetFID, 2, 19);

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
    timerForRM->start(30000);

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
        // Random pick up a peer from peerlist
                    Peer destPeer = peerList->at(qrand()%(peerList->size()));

        // Send the message
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

void PeersterDialog::fwdMessage(QString fwdInfo)
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
                        updateRoutOriSeqMap->insert(recvOrigin, recvSeqNo);
                        nextHopTable->insert(recvOrigin, *(new QPair<QHostAddress, quint16>(senderAddr, senderPort)));

                        //  Exchange my routing message 
                        /*
                        QString fwdInfo = *myOrigin + "[Ori||Seq]" + QString::number(1)
                            + "[-ADDRIS>]" + senderAddr.toString()
                            + "[-PORTIS>]" + QString::number(senderPort)
                            + "[-METYPE>]" + "RM";
                        fwdMessage(fwdInfo);
                        */
         
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
        if (recvMessage.contains("Dest") && recvMessage.contains("ChatText") && recvMessage.contains("HopLimit") )  // It is a private message 
        {
            QString dest = recvMessage.value("Dest").toString();
            if (dest  == *myOrigin) // If it is sent to me
                textview->append(senderAddr.toString() + ":" + QString::number(senderPort) + " > " + recvMessage.value("ChatText").toString());
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
    }

}

// Tian. catch the returnPressed event to send the message
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

PrivateMessage::PrivateMessage(const QModelIndex& index, PeersterDialog* p2p)
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
// when Request File button clicked
void PeersterDialog::
onRequestFileBtnClicked()
{

}

// ---------------------------------------------------------------------
// send request message
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

    /*
    // Send the datagram 
    qint64 int64Status = mySock.writeDatagram(*bytearrayToSend, host, port);
    if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
    // qDebug() << (*message);
    qDebug() << mType << " from " << mySock.getMyPort() <<" has been sent to " << port << "| size: " << int64Status;
    //qDebug() << "send the map: " << *message;
    */

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
// constructor for FileMetaData
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
        int readSize = fin.readRawData(buffer, blockSize);
        QFile qfOut(outDir + fileNameOnly + QObject::tr("_") + QString::number(part));
        if (qfOut.open(QIODevice::WriteOnly)) {
            QDataStream fout(&qfOut);
            fout.writeRawData(buffer, readSize);
            qfOut.close();
        }
        // hash
        QFile qfHash(outDir + fileNameOnly + QObject::tr("_") + QString::number(part));
        if (qfHash.open(QIODevice::ReadOnly)) {
            shaHash.update(&qfHash);
            QCA::MemoryRegion hashA = shaHash.final();
            qDebug() << QCA::arrayToHex(hashA.toByteArray());
            // save to blockList
            blockList.append(hashA.toByteArray());
            qfHash.close();
            shaHash.clear();
        }
        ++part;
    } while (!fin.atEnd());
    qf.close();
    // calculate the hash of blockList
    shaHash.update(blockList);
    QCA::MemoryRegion hashA = shaHash.final();
    qDebug() << "bloclist_has = " << QCA::arrayToHex(hashA.toByteArray());
    blockList.append(hashA.toByteArray());

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

