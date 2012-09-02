
#include <unistd.h>

#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>

#include "main.hh"

ChatDialog::ChatDialog()
{
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
	textline = new QTextEdit(this);

    pushbutton = new QPushButton("Send Message (Ctrl+Return)", this);
    shortcut = new QShortcut(QKeySequence(tr("Ctrl+Return")), this);
    
	// Lay out the widgets to appear in the main window.
	// For Qt widget and layout concepts see:
	// http://doc.qt.nokia.com/4.7-snapshot/widgets-and-layouts.html
	QVBoxLayout *layout = new QVBoxLayout();
	layout->addWidget(textview);
	layout->addWidget(textline);
    layout->addWidget(pushbutton);
    // Exercise 1. Tian added it to set focus on the textline without having to click in it first.
    textline->setFocus();

	setLayout(layout);

	// Register a callback on the textline's returnPressed signal
	// so that we can send the message entered by the user.
    /* Tian: deprecated for return would create a new line
	connect(textline, SIGNAL(returnPressed()),
		this, SLOT(gotReturnPressed()));
    */
    //NetSocket sock;
    sockRecv = new NetSocket();
	if (!sockRecv->bind())
		exit(1);
    connect(sockRecv, SIGNAL(readyRead()),
        this, SLOT(gotRecvMessage()));
    connect(pushbutton, SIGNAL(clicked()),
        this, SLOT(gotClicked()));
    connect(shortcut, SIGNAL(activated()),
        this, SLOT(gotClicked()));
}

void ChatDialog::gotClicked()
{
	// Initially, just echo the string locally.
	// Insert some networking code here...
    
    // Exercise 3. Tian added networking code here
    // Build map
    QMap<QString, QString> *mapStrStr = new QMap<QString, QString>();
    mapStrStr->insert(tr("ChatText"), textline->toPlainText());

    // Serialize map to an instance of QByteArray
    QByteArray *bytearrayToSend = new QByteArray();
    QDataStream bytearrayStreamOut(bytearrayToSend, QIODevice::WriteOnly);
    bytearrayStreamOut << (*mapStrStr);

    // send the datagram to 4 ports via local host
    NetSocket sockSend;
    for (int destPort =  sockSend.getMyPortMin(); destPort <= sockSend.getMyPortMax(); destPort++)
    {
        qint64 int64Status = sockSend.writeDatagram(*bytearrayToSend, QHostAddress::LocalHost, destPort);
        if (int64Status == -1) exit(1); 
        qDebug() << "sent to " << (quint16)(destPort) << " SIZE:" << int64Status;
    }

    
	// qDebug() << "FIX: send message to other peers: " << textline->toPlainText();
	textview->append(textline->toPlainText());

	// Clear the textline to get ready for the next input message.
	textline->clear();
}

void ChatDialog::gotRecvMessage()
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

        // de-serialize
        QMap<QString, QString> mapStrStrRecv; 
        QDataStream bytearrayStreamIn(bytearrayRecv, QIODevice::ReadOnly);
        bytearrayStreamIn >> mapStrStrRecv;
        qDebug() << "recv the map: " << mapStrStrRecv;

        // add string to the chat-log in the ChatDialog
        QString stringRecv(mapStrStrRecv.take("ChatText"));
        textview->append(stringRecv);
    }
}


/* Tian: deprecated for return would create a new line, use gotClicked instead
void ChatDialog::gotReturnPressed()
{
	// Initially, just echo the string locally.
	// Insert some networking code here...
	qDebug() << "FIX: send message to other peers: " << textline->toPlainText();
	textview->append(textline->toPlainText());

	// Clear the textline to get ready for the next input message.
	textline->clear();
}
*/

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
			qDebug() << "bound to UDP port " << p;
			return true;
		}
	}

	qDebug() << "Oops, no ports in my default range " << myPortMin
		<< "-" << myPortMax << " available";
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

