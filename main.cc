
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
    int destPort = sockRecv->getMyPort();
    if (destPort == sockRecv->getMyPortMin()) destPort = destPort + 1;
    else if (destPort == sockRecv->getMyPortMax()) destPort = destPort - 1;
    else destPort = qrand()%2 == 0?destPort-1:destPort+1;

    // Update recvMessageMap
    recvMessageMap->insert(rumorMessage->value("Origin").toString() + "[Ori||Seq]" + rumorMessage->value("SeqNo").toString(), *rumorMessage);

    // Update updateStatusMap
    updateStatusMap->insert(rumorMessage->value("Origin").toString(), rumorMessage->value("SeqNo").toInt());
    
    // DEBUG set my own port to DEBUG
    // TODO revise it 
    QString fwdInfo = rumorMessage->value("Origin").toString() + "[Ori||Seq]" + rumorMessage->value("SeqNo").toString() 
        + "[-ADDRIS>]" + QHostAddress("172.0.0.1").toString()
        + "[-PORTIS>]" + QString::number(sockRecv->getMyPort())
        + "[-METYPE>]" + "GM";
    fwdMessage(fwdInfo);

    textedit->clear();
    SeqNo++;
}

void ChatDialog::fwdMessage(QString fwdInfo)
{
    // Parse fwdInfo
    QString OriSeq = fwdInfo.left(fwdInfo.lastIndexOf("[-ADDRIS>]"));
    //qDebug() << "OriSeq: " << OriSeq;
    QHostAddress host(fwdInfo.mid(fwdInfo.lastIndexOf("[-ADDRIS>]") + 10, fwdInfo.lastIndexOf("[-PORTIS>]") - fwdInfo.lastIndexOf("[-ADDRIS>]")-10)); 
    //qDebug() << "host:" << host;
    quint16 port = fwdInfo.mid(fwdInfo.lastIndexOf("[-PORTIS>]") + 10, fwdInfo.lastIndexOf("[-METYPE>]") - fwdInfo.lastIndexOf("[-PORTIS>]")-10).toInt(); 
    //qDebug() << "port:" << port;
    QString mType = fwdInfo.right(2);
    QVariantMap *message;
    if (mType == "GM")
    {
        message = new QVariantMap(recvMessageMap->value(OriSeq).toMap());
    }
    else if (mType == "SM")
    {
        QVariantMap *wantValue = new QVariantMap();
        QString OriginSM = fwdInfo.left(fwdInfo.lastIndexOf("[Ori||Seq]"));
        QString SeqNoSM = fwdInfo.mid(fwdInfo.lastIndexOf(fwdInfo.lastIndexOf("[Ori||Seq]")+10, fwdInfo.lastIndexOf("[-ADDRIS>]") - fwdInfo.lastIndexOf("[Ori||Seq]") - 10));
        wantValue->insert(OriginSM, SeqNoSM);
        message = new QVariantMap();
        message->insert("want", *wantValue);
    }

    //qDebug() << OriSeq;
    if (!ackHist->contains(OriSeq))
    {
        // Serialize 
        QByteArray *bytearrayToSend = new QByteArray();
        QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
        bytearrayStreamOut << (*message);

        // Send the datagram 
        NetSocket sockSend;
        qint64 int64Status = sockSend.writeDatagram(*bytearrayToSend, host, port);
        if (int64Status == -1) qDebug() << "errors in writeDatagram"; 
        qDebug() << mType << " message has been sent to " << host << ":" << port << "| size: " << int64Status;
        qDebug() << "----------------------------";

        // Set timer to receive the remote peer's acknowledgment
        QSignalMapper *mapper = new QSignalMapper(this);
        timerForAck= new QTimer(this);
        timerForAck->setSingleShot(true);
        connect(timerForAck, SIGNAL(timeout()), mapper, SLOT(map()));
        connect(mapper, SIGNAL(mapped(QString)), this, SLOT(fwdMessage(QString)));
        mapper->setMapping(timerForAck, fwdInfo);
        timerForAck->start(1000);

    }
    else 
        ackHist->remove(ackHist->lastIndexOf(OriSeq));
}

void ChatDialog::gotRecvMessage()
{
    qDebug() << "Message recv!!";
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

        // de-serialize
        QVariantMap recvMessage;
        QDataStream bytearrayStreamIn(bytearrayRecv, QIODevice::ReadOnly);
        bytearrayStreamIn >> recvMessage;
        qDebug() << "recv the map: " << recvMessage;

        // send ACK
        QString recvOrigin = recvMessage.value("Origin").toString();
        int recvSeqNo = recvMessage.value("SeqNo").toInt();
        qDebug() << "recvOrigin: " << recvOrigin;
        qDebug() << "recvSeqNo: " << recvSeqNo;
        QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo) 
                            + "[-ADDRIS>]" + senderAddr.toString() // TODO addr
                            + "[-PORTIS>]" + senderPort 
                            + "[-METYPE>]" + "SM"; // TODO SM or GM herrecvSeqNoe
        fwdMessage(fwdInfo);
                   
        // treat acknowledgment (namely, status message) and gossip chat message differently.
        if (recvMessage.contains("Want")) // If it is an status message (SM), namely acknowledgment
        {
            ackHist->append(recvMessage.value("Origin").toString() + "[Ori||Seq]" + recvMessage.value("SeqNo").toString());
            if (updateStatusMap->contains(recvOrigin))
            {
                int mySeqNo = updateStatusMap->value(recvOrigin).toInt();
                if (mySeqNo == recvSeqNo) 
                {
                    if (qrand()%2 == 0)
                    {
                        // Pick up a random port in a line topo 
                        int destPort = sockRecv->getMyPort();
                        if (destPort == sockRecv->getMyPortMin()) destPort = destPort + 1;
                        else if (destPort == sockRecv->getMyPortMax()) destPort = destPort - 1;
                        else destPort = qrand()%2 == 0?destPort-1:destPort+1;

                        // Send status message
                        QString fwdInfo = recvOrigin + "[Ori||Seq]" + recvMessage.value("SeqNo").toString() 
                            + "[-ADDRIS>]" + QHostAddress("172.0.0.1").toString() // TODO addr
                            + "[-PORTIS>]" + destPort 
                            + "[-METYPE>]" + "SM"; // TODO SM or GM herrecvSeqNoe
                        fwdMessage(fwdInfo);
                    }
                    else ; // TODO cease. The problem is that sender would resend if there is no ack
                }
                else if (mySeqNo > recvSeqNo)
                {    
                    QString fwdInfo = recvOrigin  + "[Ori||Seq]" + QString::number(recvSeqNo) // TODO mySeqNo or recvSeqNo?
                            + "[-ADDRIS>]" + senderAddr.toString() 
                            + "[-PORTIS>]" + senderPort 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);
                    for (int i = recvSeqNo + 1; i <= mySeqNo; i++)
                    {
                       QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(i)
                          + "[-ADDRIS>]" + senderAddr.toString()  
                          + "[-PORTIS>]" + senderPort 
                          + "[-METYPE>]" + "GM"; 
                       fwdMessage(fwdInfo);
                    }
                }
                else if (mySeqNo < recvSeqNo)
                {
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(mySeqNo) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + senderPort 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);
                }
            } 
            else // not contain orgin 
            {
                QString fwdInfo = recvMessage.value("Origin").toString() + "[Ori||Seq]" + QString::number(0) // TODO 0 or -1
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + senderPort 
                            + "[-METYPE>]" + "SM"; 
                fwdMessage(fwdInfo);
            }
        }
        else // It is a Gossip Message (GM)
        {
            if (updateStatusMap->contains(recvOrigin))
            {
                int mySeqNo = updateStatusMap->value(recvOrigin).toInt();
                if (mySeqNo == recvSeqNo - 1 )
                {
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + senderPort 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);

                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));

                    textview->append(recvMessage.value("ChatText").toString());
                }
                else 
                {
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(mySeqNo) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + senderPort 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);
                }
                
            }
            else // not contain origin
            {
                if ( 1 == recvSeqNo )
                {
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + senderPort 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);

                    recvMessageMap->insert(recvOrigin + "[Ori||Seq]" + QString::number(recvSeqNo), recvMessage);
                    updateStatusMap->insert(recvOrigin, QString::number(recvSeqNo));

                    textview->append(recvMessage.value("ChatText").toString());
                }
                else
                {
                    QString fwdInfo = recvOrigin + "[Ori||Seq]" + QString::number(0) 
                            + "[-ADDRIS>]" + senderAddr.toString()  
                            + "[-PORTIS>]" + senderPort 
                            + "[-METYPE>]" + "SM"; 
                    fwdMessage(fwdInfo);
                }
            }
        }
    }
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

