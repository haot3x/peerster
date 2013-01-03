#include "privatemessage.hh"
#include "peersterdialog.hh"
#include "netsocket.hh"

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
    delete bytearrayToSend;
}


