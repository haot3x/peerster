#ifndef PRIVATEMESSAGE_HH
#define PRIVATEMESSAGE_HH

#include "main.hh"

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

#endif 
