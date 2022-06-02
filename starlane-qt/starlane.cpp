#include <iostream>
#include <QtWidgets/QApplication>

#include <starlane-core.h>


int main(int argc, char **argv) {
	QApplication app(argc, argv);
	Starlane::InitBackend();
	return app.exec();
}
