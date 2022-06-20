#include <iostream>
#include <QtWidgets/QApplication>

#include <starlane-core.h>

namespace SLFrontend {
void FatalError(const char *msg) {}
}


int main(int argc, char **argv) {
	QApplication app(argc, argv);
	Starlane::FECapabilities settings;
	Starlane::InitBackend(settings);
	return app.exec();
}
