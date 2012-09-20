#include <unistd.h>

#include <QApplication>
#include <QDebug>

#include "main.hh"

TabDialog::TabDialog(QWidget* parent)
    : QDialog(parent)
{
    tabWidget = new QTabWidget;
    tabWidget->addTab(new PointToPointMessaging(), tr("Point2Point Messaging"));
    tabWidget->addTab(new GossipMessagingEntry(), tr("Gossip Messaging"));

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(tabWidget);
    setLayout(mainLayout);
    
    setWindowTitle(tr("Peerster"));
    this->resize(500, 300);
} 


GossipMessagingEntry::GossipMessagingEntry(QWidget* parent)
    : QWidget(parent)
{
    switchButton = new QPushButton("Switch On", this);
    switchButton->setAutoDefault(false);

    layout = new QVBoxLayout();
    layout->addWidget(switchButton);
    setLayout(layout);

    connect(switchButton, SIGNAL(clicked()),
        this, SLOT(switchButtonClicked()));
}
    
void GossipMessagingEntry::switchButtonClicked()
{
    if (switchButton->text() == "Switch On")
    {
        switchButton->setText("Switch Off");
        gm = new GossipMessaging();
        layout->addWidget(gm);
        setLayout(layout);
    }
    else 
    {
        switchButton->setText("Switch On");
        delete gm;
    }

}



GossipMessaging::GossipMessaging(QWidget* parent)
    : QWidget(parent)
{
    // generate rand seed
    qsrand(time(0));
    // generate my Origin
    randomOriginID = qrand();
    myOrigin = new QString(QHostInfo::localHostName() + QString::number(randomOriginID));
    // Initialize SeqNo. Wait for the first message to send
    SeqNo = 1;
    // Initialize recvMessageMap <Origin+"[Ori||Seq]"+SeqNo, Messsage>
    // Used to store all the coming messages 
    recvMessageMap = new QVariantMap(); 
    updateStatusMap = new QVariantMap(); 
    // Used to record the ack to stop resending
    ackHist = new QVector<QString>();
    // Used to record peer information
    peerList = new QList<Peer>();

	setWindowTitle("Peerster: " + *myOrigin);


	// Read-only text box where we display messages from everyone.
	textview = new QTextEdit(this);
	textview->setReadOnly(true);

    // Input peer information
    addAddrPort = new QLineEdit();
    // List view to display peers 
    addrPortListView = new QListView();
    addrPortListView->setModel(new QStringListModel(addrPortStrList));
    addrPortListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    //NetSocket sock;
    sockRecv = new NetSocket();
	if (!sockRecv->bind())
		exit(1);
    connect(sockRecv, SIGNAL(readyRead()),
        this, SLOT(gotRecvMessage()));
    
    // If there are CLI arguments to set hostname:port or ipaddr:port, add them
    for (int i = 1; i < qApp->argc(); i++)
    {
        qDebug() << qApp->argv()[i];
        QString inputAddrPort = qApp->argv()[i];
        QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
    
        QHostInfo::lookupHost(addr, 
            this, SLOT(lookedUpBeforeInvoke(QHostInfo)));
    }

    // add three local ports
    for (quint16 q = sockRecv->getMyPortMin(); q <= sockRecv->getMyPortMax(); q++)
    {
        if (q != sockRecv->getMyPort())
        {
            Peer peer(QHostInfo::localHostName(), QHostAddress("127.0.0.1"), q);
            peerList->append(peer);
            addrPortStrList.append(QHostAddress("127.0.0.1").toString() + ":" + QString::number(q));
            ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);
        }
    }

    // Input chat message from text edit
	textedit= new QTextEdit(this);
    textedit->installEventFilter(this);
    
	// Lay out the widgets to appear in the main window.
	QGridLayout *layout = new QGridLayout();
	layout->addWidget(textview, 0, 0, 13, 1);
	layout->addWidget(textedit, 14, 0, 1, 1);
    layout->addWidget(addrPortListView, 2, 18, -1, 1);
    layout->addWidget(addAddrPort, 0, 18, 1, 1); 
    textedit->setFocus();

	setLayout(layout);

    // Anti-entropy
    // Set timer to send status message 
    timerForAntiEntropy = new QTimer(this);
    connect(timerForAntiEntropy, SIGNAL(timeout()), 
        this, SLOT(antiEntropy()));
    timerForAntiEntropy->start(10000);

    // Add hostname:port or ipaddr:port 
    connect(addAddrPort, SIGNAL(returnPressed()),
        this, SLOT(addrPortAdded()));

}

GossipMessaging::~GossipMessaging()
{
    delete textview;
	delete textedit;
    delete addAddrPort;
    delete addrPortListView;
    delete recvMessageMap;
    delete updateStatusMap;
    delete myOrigin;
    delete peerList;

    delete sockRecv;
}

void GossipMessaging::lookedUp(const QHostInfo& host)
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

            addAddrPort->clear();
        }
    }
}

void GossipMessaging::lookedUpBeforeInvoke(const QHostInfo& host)
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
    

void GossipMessaging::addrPortAdded()
{
    QString inputAddrPort = addAddrPort->text();
    QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
    // qDebug() << addr << ":" << port;
    
    QHostInfo::lookupHost(addr, 
        this, SLOT(lookedUp(QHostInfo)));
} 

void GossipMessaging::antiEntropy()
{
    if (updateStatusMap->contains(*myOrigin))
    {
        // Random pick up a peer from peerlist
                    Peer destPeer = peerList->at(qrand()%(peerList->size()));

        // Send the message
        QString fwdInfo = *myOrigin + "[Ori||Seq]" + QString::number(updateStatusMap->value(*myOrigin).toInt() + 1)
            + "[-ADDRIS>]" + destPeer.ipaddr.toString()
            + "[-PORTIS>]" + QString::number(destPeer.port)
            + "[-METYPE>]" + "SM";
        fwdMessage(fwdInfo);
        qDebug() << "antiEntropy triggered!!!";
    }
}


void GossipMessaging::gotReturnPressed()
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
        + "[-ADDRIS>]" + destPeer.ipaddr.toString()
        + "[-PORTIS>]" + QString::number(destPeer.port)
        + "[-METYPE>]" + "GM";
    fwdMessage(fwdInfo);

    textview->append("me > " + textedit->toPlainText());
    textedit->clear();
    SeqNo++;
}

void GossipMessaging::fwdMessage(QString fwdInfo)
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
    if (mType == "GM")
    {
        message = new QVariantMap(recvMessageMap->value(OriSeq).toMap());
        
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
    else if (mType == "SM")
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

    /* deprecated, timer only for gossip message
    if (!(ackHist->contains(OriginSM + "[Ori||Seq]" + QString::number(SeqNoSM + 1))))
    {
        // Serialize 
        QByteArray *bytearrayToSend = new QByteArray();
        QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
        bytearrayStreamOut << (*message);

        // Send the datagram 
        qint64 int64Status = sockRecv->writeDatagram(*bytearrayToSend, host, port);
        if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
        // qDebug() << (*message);
        qDebug() << mType << " from " << sockRecv->getMyPort() <<" has been sent to " << port << "| size: " << int64Status;

        // Set timer to receive the remote peer's acknowledgment
        if (mType == "GM")
        {
            QSignalMapper *mapper = new QSignalMapper(this);
            timerForAck= new QTimer(this);
            timerForAck->setSingleShot(true);
            connect(timerForAck, SIGNAL(timeout()), mapper, SLOT(map()));
            connect(mapper, SIGNAL(mapped(QString)), this, SLOT(fwdMessage(QString)));
            mapper->setMapping(timerForAck, fwdInfo);
            timerForAck->start(3000);
        }

        //qDebug() << "Send over! \n";
    }
    //else 
        //ackHist->remove(OriginSM + "[Ori||Seq]" + QString::number(SeqNoSM + 1));
    */
}


void GossipMessaging::gotRecvMessage()
{
    //while (sockRecv->hasPendingDatagrams())
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
            if (peerList->at(i).ipaddr == senderAddr && peerList->at(i).port == senderPort)
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
                                + "[-ADDRIS>]" + destPeer.ipaddr.toString() 
                                + "[-PORTIS>]" + QString::number(destPeer.port)
                                + "[-METYPE>]" + "GM";
                        fwdMessage(fwdInfo);
                    }
                    else {; } // Cease
                    //qDebug() << "pop pop haha ";
                }
                else if (mySeqNo + 1 > recvSeqNo)
                {    
                    // You need new message
                    // Ack
                    /*
                    QString fwdInfo = recvOrigin  + "[Ori||Seq]" + QString::number(recvSeqNo) // TODO mySeqNo or recvSeqNo?
                            + "[-ADDRIS>]" + senderAddr.toString() 
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);
                    */
                    
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
                    // ACK
                    /*
                    QString fwdInfo = recvOrigin  + "[Ori||Seq]" + QString::number(recvSeqNo) // TODO mySeqNo or recvSeqNo?
                            + "[-ADDRIS>]" + senderAddr.toString() 
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);
                    */

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
                //qDebug() << "Want & not contain origin" ;
                //qDebug() << "recvOrigin " << recvOrigin;
                // I need new message
                // ACK
                /*
                QString fwdInfo = recvOrigin  + "[Ori||Seq]" + QString::number(recvSeqNo) // TODO mySeqNo or recvSeqNo?
                            + "[-ADDRIS>]" + senderAddr.toString() 
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
                fwdMessage(fwdInfo);
                */

                QString fwdInfo2 = recvOrigin + "[Ori||Seq]" + QString::number(1) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
                fwdMessage(fwdInfo2);
            }
        }
        else if (recvMessage.contains("ChatText")) // It is a Gossip Message (GM)
        {
            qDebug() << "received new GM from " << senderPort;
            //qDebug() << recvMessage;
            QString recvOrigin = recvMessage.value("Origin").toString();
            quint32 recvSeqNo = recvMessage.value("SeqNo").toInt();
            //qDebug() << "recvOrigin: " << recvOrigin;
            //qDebug() << "recvSeqNo: " << recvSeqNo;
            //qDebug() << "senderPort: " << senderPort;

            //ACK
            QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo+1) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + QString::number(senderPort) 
                            + "[-METYPE>]" + "SM"; 
            fwdMessage(fwdInfo);

            if (updateStatusMap->contains(recvOrigin))
            {
                quint32 mySeqNo = updateStatusMap->value(recvOrigin).toInt();
                //qDebug() << "mySeqNo = " << mySeqNo ;
                if (mySeqNo + 1 == recvSeqNo)
                {
                    // It is an exact new message for me
                    // update message 
                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));

                    // display
                    textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());
                    
                    // Random pick up a peer from peer list
                    Peer destPeer = peerList->at(qrand()%(peerList->size()));

                    // forward
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo)
                            + "[-ADDRIS>]" + destPeer.ipaddr.toString() 
                            + "[-PORTIS>]" + QString::number(destPeer.port)
                            + "[-METYPE>]" + "GM";
                    fwdMessage(fwdInfo);
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
                if ( 1 == recvSeqNo )
                {
                    // It is an exact new message for me
                    // receive and update the message
                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));

                    textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());
                
                    // Random pick up a peer from peerlist
                    Peer destPeer = peerList->at(qrand()%(peerList->size()));

                    // forward
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo)
                            + "[-ADDRIS>]" + destPeer.ipaddr.toString()
                            + "[-PORTIS>]" + QString::number(destPeer.port)
                            + "[-METYPE>]" + "GM";
                    fwdMessage(fwdInfo);
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
        else 
        {
            qDebug() << "not normal messages";
            exit(-1);
        }
    }

    // qDebug() << "recv over!\n";
}

// Tian. catch the returnPressed event to send the message
bool GossipMessaging::eventFilter(QObject *obj, QEvent *event)
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
        // Tian added IP addr. here
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

PointToPointMessaging::PointToPointMessaging(QWidget* parent)
    : QWidget(parent)
{
    // Lab2: Initial next-hop routing table
    nextHopTable = new QHash<QString, QPair<QHostAddress, quint16> >();
   // generate rand seed
    qsrand(time(0));
    // generate my Origin
    randomOriginID = qrand();
    myOrigin = new QString(QHostInfo::localHostName() + QString::number(randomOriginID));
    // Initialize SeqNo. Wait for the first message to send
    SeqNo = 1;
    // Initialize recvMessageMap <Origin+"[Ori||Seq]"+SeqNo, Messsage>
    // Used to store all the coming messages 
    recvMessageMap = new QVariantMap(); 
    updateStatusMap = new QVariantMap(); 
    // Used to record the ack to stop resending
    ackHist = new QVector<QString>();
    // Used to record peer information
    peerList = new QList<Peer>();

	setWindowTitle("Peerster: " + *myOrigin);

	// Read-only text box where we display messages from everyone.
	textview = new QTextEdit(this);
	textview->setReadOnly(true);

    // Input peer information
    addAddrPort = new QLineEdit();
    // List view to display DIRECT peers ipaddress and ports
    addrPortListView = new QListView();
    addrPortListView->setModel(new QStringListModel(addrPortStrList));
    addrPortListView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // List view to display dest (DIRECT and INDIRECT) origin list
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
    
    // If there are CLI arguments to set hostname:port or ipaddr:port, add them
    for (int i = 1; i < qApp->argc(); i++)
    {
        qDebug() << qApp->argv()[i];
        QString inputAddrPort = qApp->argv()[i];
        QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
    
        QHostInfo::lookupHost(addr, 
            this, SLOT(lookedUpBeforeInvoke(QHostInfo)));
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
    QLabel *destListLabel = new QLabel("All destinations (Double-Click to talk)", this);
    // destListLabel.setText("All destinations (DoubleClick to talk)");
    QLabel *neighborListLabel = new QLabel("    Direct neighbors", this);
    //neighborListLabel.setText("Direct neighbors");
    
	// Lay out the widgets to appear in the main window.
	QGridLayout *layout = new QGridLayout();
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
    timerForRM->start(60000);

    // Add hostname:port or ipaddr:port 
    connect(addAddrPort, SIGNAL(returnPressed()),
        this, SLOT(addrPortAdded()));

}

void PointToPointMessaging::openPrivateMessageWin(const QModelIndex& index)
{
    //dest = (((QStringListModel*) index.model())->stringList()).at(index.row());
    pm = new PrivateMessage(index, this);
    pm->exec();
} 

PointToPointMessaging::~PointToPointMessaging()
{
    delete textview;
	delete textedit;
    delete addAddrPort;
    delete addrPortListView;
    delete recvMessageMap;
    delete updateStatusMap;
    delete myOrigin;
    delete peerList;

    delete sockRecv;
}

void PointToPointMessaging::lookedUp(const QHostInfo& host)
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
void PointToPointMessaging::lookedUpBeforeInvoke(const QHostInfo& host)
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
    

void PointToPointMessaging::addrPortAdded()
{
    QString inputAddrPort = addAddrPort->text();
    QString addr = inputAddrPort.left(inputAddrPort.lastIndexOf(":"));
    // qDebug() << addr << ":" << port;
    
    QHostInfo::lookupHost(addr, 
        this, SLOT(lookedUp(QHostInfo)));
} 

// broadcast routing messages to all direct neighbor peers
void PointToPointMessaging::broadcastRM()
{
    for (int i = 0; i < peerList->size(); i++)
    {
        QString fwdInfo = *myOrigin + "[Ori||Seq]" + QString::number(1)
            + "[-ADDRIS>]" + peerList->at(i).ipaddr.toString()
            + "[-PORTIS>]" + QString::number(peerList->at(i).port)
            + "[-METYPE>]" + "RM";
        fwdMessage(fwdInfo);
    }
}
void PointToPointMessaging::antiEntropy()
{
    if (updateStatusMap->contains(*myOrigin))
    {
        // Random pick up a peer from peerlist
                    Peer destPeer = peerList->at(qrand()%(peerList->size()));

        // Send the message
        QString fwdInfo = *myOrigin + "[Ori||Seq]" + QString::number(updateStatusMap->value(*myOrigin).toInt() + 1)
            + "[-ADDRIS>]" + destPeer.ipaddr.toString()
            + "[-PORTIS>]" + QString::number(destPeer.port)
            + "[-METYPE>]" + "SM";
        fwdMessage(fwdInfo);
        qDebug() << "antiEntropy triggered!!!";
    }
}


void PointToPointMessaging::gotReturnPressed()
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
        + "[-ADDRIS>]" + destPeer.ipaddr.toString()
        + "[-PORTIS>]" + QString::number(destPeer.port)
        + "[-METYPE>]" + "GM";
    fwdMessage(fwdInfo);

    textview->append("me > " + textedit->toPlainText());
    textedit->clear();
    SeqNo++;
}

void PointToPointMessaging::fwdMessage(QString fwdInfo)
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
    if (mType == "GM")
    {
        message = new QVariantMap(recvMessageMap->value(OriSeq).toMap());
        
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
    else if (mType == "SM")
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


void PointToPointMessaging::gotRecvMessage()
{
    //while (sockRecv->hasPendingDatagrams())
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
            if (peerList->at(i).ipaddr == senderAddr && peerList->at(i).port == senderPort)
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
                                + "[-ADDRIS>]" + destPeer.ipaddr.toString() 
                                + "[-PORTIS>]" + QString::number(destPeer.port)
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
        else if (recvMessage.contains("Origin") && recvMessage.contains("SeqNo")) // It is a Gossip Message (GM) or Routing Message (RM)
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

            if (updateStatusMap->contains(recvOrigin))
            {
                quint32 mySeqNo = updateStatusMap->value(recvOrigin).toInt();
                if (mySeqNo + 1 == recvSeqNo)
                {
                    // It is an exact new message for me
                    // update message 
                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));
                    // Lab2: update nextHopTable
                    if (recvOrigin != *myOrigin)
                    {
                        nextHopTable->insert(recvOrigin, *(new QPair<QHostAddress, quint16>(senderAddr, senderPort)));
                        qDebug() << "+++++++++++++" << nextHopTable->value(recvOrigin);
                        broadcastRM();
                        // Add it to the dest (DIRECT and INDIRECT) list view if not exist
                        if (!originStrList.contains(recvOrigin))
                        {
                            originStrList.append(recvOrigin);
                            ((QStringListModel*) originListView->model())->setStringList(originStrList);
                        }
                    } 
 
                    // Random pick up a peer from peer list
                    // broadcast
                    for (int i = 0; i < peerList->size(); i++)
                    {
                        Peer destPeer = peerList->at(i);

                        // if it is a gossip message then display
                        QString mType;
                        if (recvMessage.contains("ChatText"))
                        {
                            textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());
                            mType = "GM";
                        }
                        else
                            mType = "RM";
                        
                        // forward
                        QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo)
                                + "[-ADDRIS>]" + destPeer.ipaddr.toString() 
                                + "[-PORTIS>]" + QString::number(destPeer.port)
                                + "[-METYPE>]" + mType;
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
                if ( 1 == recvSeqNo )
                {
                    // It is an exact new message for me
                    // receive and update the message
                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));
                    // Lab2: update nextHopTable
                    if (recvOrigin != *myOrigin)
                    {
                        nextHopTable->insert(recvOrigin, *(new QPair<QHostAddress, quint16>(senderAddr, senderPort)));
                        qDebug() << "+++++++++++++" << nextHopTable->value(recvOrigin);
                        broadcastRM();
                        // Add it to the list view if not exist
                        if (!originStrList.contains(recvOrigin))
                        {
                            originStrList.append(recvOrigin);
                            ((QStringListModel*) originListView->model())->setStringList(originStrList);
                        }
                    }
                
                    // Random pick up a peer from peerlist
                    // broadcast
                    for (int i = 0; i < peerList->size(); i++)
                    {
                        Peer destPeer = peerList->at(qrand()%(peerList->size()));

                        QString mType;
                        if (recvMessage.contains("ChatText"))
                        {
                            mType = "GM";
                            textview->append(recvOrigin + " > " + recvMessage.value("ChatText").toString());
                        }
                        else 
                            mType = "RM";

                        // forward
                        QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo)
                                + "[-ADDRIS>]" + destPeer.ipaddr.toString()
                                + "[-PORTIS>]" + QString::number(destPeer.port)
                                + "[-METYPE>]" + mType;
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
        else if (recvMessage.contains("Dest") && recvMessage.contains("ChatText") && recvMessage.contains("HopLimit")) 
            // It is a private message
        {
            QString dest = recvMessage.value("Dest").toString();
            if (dest  == *myOrigin) // If it is sent to me
                textview->append("Private Message > " + recvMessage.value("ChatText").toString());
            else if (recvMessage.value("HopLimit").toInt() > 0)
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
            /* TODO extra support for one on one talk 
            if (!pm) // if private message window do not exist
            {
                // Add it to the list view
                addrPortStrList.append(recvOrigin);
                ((QStringListModel*) addrPortListView->model())->setStringList(addrPortStrList);

                pm = new PrivateMessage(addrPortListView->currentIndex(), this);
                pm->exec();
            }
            
            pm->textview->append(recvMessage.value("ChatText").toString());
            */
            
        }
        else 
        {
            qDebug() << "not normal messages";
            exit(-1);
        }
    }

    // qDebug() << "recv over!\n";
}

// Tian. catch the returnPressed event to send the message
bool PointToPointMessaging::eventFilter(QObject *obj, QEvent *event)
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

PrivateMessage::PrivateMessage(const QModelIndex& index, PointToPointMessaging* p2p)
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

int main(int argc, char **argv)
{
	// Initialize Qt toolkit
	QApplication app(argc,argv);

    TabDialog dialog;

	// Enter the Qt main loop; everything else is event driven
	return dialog.exec();
}

