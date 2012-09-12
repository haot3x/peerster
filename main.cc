
#include <unistd.h>

#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>

#include "main.hh"

ChatDialog::ChatDialog()
{
    // Tian: Generate partial origin with rand num
    qsrand(time(0));
    randomOriginID = qrand();
    myOrigin = new QString(QHostInfo::localHostName() + QString::number(randomOriginID));
    // Tian: Initialize SeqNo;
    SeqNo = 1;
    // Tian: Initialize recvMessageMap;
    recvMessageMap = new QVariantMap(); 
    updateStatusMap = new QVariantMap(); 
    ackHist = new QVector<QString>();

	setWindowTitle("Peerster");

	// Read-only text box where we display messages from everyone.
	// This widget expands both horizontally and vertically.
	textview = new QTextEdit(this);
	textview->setReadOnly(true);

	// Small text-entry box the user can enter messages.
	// This widget normally expands only horizontally,
	// leaving extra vertical space for the textview widget.
	//
	// You might change this into a read/write QTextEdit,
	// so that the user can easily enter multi-line messages.
    // Exercise 2. Tian modified the class
	textedit= new QTextEdit(this);
    textedit->installEventFilter(this);
    
	// Lay out the widgets to appear in the main window.
	// For Qt widget and layout concepts see:
	// http://doc.qt.nokia.com/4.7-snapshot/widgets-and-layouts.html
	QVBoxLayout *layout = new QVBoxLayout();
	layout->addWidget(textview);
	layout->addWidget(textedit);
    // Exercise 1. Tian added it to set focus on the textedit without having to click in it first.
    textedit->setFocus();

	setLayout(layout);

	// Register a callback on the textedit's returnPressed signal
	// so that we can send the message entered by the user.
    //NetSocket sock;
    sockRecv = new NetSocket();
	if (!sockRecv->bind())
		exit(1);
    connect(sockRecv, SIGNAL(readyRead()),
        this, SLOT(gotRecvMessage()));
    
    // Anti-entropy
    // Set timer to send status message 
    timerForAntiEntropy = new QTimer(this);
    connect(timerForAntiEntropy, SIGNAL(timeout()), this, SLOT(antiEntropy()));
    timerForAntiEntropy->start(10000);
}

void ChatDialog::antiEntropy()
{
    if (updateStatusMap->contains(*myOrigin))
    {
        // Pick up a random port in a line topo
        quint16 destPort = sockRecv->getMyPort();
        if (destPort == sockRecv->getMyPortMin()) destPort = destPort + 1;
        else if (destPort == sockRecv->getMyPortMax()) destPort = destPort - 1;
        else destPort = qrand()%2 == 0?destPort-1:destPort+1;

        // Send the message
        QString fwdInfo = *myOrigin + "[Ori||Seq]" + QString::number(updateStatusMap->value(*myOrigin).toInt() + 1)
            + "[-ADDRIS>]" + QHostAddress("127.0.0.1").toString()
            + "[-PORTIS>]" + QString::number(destPort)
            + "[-METYPE>]" + "SM";
        fwdMessage(fwdInfo);
        qDebug() << "antiEntropy triggered!!!";
    }
}


void ChatDialog::gotReturnPressed()
{

    // Initially, just echo the string locally.
    // Insert some networking code here...
   
    // Exercise 3. Tian added networking code here
    // Build the gossip chat message 
    QVariantMap *rumorMessage = new QVariantMap();
    rumorMessage->insert("ChatText", textedit->toPlainText());
    rumorMessage->insert("Origin", QHostInfo::localHostName() + QString::number(randomOriginID));
    rumorMessage->insert("SeqNo", SeqNo);
    
    // Pick up a random port in a line topo
    quint16 destPort = sockRecv->getMyPort();
    if (destPort == sockRecv->getMyPortMin()) destPort = destPort + 1;
    else if (destPort == sockRecv->getMyPortMax()) destPort = destPort - 1;
    else destPort = qrand()%2 == 0?destPort-1:destPort+1;

    // Update recvMessageMap
    recvMessageMap->insert(rumorMessage->value("Origin").toString() + "[Ori||Seq]" + rumorMessage->value("SeqNo").toString(), *rumorMessage);

    // Update updateStatusMap
    updateStatusMap->insert(rumorMessage->value("Origin").toString(), rumorMessage->value("SeqNo").toInt());
    
    // Send the message
    QString fwdInfo = rumorMessage->value("Origin").toString() + "[Ori||Seq]" + rumorMessage->value("SeqNo").toString() 
        + "[-ADDRIS>]" + QHostAddress("127.0.0.1").toString()
        + "[-PORTIS>]" + QString::number(destPort)
        + "[-METYPE>]" + "GM";
    fwdMessage(fwdInfo);

    textview->append(textedit->toPlainText());
    textedit->clear();
    SeqNo++;
}

void ChatDialog::fwdMessage(QString fwdInfo)
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


void ChatDialog::gotRecvMessage()
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
                        // Pick up a random port in a line topo
                        quint16 destPort = sockRecv->getMyPort();
                        if (destPort == sockRecv->getMyPortMin()) destPort = destPort + 1;
                        else if (destPort == sockRecv->getMyPortMax()) destPort = destPort - 1;
                        else destPort = qrand()%2 == 0?destPort-1:destPort+1;

                        // forward
                        QString fwdInfo = recvOrigin + "[Ori||Seq]" + updateStatusMap->value(recvOrigin).toString()
                                + "[-ADDRIS>]" + QHostAddress("127.0.0.1").toString()
                                + "[-PORTIS>]" + QString::number(destPort)
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
            //qDebug() << "senderPor: " << senderPort;

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
                    textview->append(recvMessage.value("ChatText").toString());
                    
                    // Pick up a random port in a line topo
                    quint16 destPort = sockRecv->getMyPort();
                    if (destPort == sockRecv->getMyPortMin()) destPort = destPort + 1;
                    else if (destPort == sockRecv->getMyPortMax()) destPort = destPort - 1;
                    else destPort = qrand()%2 == 0?destPort-1:destPort+1;

                    // forward
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo)
                            + "[-ADDRIS>]" + QHostAddress("127.0.0.1").toString()
                            + "[-PORTIS>]" + QString::number(destPort)
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

                    textview->append(recvMessage.value("ChatText").toString());
                
                    // Pick up a random port in a line topo
                    quint16 destPort = sockRecv->getMyPort();
                    if (destPort == sockRecv->getMyPortMin()) destPort = destPort + 1;
                    else if (destPort == sockRecv->getMyPortMax()) destPort = destPort - 1;
                    else destPort = qrand()%2 == 0?destPort-1:destPort+1;

                    // forward
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo)
                            + "[-ADDRIS>]" + QHostAddress("127.0.0.1").toString()
                            + "[-PORTIS>]" + QString::number(destPort)
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
bool ChatDialog::eventFilter(QObject *obj, QEvent *event)
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

int main(int argc, char **argv)
{
	// Initialize Qt toolkit
	QApplication app(argc,argv);

	// Create an initial chat dialog window
	ChatDialog dialog;
	dialog.show();

	// Create a UDP network socket
    /* Tian: Deprecated for no use here.
	NetSocket sock;
	if (!sock.bind())
		exit(1);
    */

	// Enter the Qt main loop; everything else is event driven
	return app.exec();
}

