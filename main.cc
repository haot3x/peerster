#include <unistd.h>

#include <QApplication>
#include <QDebug>

#include "main.hh"
#include "netsocket.hh"
#include "peersterdialog.hh"
#include "privatemessage.hh"
#include "filemetadata.hh"


// ---------------------------------------------------------------------
// main program entry
int main(int argc, char **argv) {
    // support crypto
    QCA::Initializer qcainit;
	// Initialize Qt toolkit
	QApplication app(argc,argv);

    PeersterDialog dialog;

	// Enter the Qt main loop; everything else is event driven
	return dialog.exec();
}
