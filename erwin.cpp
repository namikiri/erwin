#include "erwin.h"

Erwin::Erwin(QSettings *s, QObject *parent) : QObject(parent)
{
    settings = s;

    logToFile = false;
    log (" -= Welcome to Erwin by IDENT Software =-");

    if (settings == NULL)
    {
        log ("Settings loading failed.");
        exit(-1);
    }

    log ("Loading settings from "+settings->fileName());
    settings->setIniCodec("UTF-8");
    loadSettings();

    log ("Setting up main socket...");
    socket = new QTcpSocket(this);

    connect(socket, SIGNAL(readyRead()), this, SLOT(socketMessageReceived()));
    connect(socket, SIGNAL(connected()), this, SLOT(socketConnected()));
    connect(socket, SIGNAL(disconnected()), this, SLOT(socketClosed()));
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(socketError(QAbstractSocket::SocketError)));

    log ("Setting up blacklisting NAM...");
    nmBlacklister = new QNetworkAccessManager(this);
    connect (nmBlacklister, SIGNAL(finished(QNetworkReply*)), this, SLOT(nmBlacklistingFinished(QNetworkReply*)));

    log ("Setting up PM reading NAM...");
    nmPrivateReader = new QNetworkAccessManager(this);
    connect (nmPrivateReader, SIGNAL(finished(QNetworkReply*)), this, SLOT(nmPrivateRead(QNetworkReply*)));

    log ("Setting up channel resolving NAM...");
    nmChannelResolver = new QNetworkAccessManager(this);
    connect (nmChannelResolver, SIGNAL(finished(QNetworkReply*)), this, SLOT(nmChannelResolved(QNetworkReply*)));

    log ("Initializing counters...");
    bannedDeferred = 0; bannedDirectly = 0; bannedForPrison = 0;
    botCalls = 0; autobanEnabled = false; autokickEnabled = false;
    prisonbanEnabled = true; isReconnecting = false;
    lastPrisonedMe = QString();

    log ("Setting up notifier...");
    if (statsDelay > 0)
    {
        statsTimer = new QTimer(this);
        connect (statsTimer, SIGNAL(timeout()), this, SLOT(sendStatistics()));
        statsTimer->setInterval(statsDelay*1000);
        statsTimer->start();
    }

    log ("Using the first recovery code.");
    recoveryCode = recoveries.at(0);
    recoveries.removeAt(0); // don't use this code anymore

    log ("Let's go!");
    socket->connectToHost(chatServerAddress, chatServerPort);
}


void Erwin::processGalaxyCommand(QString message)
{

    if (isReconnecting) // it receives junk commands because of async socket
        return;

    QStringList commands = message.split(' ', QString::SkipEmptyParts);

    if (commands.count() == 0)
        return;

    if (commands[0] == "JOIN")
    {
        if (contains(&nickPatterns, commands[2]))
        {
            sendMessage("BAN "+commands[3]);
            lastBanned = commands[3];
            log ("Express Ban: banned UID "+commands[3], "frwl");
            bannedDirectly++;
        }

        if (commands.count() < 4)
            return;


        if (autokickEnabled)
        {
            sendMessage("KICK "+commands[3]);
            return;
        }
            else
        if (autobanEnabled)
        {
            sendMessage("BAN "+commands[3]);
            return;
        }

        if (contains(&clanPatterns, commands[1]))
        {
            say (tr("* %1 (UID %3) has been banned because he/she is a member of clan %2. If this is wrong, please send a message to %4").arg(commands[2]).arg(commands[1]).arg(commands[3]).arg(contactNick));
            sendMessage("BAN "+commands[3]);
            log (QString("Banned by clan %1: Nick %2, UID %3").arg(commands[1]).arg(commands[2])
                                                              .arg(commands[3]), "frwl");
            return;
        }

        if (id2nick(commands[3]).isEmpty())
        {
            GalaxyUser u;
            u.clan = commands[1];
            u.nick = commands[2];
            u.id   = commands[3];

            if (commands.indexOf("@") > -1)
            {
                int cpos = 0;
                do
                {
                    cpos++;
                } while (commands[cpos] != "@");

                u.position = commands[cpos+4].toInt();
            }

            log (QString("Added a new user: Clan %1, Nick %2, ID %3")
                        .arg(u.clan)
                        .arg(u.nick)
                        .arg(u.id), "user");

            users.append(u);
        }

        if (commands[3] == companionId)
        {
            say(tr("Hallo meine lieben %1 :3").arg(commands[2]));
            sendMessage(QString("EMO 1017 :%1 one love :3").arg(commands[2]));
        }

        return;
    }

    log ("Recv: "+message, "sock"); // shortened to make logs more readable

    if (commands[0] == "HAAAPSI")
    {
        // in galaxyhash
        authToken = GalaxyHash::getHash(commands[1]);

        sendMessage ("RECOVER "+recoveryCode);

        return;
    }

    if (commands[0] == "REGISTER")
    {
        userId = commands[1];
        userPasswd = commands[2];
        userNick = commands[3];

        sendMessage(QString("USER %1 %2 %3 %4")
                    .arg(userId)
                    .arg(userPasswd)
                    .arg(userNick)
                    .arg(authToken));
        return;
    }


    if (commands[0] == "999")
    {
        sendMessage("PHONE 1337 1337 0 2 :Erwin/0.2");
        joinChannel(planet);
        // sendMessage("PRIVMSG 1 -1 :~ Erwin: Interactive Bot Daemon * by IDENT Software ~");
        return;
    }

    if (commands[0] == "401")
    {
        log ("Oh shish, could not ban! Trying deferred blacklisting for "+lastBanned+"...", "frwl");
        banRemotely(lastBanned);
        return;
    }


    if (commands[0] == "475")
    {
        log ("This character is prisoned, OK, switching to another...");
        reconnect(true);
        return;
    }

    if (commands[0] == "PRIVMSG")
    {
        if (message.indexOf(userNick) > -1)
            botCalls++;

        if (commands[1] == adminId)
        {
            processAdminCommand(message.mid(message.indexOf(":")+1), id2nick(commands[1]));
        }

        /// REMOVE ME PLZ

        if (message.indexOf("сам пидор") > -1)
        {
            lastPidor = id2nick(commands[1]);
        }

        return;
    }

    if (commands[0] == "353")
    {
        processUserList(message.mid(message.indexOf(":")+1));
        return;
    }


    if (commands[0] == "SLEEP" || commands[0] == "PART")
    {
        removeById(commands[1]);
        return;
    }

    if (commands.count() > 2 && (commands[1] == "KICK" || commands[1] == "BAN"))
    {
        removeById(commands[2]);

        if (prisonbanEnabled && message.indexOf("Тюрьма") > -1)
        {
            QString nickToBan = commands[0].mid(1),
                    idToBan   = nick2id(nickToBan);
            sendMessage("BAN "+idToBan);
            say(tr("* Banned %1 (UID %2) for prisoning. Please report the issue to %3 if this was wrong.")
                        .arg(nickToBan).arg(idToBan).arg(contactNick));
            log (QString("Banned %1 (UID %2) for prisoning.").arg(nickToBan).arg(idToBan), "frwl");

            bannedForPrison++;
        }
        return;
    }

    if (commands.count() > 2 && commands[1] == "PRISON")
    {
        QString nickToBan = commands[0].mid(1),
                idToBan   = nick2id(nickToBan);
        log (QString("!!! OH SHIT! %1 (UID %2) prisoned me! HELP!").arg(nickToBan).arg(idToBan));
        log ("I will ban them remotely and close the socket.", "frwl");
        banRemotely(idToBan);

        if (reconnectOnPrison)
        {
            lastPrisonedMe = idToBan;
            reconnect(true);
        }
        else
            socket->close();

        return;
    }

    if (commands[0] == "REMOVE")
    {
        updatePosition(commands[1], commands[2].toInt());
        return;
    }

    if (commands[0] == "PING")
    {
        sendMessage("PONG");
        return;
    }
}

void Erwin::processAdminCommand(QString message, QString senderNick)
{
    if (message.startsWith("/"))
    {
        QString command = message.mid(1, message.indexOf(" ")).trimmed();
        log ("* Admin command detected: "+command, "actl");

        if (command == "exec")
        {
            message = message.mid(message.indexOf(" ")).trimmed();
            sendMessage(message);
            return;
        }

        if (command == "nus")
        {
            say(QString("%1, sus").arg(senderNick));
            return;
        }

        if (command == "stats")
        {
            say(tr("%5, banned %1 bots (%2 directly, %3 with deferred blacklisting). "
                                "%7 users banned for prisoning, %4 bot calls detected, %6 users on '%8' (ChannelID %9) are currently online.")
                        .arg(bannedDeferred+bannedDirectly)
                        .arg(bannedDirectly).arg(bannedDeferred).arg(botCalls)
                        .arg(senderNick).arg(users.count()).arg(bannedForPrison)
                        .arg(planet).arg(channelId));

            return;
        }

        if (command == "shconf")
        {
            // so many args wow sorry lol
            say(tr("%2, daemon configuration: %1")
                .arg(
                    QString(
                                "autokick %1; autoban %2; prisonban %3; admin_id %4; "
                                "companion_id %7; reconnect_on_prison %8; stats_delay %5s; contact_nick '%9'; planet '%6'")
                        .arg(autokickEnabled ? "on" : "off")
                        .arg(autobanEnabled ? "on" : "off")
                        .arg(prisonbanEnabled ? "on" : "off")
                        .arg(adminId).arg(statsDelay)
                        .arg(planet).arg(companionId)
                        .arg(reconnectOnPrison ? "on" : "off")
                        .arg(contactNick))
                .arg(senderNick));

            return;
        }

        if (command == "echo")
        {
            message = message.mid(message.indexOf(" ")).trimmed();
            say(message);
            return;
        }

        if (command == "kick")
        {
            QString nick = message.mid(message.indexOf(" ")).trimmed();
            nick = nick.mid(0, nick.indexOf(' '));

            if (nick == "*")
            {
                for (int i = 0; i < users.count(); i++)
                {
                    sendMessage("KICK "+users.at(i).id);
                }

                 say(tr("%1, tried to kick ALL from the planet.").arg(senderNick));

                 return;
            }

            QString id = nick2id(nick);
            if (id.isEmpty())
                say(tr("%1, no such nick on the planet.").arg(senderNick));
            else
            {
                sendMessage("KICK "+id);
                say(tr("%1, successfully kicked %2 (UID %3)")
                            .arg(senderNick).arg(nick).arg(id));
            }

            return;
        }

        if (command == "ban")
        {
            QString nick = message.mid(message.indexOf(" ")).trimmed();
            nick = nick.mid(0, nick.indexOf(' '));

            if (nick == "*")
            {
                for (int i = 0; i < users.count(); i++)
                {
                    sendMessage("BAN "+users.at(i).id);
                }

                 say(tr("%1, tried to ban ALL from the planet.").arg(senderNick));

                 return;
            }

            QString id = nick2id(nick);
            if (id.isEmpty())
                say(tr("%1, no such nick on the planet.").arg(senderNick));
            else
            {
                sendMessage("BAN "+id);
                say(tr("%1, successfully banned %2 (UID %3)")
                            .arg(senderNick).arg(nick).arg(id));
            }

            return;
        }

        if (command == "idof")
        {
            QString nick = message.mid(message.indexOf(" ")).trimmed();
            nick = nick.mid(0, nick.indexOf(' '));

            QString id = nick2id(nick);
            if (id.isEmpty())
                say(tr("%1, no such nick on the planet.").arg(senderNick));
            else
            {
                say(tr("%1, UID of %2 is %3")
                            .arg(senderNick).arg(nick).arg(id));
            }

            return;
        }

        if (command == "join")
        {
            QString chan = message.mid(message.indexOf(" ")).trimmed();
            chan = chan.mid(0, chan.indexOf(' '));

            say(tr("%1, joining the channel %2...").arg(senderNick).arg(chan));
            joinChannel(chan);

            return;
        }

        if (command == "setadm")
        {
            QString nick = message.mid(message.indexOf(" ")).trimmed();
            nick = nick.mid(0, nick.indexOf(' '));

            QString id = nick2id(nick);
            if (id.isEmpty())
                say(tr("%1, no such nick on the planet.").arg(senderNick));
            else
            {
                say(tr("%1, granted admin rights to %2 (UID %3)")
                            .arg(senderNick).arg(nick).arg(id));
                adminId = id;
            }

            return;
        }

        if (command == "setcompanion")
        {
            QString nick = message.mid(message.indexOf(" ")).trimmed();
            nick = nick.mid(0, nick.indexOf(' '));

            QString id = nick2id(nick);
            if (id.isEmpty())
                say(tr("%1, no such nick on the planet.").arg(senderNick));
            else
            {
                say(tr("%1, my companion is %2 (UID %3) now.")
                            .arg(senderNick).arg(nick).arg(id));
                companionId = id;
            }

            return;
        }

        if (command == "help")
        {
            say(tr("%1, available commands are: %2")
                        .arg(senderNick).arg("exec nus silence stats shconf echo list join kick ban autokick autoban prisonban idof setadm swcode daemon help"));

            return;
        }

        if (command == "swcode")
        {
            if (recoveries.count() > 0)
            {
                say(tr("%1, switching to the next recovery code...").arg(senderNick));
                reconnect(true);
            }
            else
                say(tr("%1, no more recovery codes available!").arg(senderNick));

            return;
        }

        if (command == "daemon")
        {
            QString action = message.mid(message.indexOf(" ")).trimmed();
            action = action.mid(0, action.indexOf(' '));

            if (action == "stop")
            {
                say(tr("%1, stopping the daemon. Good luck :3").arg(senderNick));
                sendMessage("QUIT :ds");
                socket->close();
                qApp->quit();
                return;
            }

            if (action == "restart")
            {
                say(tr("%1, daemon will disconnect and restart after 10 seconds.").arg(senderNick));
                sendMessage("QUIT :ds");
                socket->close();

                QTimer::singleShot(10500, this, SLOT(restartDaemon()));
                return;
            }

            if (action == "reconnect")
            {
                say(tr("%1, Erwin will disconnect and connect again, please wait...").arg(senderNick));
                reconnect(false);
                return;
            }

            say(tr("%1, usage: /daemon {restart|stop}").arg(senderNick));
        }

        if (command == "autoban")
        {
            QString action = message.mid(message.indexOf(" ")).trimmed();
            action = action.mid(0, action.indexOf(' '));

            if (action == "on")
            {
                if (autokickEnabled)
                {
                    say(tr("%1, disable AUTOKICK first: /autokick off").arg(senderNick));
                }
                else
                {
                    autobanEnabled = true;
                    say(tr("%1, automatic BAN enabled. I will BAN everybody who comes in.").arg(senderNick));
                }
                return;
            }

            if (action == "off")
            {
                autobanEnabled = false;
                say(tr("%1, automatic BAN disabled.").arg(senderNick));
                return;
            }

            say(tr("%1, usage: /autoban {on|off}").arg(senderNick));

            return;
        }

        if (command == "autokick")
        {
            QString action = message.mid(message.indexOf(" ")).trimmed();
            action = action.mid(0, action.indexOf(' '));

            if (action == "on")
            {
                if (autobanEnabled)
                {
                    say(tr("%1, disable AUTOBAN first: /autoban off").arg(senderNick));
                }
                else
                {
                    autokickEnabled = true;
                    say(tr("%1, automatic KICK enabled. I will KICK everybody who comes in.").arg(senderNick));
                }
                return;
            }

            if (action == "off")
            {
                autokickEnabled = false;
                say(tr("%1, automatic KICK disabled.").arg(senderNick));
                return;
            }

            say(tr("%1, usage: /autokick {on|off}").arg(senderNick));
            return;
        }

        if (command == "prisonban")
        {
            QString action = message.mid(message.indexOf(" ")).trimmed();
            action = action.mid(0, action.indexOf(' '));

            if (action == "on")
            {
                prisonbanEnabled = true;
                say(tr("%1, prison ban enabled. I will ban all the motherfuckers that prison others!").arg(senderNick));
                return;
            }

            if (action == "off")
            {
                prisonbanEnabled = false;
                say(tr("%1, prisonban disabled.").arg(senderNick));
                return;
            }

            say(tr("%1, usage: /prisonban {on|off}").arg(senderNick));
            return;
        }

        if (command == "silence")
        {
            QString action = message.mid(message.indexOf(" ")).trimmed();
            action = action.mid(0, action.indexOf(' '));

            if (action == "on")
            {
                say(tr("%1, silent mode enabled, you won't receive any messages from the bot anymore.").arg(senderNick));
                silentMode = true;
                return;
            }

            if (action == "off")
            {
                silentMode = false;
                say(tr("%1, silent mode disabled.").arg(senderNick));
                return;
            }

            say(tr("%1, usage: /prisonban {on|off}").arg(senderNick));
            return;
        }


        if (command == "list")
        {
            QString action = message.mid(message.indexOf(" ")).trimmed();
            action = action.mid(0, action.indexOf(' '));

            if (action == "nicks")
            {
                QString patts;
                for (int i = 0; i < nickPatterns.count(); i++)
                {
                    patts += nickPatterns.at(i)->pattern() + " ";
                }

                say(tr("%1, will ban all the nicks matching patterns: %2").arg(senderNick).arg(patts));

                return;
            }

            if (action == "clans")
            {
                QString patts;
                for (int i = 0; i < clanPatterns.count(); i++)
                {
                    patts += clanPatterns.at(i)->pattern() + " ";
                }
                say(tr("%1, will ban all the clans matching patterns: %2").arg(senderNick).arg(patts));

                return;
            }

            say(tr("%1, usage: /list {nicks|clans}").arg(senderNick));
            return;
        }

        if (command == "pidorate")
        {
            QString action = message.mid(message.indexOf(" ")).trimmed();
            action = action.mid(0, action.indexOf(' '));

            if (action == "on")
            {
                if (pidoringPhrases.count() == 0)
                {
                    say(tr("%1, no pidoring phrases configured, cannot enable.").arg(senderNick));
                    return;
                }

                if (pidorate)
                {
                    say(tr("%1, pidorating is already enabled").arg(senderNick));
                }
                else
                {
                    if (lastPidor.isEmpty())
                        say(tr("%1, no nickname of current pidor").arg(senderNick));
                    else
                    {
                        pidorate = true;
                        sayPidor();
                        say(tr("%1, pidorating enabled successfully").arg(senderNick));
                    }
                }
                return;
            }

            if (action == "off")
            {
                if (pidorate)
                {
                    pidorate = false;
                    say(tr("%1, pidorating disabled successfully").arg(senderNick));
                }
                else
                    say(tr("%1, pidorating is already disabled or could not activate").arg(senderNick));

                return;
            }

            if (action == "delay")
            {
                QStringList cmds = message.split(' ', QString::SkipEmptyParts);
                if (cmds.count() < 3)
                {
                    say(tr("%1, usage: /pidorate delay [delay_time]").arg(senderNick));
                    return;
                }

                uint delay = cmds.at(2).toUInt();
                if (delay > 0)
                {
                    pidorateDelay = delay;
                    say(tr("%1, pidorating delay time is set to %2 ms.").arg(senderNick).arg(delay));
                }
                else
                    say(tr("%1, bad pidorating delay time!").arg(senderNick));

                return;
            }

            say(tr("%1, usage: /pidorate {on|off|delay} [delay_time]").arg(senderNick));

            return;
        }
    }
}

void Erwin::processUserList(QString userlist)
{
    log ("Parsing the channel and building user list", "user");
    while (userlist.length() > 0)
    {
        GalaxyUser u;
        u.clan = userlist.mid(0, userlist.indexOf(' '));
        userlist = userlist.mid(userlist.indexOf(' ')+1);

        u.nick = userlist.mid(0, userlist.indexOf(' '));
        if (u.nick.startsWith('+') || u.nick.startsWith('@'))
            u.nick = u.nick.mid(1);
        userlist = userlist.mid(userlist.indexOf(' ')+1);

        u.id = userlist.mid(0, userlist.indexOf(' '));
        userlist = userlist.mid(userlist.indexOf('@')+2);
        userlist = userlist.mid(userlist.indexOf(' ')+1);
        userlist = userlist.mid(userlist.indexOf(' ')+1);
        userlist = userlist.mid(userlist.indexOf(' ')+1);
        userlist = userlist.mid(userlist.indexOf(' ')+1);

        u.position = userlist.mid(0, userlist.indexOf(' ')).toInt();
        userlist = userlist.mid(userlist.indexOf(' ')+1);

        log (QString("Added a new user: Clan %1, Nick %2, ID %3")
                    .arg(u.clan)
                    .arg(u.nick)
                    .arg(u.id), "user");

        users.append(u);

        if (!lastPrisonedMe.isEmpty() && u.id == lastPrisonedMe)
        {
            log (QString ("Found a dickhead that prisoned me (UID %1), banning this scum!")
                 .arg(u.id), "user");

            sendMessage("BAN "+u.id);
            say(tr("Go fcuk yourself, %1.").arg(u.nick));

            lastPrisonedMe.clear();
        }
    }
}

void Erwin::removeById(QString id)
{
    log ("User removal for UID "+id, "user");
    for (int i = 0; i < users.count(); i++)
    {
        if (users.at(i).id == id)
        {
            log ("User found and removed.", "user");
            users.removeAt(i);
            return;
        }
    }

    log ("Could not remove user.", "user");
}

void Erwin::updatePosition(QString id, int position)
{
    for (int i = 0; i < users.count(); i++)
    {
        if (users.at(i).id == id)
        {
            // users.at(i).position = position;
            return;
        }
    }
}

QString Erwin::nick2id(QString nick)
{
    for (int i = 0; i < users.count(); i++)
    {
        if (users.at(i).nick == nick)
        {
            return users.at(i).id;
        }
    }

    return QString();
}

QString Erwin::id2nick(QString id)
{
    for (int i = 0; i < users.count(); i++)
    {
        if (users.at(i).id == id)
        {
            return users.at(i).nick;
        }
    }

    return QString();
}

QString Erwin::id2clan(QString id)
{
    for (int i = 0; i < users.count(); i++)
    {
        if (users.at(i).id == id)
        {
            return users.at(i).clan;
        }
    }

    return QString();
}

void Erwin::sendMessage(QString message)
{
    if (socket->isOpen())
    {
        log ("Sent: "+message, "sock");
        socket->write(QString(message+"\r\n").toUtf8());
    }
    else
        log ("Socket is not open!", "sock");
}

void Erwin::say(QString message)
{
    if (silentMode)
        log ("SILENT: "+message);
    else
        sendMessage("PRIVMSG 1 -1 :"+message);
}

void Erwin::joinChannel(QString channelName)
{
    log ("Joining channel "+channelName);
    getChannelId(channelName);
    users.clear();
    planet = channelName;
    sendMessage("JOIN "+channelName);
}

void Erwin::log(QString message, QString module)
{
    printf ("<%s> [%s]: %s\n",
            QDateTime::currentDateTime().toString("dd.MM.yy@hh:mm:ss:zzz").toUtf8().data(),
            module.toUtf8().data(),
            message.toUtf8().data());

    if (logToFile)
    {
        QTextStream(systemLogFile) << QString("<%1> [%2]: %3\n")
                                .arg(QDateTime::currentDateTime().toString("dd.MM.yy@hh:mm:ss:zzz"))
                                .arg(module)
                                .arg(message);
    }
}

void Erwin::loadSettings()
{
    settings->beginGroup("filesystem");

    log ("Loading filesystem...", "conf");

    ///
    /// Log file
    ///
    QString logFileName = settings->value("log_system", "stdout").toString();
    if (logFileName.isEmpty() || logFileName == "stdout")
    {
        logToFile = false;
        log ("Will write only to STDOUT");
    }
    else
    {
        systemLogFile = new QFile(logFileName);
        if (!systemLogFile->open(QIODevice::WriteOnly | QIODevice::Append))
        {
            logToFile = false;
            log ("Could not open log file, will write to STDOUT");
        }
        else
        {
            log ("Will also write log to "+logFileName);
            logToFile = true;
        }
    }

    ///
    /// Log file for private messages
    ///
    // Variable reused
    logFileName = settings->value("log_private", "").toString();
    if (logFileName.isEmpty() || logFileName == "stdout")
    {
        log ("No log file for private messages configured, won't process them.", "conf");
    }
    else
    {
        privateLogFile = new QFile(logFileName);
        if (!privateLogFile->open(QIODevice::WriteOnly | QIODevice::Append))
        {
            log ("Could not open private log file. Won't process PMs");
            privateLogFile->deleteLater();
        }
        else
        {
            log ("Will save all PMs to "+logFileName);
        }
    }


    ///
    /// Recovery codes
    ///
    QString recoveriesFile = settings->value("recoveries", "").toString();
    if (recoveriesFile.isEmpty())
    {
        log ("No recovery codes file configured, Erwin will exit.", "conf");
        exit(1);
    }
    else
    {
        QFile rcFile (recoveriesFile);

        log ("Loading recovery codes from "+recoveriesFile, "conf");
        if (rcFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream textStream(&rcFile);

            while (!textStream.atEnd())
                recoveries.append(textStream.readLine());

            rcFile.close();

            if (recoveries.count() == 0)
            {
                log ("No recoveries in "+recoveriesFile, "conf");
                exit(1);
            }
        }
        else
        {
            log ("Could not load recoveries from "+recoveriesFile, "conf");
            exit(1);
        }
    }

    ///
    /// Nicks blacklist
    ///
    QString regexFile = settings->value("bl_nicks", "").toString();
    if (regexFile.isEmpty())
    {
        log ("No nick regexes file configured, Erwin will exit.");
        exit(1);
    }
    else
    {
        QFile rxFile (regexFile);

        log ("Loading nick patterns from "+regexFile, "conf");
        if (rxFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream textStream(&rxFile);
            QString rx;

            while (!textStream.atEnd())
            {
                rx = textStream.readLine();
                QRegExp *regex = new QRegExp(rx);

                if (regex->isValid())
                {
                    log ("Successfully added pattern "+rx);
                    nickPatterns.append(regex);
                }
                else
                    log (QString("Pattern %1 is invalid: %2").arg(rx).arg(regex->errorString()));
            }

            rxFile.close();
        }
        else
        {
            log ("Could not load regexes from "+regexFile);
            exit(1);
        }
    }

    ///
    /// Clans blacklist
    ///
    // We use already initialized variable ;3
    regexFile = settings->value("bl_clans", "").toString();
    if (regexFile.isEmpty())
    {
        log ("No clan regexes file configured, will not ban for clans.", "conf");
    }
    else
    {
        QFile rxFile (regexFile);

        log ("Loading clan patterns from "+regexFile, "conf");
        if (rxFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream textStream(&rxFile);
            QString rx;

            while (!textStream.atEnd())
            {
                rx = textStream.readLine();
                QRegExp *regex = new QRegExp(rx);

                if (regex->isValid())
                {
                    log ("Successfully added pattern "+rx);
                    clanPatterns.append(regex);
                }
                else
                    log (QString("Pattern %1 is invalid: %2").arg(rx).arg(regex->errorString()));
            }

            rxFile.close();
        }
        else
        {
            log ("Could not load clan regexes from "+regexFile, "conf");
        }
    }

    ///
    /// Pidoring phrases
    ///
    QString phrasesFile = settings->value("pidoring_phrases", "").toString();
    if (phrasesFile.isEmpty())
    {
        log ("No pidoring phrases file.", "conf");
    }
    else
    {
        QFile rcFile (phrasesFile);

        log ("Loading pidoring phrases from "+phrasesFile, "conf");
        if (rcFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream textStream(&rcFile);

            while (!textStream.atEnd())
                pidoringPhrases.append(textStream.readLine());

            rcFile.close();
        }
        else
        {
            log ("Could not load pidoring phrases from "+phrasesFile, "conf");
        }
    }

    settings->endGroup();



    settings->beginGroup("daemon");
    log ("Loading daemon settings...", "conf");

    chatServerAddress = settings->value("server_address", "galaxy.mobstudio.ru").toString();
    chatServerPort    = settings->value("server_port", 6667).toUInt();
    frontendAddress   = settings->value("frontend_url", "http://galaxy.mobstudio.ru/services/index.php").toString();

    planet = settings->value("planet", "").toString();
    if (planet.isEmpty())
    {
        log ("No planet configured, Erwin will exit.", "conf");
        exit(1);
    }
    else
        log ("Planet is set to "+planet, "conf");

    adminId = settings->value("admin_id", QString()).toString();
    if (adminId.isEmpty())
    {
        log ("No administrator ID configured. That's potentially insecure, exiting.", "conf");
        exit(1);
    }
    else
        log ("Will wait for commands from "+adminId, "conf");

    statsDelay = settings->value("stats_frequency", 60).toInt();
    prisonbanEnabled = settings->value("prison_ban", true).toBool();
    companionId = settings->value("companion_id", "0").toString();
    contactNick = settings->value("contact_nick", "*NULL*").toString();
    reconnectOnPrison = settings->value("reconnect_on_prison", false).toBool();
    reconnectOnLoss   = settings->value("reconnect_on_loss", false).toBool();
    reconnectDelay    = settings->value("reconnect_delay", 5000).toUInt();
    silentMode        = settings->value("silent_mode", false).toBool();
    pidorate          = settings->value("pidorate", false).toBool();
    pidorateDelay     = settings->value("pidorate_delay", 60000).toUInt();

    if (pidoringPhrases.count() == 0)
    {
        log ("No pidoring phrases, will not enable pidoring!", "conf");
        pidorate = false;
    }

    settings->endGroup();
}

void Erwin::reconnect(bool useNewCode)
{
    if (useNewCode)
    {
        if (recoveries.count() > 0)
        {
            log ("Switching to the next recovery code...");
            recoveryCode = recoveries.at(0);
            recoveries.removeAt(0);
        }
            else
        {
            log ("WARNING! No more recovery codes available! Daemon suspends its work.");
            // qApp->quit(); ?
        }
    }

    log ("Reconnecting: closing the socket.");
    // sendMessage("QUIT :ds"); // no need to send, socket hangs up
    isReconnecting = true;
    if (socket->isOpen())
        socket->close();
    else
        connectAgain();
}

bool Erwin::contains(QList<QRegExp *> *list, QString needle)
{
    for (int i = 0; i < list->count(); i++)
    {
        if (list->at(i)->indexIn(needle) > -1)
            return true;
    }

    return false;
}
void Erwin::banRemotely(QString id)
{
    if (channelId.isEmpty())
    {
        log ("Deferred blacklisting is impossible: no channel ID", "http");
        return;
    }

    QString request = QString(
                frontendAddress+
                "?userID=%1"
                "&password=%2"
                "&usercur=%1"
                "&a=channels_access"
                "&chanid=%3"
                "&type=0"
                "&add=%4"
                "&dbl=4"
                "&flash10=1"
                "&kbv=195"
                "&lngg=ru").arg(userId).arg(userPasswd).arg(channelId).arg(id);

    log ("Blacklisting request: "+request, "http");

    nmBlacklister->get(QNetworkRequest(QUrl(request)));
}

void Erwin::getPrivateMessages()
{
    // get private
}

void Erwin::getChannelId(QString channelName)
{
    if (channelName.isEmpty())
    {
        log ("Cannot get channel ID of НИХУЯ.", "http");
        return;
    }

    QString request = QString(
                frontendAddress+
                "?userID=%1"
                "&password=%2"
                "&usercur=%1"
                "&a=new_search"
                "&sel=1"
                "&dbl=4"
                "&flash10=1"
                "&kbv=195"
                "&lngg=ru").arg(userId).arg(userPasswd);

    QByteArray data;
    data.append("search_field="+QString(QUrl::toPercentEncoding(channelName)));

    log ("Channel ID resolution request: "+request, "http");

    nmChannelResolver->post(QNetworkRequest(QUrl(request)), data);
}

void Erwin::socketMessageReceived()
{
    QString data = QString(socket->readAll());
    socketBuffer += data;

    while (socketBuffer.indexOf("\r\n") > -1)
    {
        processGalaxyCommand(socketBuffer.mid(0, socketBuffer.indexOf("\r\n")));
        socketBuffer = socketBuffer.mid(socketBuffer.indexOf("\r\n")+2);
    }
}

void Erwin::socketClosed()
{
    if (isReconnecting)
    {
        if (reconnectDelay > 0)
        {
            log (QString("Socket will be connected after %1 msec").arg(reconnectDelay));
            QTimer::singleShot(reconnectDelay, this, SLOT(connectAgain()));
        }
        else
            connectAgain();

    }
    else
        log ("Socket died.");
}

void Erwin::socketConnected()
{
    log ("Socket is connected, starting init process!");

    isReconnecting = false;
    sendMessage(QString(":ru IDENT %1 -2 4030 1 2 :GALA").arg(GALAXY_HASH_VERSION)); // in galaxyhash
}

void Erwin::socketError(QAbstractSocket::SocketError error)
{
    log (QString("Oh shit, there's a socket error: %1").arg(error));
    if (reconnectOnLoss)
        reconnect(false);
}

void Erwin::nmBlacklistingFinished(QNetworkReply *reply)
{
    log ("Blacklisting request sent.", "http");
    if (bannedDirectly > 0)
        bannedDirectly--;

    bannedDeferred++;

    Q_UNUSED(reply);
}

void Erwin::nmPrivateRead(QNetworkReply *reply)
{
    // private mesages receiver
}

void Erwin::nmChannelResolved(QNetworkReply *reply)
{
    log ("Got data from Galaxy about the planet! Let's extract...", "http");
    QString data(reply->readAll());

    QRegExp rx("chanid=([0-9]{1,10})");
    rx.indexIn(data);
    channelId = rx.cap(1);

    log ("Probably, channel ID is "+channelId);
}

void Erwin::sendStatistics()
{
    sendMessage(QString("EMO 1024 :Banned %1 bots (%2 direct, %3 deferred)").arg(bannedDeferred+bannedDirectly)
                .arg(bannedDirectly).arg(bannedDeferred));
}

void Erwin::restartDaemon()
{
    qApp->quit();
    QProcess::startDetached(qApp->arguments()[0], qApp->arguments());
}

void Erwin::connectAgain()
{
    log ("Reconnecting: opening the socket again.");
    socket->connectToHost(chatServerAddress, chatServerPort);
}

void Erwin::sayPidor()
{
    if (pidorate && pidoringPhrases.count() > 0)
    {
        QString phrase = pidoringPhrases.at(rand() % pidoringPhrases.count() - 1);

        say(QString(phrase).arg(lastPidor));
        QTimer::singleShot(rand()% pidorateDelay + 5000, this, SLOT(sayPidor()));
    }
}
