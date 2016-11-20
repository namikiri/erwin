#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTextCodec>
#include <QString>
#include <QTranslator>
#include <QSettings>
#include "erwin.h"


int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    QCommandLineParser parser;
    parser.addOption(QCommandLineOption(QStringList() << "c" << "config", "Configuration file", "config"));
    parser.process(a);

    QString configFile = parser.value("config").isEmpty()
                          ? a.applicationDirPath()+"/erwin.conf"
                          : parser.value("config");

    QSettings s(configFile, QSettings::IniFormat);
    s.setIniCodec("UTF-8");

    QTranslator ya3baal;
    QString locale = (s.value("lang", QString()).toString().isEmpty()
                      ? QLocale::system().name()
                      : s.value("lang", QString()).toString());

    ya3baal.load("erwin_" + locale);
    a.installTranslator(&ya3baal);

    Erwin e(&s);
    return a.exec();
}
