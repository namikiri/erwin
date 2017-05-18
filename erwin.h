#ifndef ERWIN_H
#define ERWIN_H

#include <QObject>
#include <QString>
#include <QProcess>
#include <QCoreApplication>
#include <QStringList>
#include <QDateTime>
#include <QTextStream>
#include <QFile>
#include <QUrl>
#include <QCryptographicHash>
#include <QTcpSocket>
#include <QList>
#include <QRegExp>
#include <QSettings>
#include <QTimer>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

// Do your own research for this thing plz
#include "galaxyhash.h"

struct GalaxyUser {
    QString nick;
    QString clan;
    QString id;
    int position;
};

class Erwin : public QObject
{
    Q_OBJECT
public:
    explicit Erwin(QSettings *settings, QObject *parent = 0);


private:

    QString authToken;
    QString recoveryCode;
    QString userId;
    QString userPasswd;
    QString userNick;
    QString channelId;
    QString planet;
    QString adminId;
    QString companionId;
    QString contactNick;
    QString lastPidor;
    QString lastPrisonedMe;

    uint statsDelay;

    QString lastBanned;
    uint bannedDirectly;
    uint bannedDeferred;
    uint bannedForPrison;
    uint botCalls;
    uint reconnectDelay;
    uint pidorateDelay;
    bool logToFile;
    bool autobanEnabled;
    bool autokickEnabled;
    bool prisonbanEnabled;
    bool reconnectOnPrison;
    bool reconnectOnLoss;
    bool silentMode;
    bool pidorate;

    QString chatServerAddress;
    uint chatServerPort;
    QString frontendAddress;
    QString socketBuffer;
    QTcpSocket *socket;

    bool isReconnecting;

    QNetworkAccessManager *nmBlacklister;
    QNetworkAccessManager *nmPrivateReader;
    QNetworkAccessManager *nmChannelResolver;

    QList <QRegExp *> nickPatterns;
    QList <QRegExp *> clanPatterns;
    QList <GalaxyUser> users;
    QStringList recoveries;
    QStringList pidoringPhrases;

    QSettings *settings;

    QTimer *statsTimer;

    QFile *systemLogFile;
    QFile *privateLogFile;

    void processGalaxyCommand (QString message);
    void processAdminCommand (QString message, QString senderNick = QString());
    void processUserList (QString userlist);

    void removeById (QString id);
    void updatePosition (QString id, int position);
    QString nick2id(QString nick);
    QString id2nick (QString id);
    QString id2clan (QString id);


    void sendMessage (QString message);
    void say (QString message);
    void joinChannel (QString channelName);
    void log (QString message, QString module = "core");
    void loadSettings();
    void reconnect(bool useNewCode);

    bool contains(QList<QRegExp *> *list, QString needle);

    void banRemotely (QString id);
    void getPrivateMessages();
    void getChannelId (QString channelName);

signals:

public slots:

private slots:
    void socketMessageReceived();
    void socketClosed();
    void socketConnected();
    void socketError (QAbstractSocket::SocketError error);

    void nmBlacklistingFinished(QNetworkReply *reply);
    void nmPrivateRead (QNetworkReply *reply);
    void nmChannelResolved (QNetworkReply *reply);

    void sendStatistics();
    void restartDaemon();
    void connectAgain();

    void sayPidor();
};

#endif // ERWIN_H
