#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QTextStream>

int main(int argc, char *argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("tg_cli");

    QCommandLineParser parser;
    parser.setApplicationDescription("Telegram command-line client skeleton");
    parser.addHelpOption();
    parser.process(application);

    QTextStream(stdout) << "tg_cli skeleton\nUse --help for options.\n";

    return 0;
}