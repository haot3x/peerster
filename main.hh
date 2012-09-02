#ifndef PEERSTER_MAIN_HH
#define PEERSTER_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QUdpSocket>
#include <QPushButton>// Tian added for click to send message
#include <QKeySequence>// Tian added for shortcuts
#include <QShortcut>// Tian added for shortcuts
#include <QString>
#include <QDataStream>
#include <QVariant>
#include <QHostAddress>

class NetSocket : public QUdpSocket
{
	Q_OBJECT

public:
	NetSocket();
    int getMyPortMin() { return myPortMin;}
    int getMyPortMax() { return myPortMax;}

	// Bind this socket to a Peerster-specific default port.
	bool bind();

private:
	int myPortMin, myPortMax;
};


class ChatDialog : public QDialog
{
	Q_OBJECT

public:
	ChatDialog();

public slots:
    /* Tian: deprecated for return would create a new line. use gotClicked() instead.
	void gotReturnPressed();
    */
    void gotClicked();
    void gotRecvMessage();

private:
	QTextEdit *textview;
	QTextEdit *textline;
    QPushButton *pushbutton;
    QShortcut *shortcut;
    NetSocket *sockRecv;

};


#endif // PEERSTER_MAIN_HH
