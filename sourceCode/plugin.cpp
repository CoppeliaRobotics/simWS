#include <string>
#include <stdexcept>
#include <optional>

#include <QTcpServer>
#include <QWebSocket>
#include <QWebSocketServer>

#include <simPlusPlus/Plugin.h>
#include <simPlusPlus/Handles.h>

#include "config.h"
#include "plugin.h"
#include "stubs.h"

std::ostream& operator<<(std::ostream& os, const QString& str)
{
    return os << str.toStdString();
}

std::ostream& operator<<(std::ostream& os, const QByteArray& byteArray)
{
    return os << byteArray.toStdString();
}

class WebSocketClient : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketClient(const QString &url, sim::Handles<WebSocketClient*> &cliHandles, sim::Handles<QWebSocket*> &connHandles, bool verbose, QObject *parent = nullptr)
        : QObject(parent),
          cliHandles(cliHandles),
          connHandles(connHandles),
          verbose(verbose)
    {
        connect(&ws, &QWebSocket::connected, this, &WebSocketClient::onOpen);
        connect(&ws, &QWebSocket::disconnected, this, &WebSocketClient::onClose);
        connect(&ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, &WebSocketClient::onFail);
        ws.open(QUrl(url));
    }

public slots:
    void onOpen()
    {
        if(verbose)
            std::cout << "WebSocketClient: opened connection" << std::endl;
        connect(&ws, &QWebSocket::textMessageReceived, this, &WebSocketClient::onTextMessage);
        connect(&ws, &QWebSocket::binaryMessageReceived, this, &WebSocketClient::onBinaryMessage);
        if(openHandler)
        {
            openCallback_in in;
            in.serverOrClientHandle = cliHandles.add(this, scriptID);
            in.connectionHandle = connHandles.add(&ws, scriptID);
            openCallback_out out;
            openCallback(scriptID, openHandler->c_str(), &in, &out);
        }
    }

    void onClose()
    {
        if(verbose)
            std::cout << "WebSocketClient: closed connection" << std::endl;
        if(closeHandler)
        {
            closeCallback_in in;
            in.serverOrClientHandle = cliHandles.add(this, scriptID);
            in.connectionHandle = connHandles.add(&ws, scriptID);
            closeCallback_out out;
            closeCallback(scriptID, closeHandler->c_str(), &in, &out);
        }
        connHandles.remove(&ws);
    }

    void onFail(QAbstractSocket::SocketError error)
    {
        if(verbose)
            std::cout << "WebSocketClient: failure: " << error << std::endl;
        if(failHandler)
        {
            failCallback_in in;
            in.serverOrClientHandle = cliHandles.add(this, scriptID);
            in.connectionHandle = connHandles.add(&ws, scriptID);
            failCallback_out out;
            failCallback(scriptID, failHandler->c_str(), &in, &out);
        }
    }

    void onTextMessage(const QString &message)
    {
        if(verbose)
            std::cout << "WebSocketClient: received text message: " << message << std::endl;
        if(messageHandler)
        {
            messageCallback_in in;
            in.serverOrClientHandle = cliHandles.add(this, scriptID);
            in.connectionHandle = connHandles.add(&ws, scriptID);
            in.data = message.toStdString();
            messageCallback_out out;
            if(verbose)
                std::cout << "WebSocketClient::onTextMessage() - before callback" << std::endl;
            messageCallback(scriptID, messageHandler->c_str(), &in, &out);
            if(verbose)
                std::cout << "WebSocketClient::onTextMessage() - after callback" << std::endl;
        }
    }

    void onBinaryMessage(const QByteArray &message)
    {
        if(verbose)
            std::cout << "WebSocketClient: received binary message: " << message << std::endl;
        if(messageHandler)
        {
            messageCallback_in in;
            in.serverOrClientHandle = cliHandles.add(this, scriptID);
            in.connectionHandle = connHandles.add(&ws, scriptID);
            in.data = message.toStdString();
            messageCallback_out out;
            messageCallback(scriptID, messageHandler->c_str(), &in, &out);
        }
    }

public:
    int scriptID;
    std::optional<std::string> openHandler;
    std::optional<std::string> failHandler;
    std::optional<std::string> closeHandler;
    std::optional<std::string> messageHandler;

private:
    QWebSocket ws;
    sim::Handles<WebSocketClient*> &cliHandles;
    sim::Handles<QWebSocket*> &connHandles;
    bool verbose;
};

class WebSocketServer : public QObject
{
    Q_OBJECT
public:
    explicit WebSocketServer(quint16 port, sim::Handles<WebSocketServer*> &srvHandles, sim::Handles<QWebSocket*> &connHandles, bool verbose, QObject *parent = nullptr)
        : QObject(parent),
          ts(parent),
          ws(QString::fromStdString(*sim::getNamedStringParam("simWS.userAgent")), QWebSocketServer::NonSecureMode, this),
          srvHandles(srvHandles),
          connHandles(connHandles)
    {
#if 0
        if(ws.listen(QHostAddress::Any, port))
        {
            connect(&ws, &QWebSocketServer::newConnection, this, &WebSocketServer::onOpen);
            connect(&ws, &QWebSocketServer::closed, this, &WebSocketServer::onClose);
            connect(&ws, &QWebSocketServer::serverError, this, &WebSocketClient::onFail);
        }
#else
        if(ts.listen(QHostAddress::Any, port))
        {
            connect(&ts, &QTcpServer::newConnection, this, &WebSocketServer::handleNewConnection);
        }
#endif
    }

    void handleNewConnection()
    {
        if(verbose)
            std::cout << "WebSocketServer: handleNewConnection" << std::endl;
        QTcpSocket *socket = ts.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { processRequest(socket); });
    }

    void processRequest(QTcpSocket *socket)
    {
        if(verbose)
            std::cout << "WebSocketServer: processRequest" << std::endl;
        if(!socket->canReadLine()) return;

        QByteArray requestData = socket->readAll();
        QString requestLine = QString::fromUtf8(requestData.split('\n').first());

        if(requestLine.startsWith("GET /"))
        {
            QString resource = requestLine.mid(4);
            if(httpHandler)
            {
                httpCallback_in in;
                in.serverHandle = srvHandles.add(this, scriptID);
                //in.connectionHandle = connHandles.add(nullptr, scriptID);
                in.resource = resource.toStdString();
                in.data = "";
                httpCallback_out out;
                int httpStatus = 500;
                QByteArray responseBody;
                if(httpCallback(scriptID, httpHandler->c_str(), &in, &out))
                {
                    httpStatus = out.status;
                    responseBody = QByteArray::fromStdString(out.data);
                }
                sendResponse(socket, httpStatus, responseBody);
            }
            else sendErrorResponse(socket);
        }
        else if(requestData.contains("Upgrade: websocket"))
        {
            upgradeToWebSocket(socket, requestData);
        }
        else
        {
            sendErrorResponse(socket);
        }
    }

    void sendResponse(QTcpSocket *socket, int status, const QByteArray &content = "")
    {
        if(verbose)
            std::cout << "WebSocketServer: processRequest" << std::endl;
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + "\r\n"
                              "Connection: close\r\n" + content;
                              //"Content-Type: text/html\r\n"
                              //"Content-Length: " + QByteArray::number(file.size()) + "\r\n\r\n" + file.readAll();
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    void sendErrorResponse(QTcpSocket *socket)
    {
        if(verbose)
            std::cout << "WebSocketServer: processRequest" << std::endl;
        QByteArray response = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    void upgradeToWebSocket(QTcpSocket *socket, const QByteArray &requestData)
    {
        if(verbose)
            std::cout << "WebSocketServer: processRequest" << std::endl;
        socket->setParent(nullptr);
        ws.handleConnection(socket); // Hand off to QWebSocketServer
    }

    void onOpen()
    {
        QWebSocket *c = ws.nextPendingConnection();
        if(verbose)
            std::cout << "WebSocketServer: opened connection " << c << std::endl;
        connect(c, &QWebSocket::textMessageReceived, this, &WebSocketServer::onTextMessage);
        connect(c, &QWebSocket::binaryMessageReceived, this, &WebSocketServer::onBinaryMessage);
        connect(c, &QWebSocket::disconnected, this, &WebSocketServer::onClose);
        clients << c;
        if(openHandler)
        {
            openCallback_in in;
            in.serverOrClientHandle = srvHandles.add(this, scriptID);
            in.connectionHandle = connHandles.add(c, scriptID);
            openCallback_out out;
            openCallback(scriptID, openHandler->c_str(), &in, &out);
        }
    }

    void onClose()
    {
        if(QWebSocket *c = qobject_cast<QWebSocket*>(sender())) {
            if(verbose)
                std::cout << "WebSocketServer: closed connection " << c << std::endl;
            if(closeHandler)
            {
                closeCallback_in in;
                in.serverOrClientHandle = srvHandles.add(this, scriptID);
                in.connectionHandle = connHandles.add(c, scriptID);
                closeCallback_out out;
                closeCallback(scriptID, closeHandler->c_str(), &in, &out);
            }
            connHandles.remove(c);
            clients.removeAll(c);
            c->deleteLater();
        }
    }

    void onFail(QWebSocketProtocol::CloseCode closeCode)
    {
        if(verbose)
            std::cout << "WebSocketServer: failure: " << closeCode << std::endl;
        if(failHandler)
        {
            failCallback_in in;
            in.serverOrClientHandle = srvHandles.add(this, scriptID);
            //in.connectionHandle = connHandles.add(nullptr, scriptID);
            failCallback_out out;
            failCallback(scriptID, failHandler->c_str(), &in, &out);
        }
    }

    void onTextMessage(const QString &message)
    {
        if(verbose)
            std::cout << "WebSocketServer: received text message: " << message << std::endl;
        if(QWebSocket *c = qobject_cast<QWebSocket*>(sender())) {
            if(messageHandler)
            {
                messageCallback_in in;
                in.serverOrClientHandle = srvHandles.add(this, scriptID);
                in.connectionHandle = connHandles.add(c, scriptID);
                in.data = message.toStdString();
                messageCallback_out out;
                messageCallback(scriptID, messageHandler->c_str(), &in, &out);
            }
        }
    }

    void onBinaryMessage(const QByteArray &message)
    {
        if(verbose)
            std::cout << "WebSocketServer: received binary message: " << message << std::endl;
        if(QWebSocket *c = qobject_cast<QWebSocket*>(sender())) {
            if(messageHandler)
            {
                messageCallback_in in;
                in.serverOrClientHandle = srvHandles.add(this, scriptID);
                in.connectionHandle = connHandles.add(c, scriptID);
                in.data = message.toStdString();
                messageCallback_out out;
                messageCallback(scriptID, messageHandler->c_str(), &in, &out);
            }
        }
    }

public:
    int scriptID;
    std::optional<std::string> openHandler;
    std::optional<std::string> failHandler;
    std::optional<std::string> closeHandler;
    std::optional<std::string> messageHandler;
    std::optional<std::string> httpHandler;

private:
    QTcpServer ts;
    QWebSocketServer ws;
    QList<QWebSocket*> clients;
    sim::Handles<WebSocketServer*> &srvHandles;
    sim::Handles<QWebSocket*> &connHandles;
    bool verbose;
};

#include "plugin.moc"

class Plugin : public sim::Plugin
{
public:
    void onInit()
    {
        if(!registerScriptStuff())
            throw std::runtime_error("failed to register script stuff");

        setExtVersion("WebSocket Plugin");
        setBuildDate(BUILD_DATE);

        if(!sim::getNamedStringParam("simWS.userAgent"))
        {
            std::vector<int> v{0, 0, 0, sim::getInt32Param(sim_intparam_program_full_version)};
            for(int i = 3; i > 0; i--) v[i - 1] = v[i] / 100;
            for(int i = 0; i < 4; i++) v[i] = v[i] % 100;
            auto p = sim::getInt32Param(sim_intparam_platform);
            sim::setNamedStringParam("simWS.userAgent",
                sim::util::sprintf("CoppeliaSim/%d.%d.%drev%d %s",
                    v[0], v[1], v[2], v[3],
                    p == 0 ? "Windows" :
                    p == 1 ? "macOS" :
                    p == 2 ? "Linux" :
                    "Unknown-platform"
                )
            );
        }
    }

    void onCleanup()
    {
    }

    void onInstancePass(const sim::InstancePassFlags &flags)
    {
    }

    void onScriptStateAboutToBeDestroyed(int scriptHandle, long long scriptUid)
    {
        for(auto srv : srvHandles.find(scriptHandle))
            srvHandles.remove(srv)->deleteLater();

        for(auto cli : cliHandles.find(scriptHandle))
            cliHandles.remove(cli)->deleteLater();
    }

    void start(start_in *in, start_out *out)
    {
        bool verbose = sim::getNamedBoolParam("simWS.verbose").value_or(false);
        auto *srv = new WebSocketServer(in->listenPort, srvHandles, connHandles, verbose);
        srv->scriptID = in->_.scriptID;
        out->serverHandle = srvHandles.add(srv, in->_.scriptID);
    }

    void stop(stop_in *in, stop_out *out)
    {
        auto *srv = srvHandles.get(in->serverHandle);
        srvHandles.remove(srv);
        delete srv;
    }

    void connect(connect_in *in, connect_out *out)
    {
        bool verbose = sim::getNamedBoolParam("simWS.verbose").value_or(false);
        auto *cli = new WebSocketClient(QString::fromStdString(in->uri), cliHandles, connHandles, verbose);
        cli->scriptID = in->_.scriptID;
        out->clientHandle = cliHandles.add(cli, in->_.scriptID);
    }

    void disconnect(disconnect_in *in, disconnect_out *out)
    {
        auto *cli = cliHandles.get(in->clientHandle);
        cliHandles.remove(cli);
        delete cli;
    }

    void send(send_in *in, send_out *out)
    {
        if(in->opcode < 0 || in->opcode > 2)
            throw sim::exception("invalid opcode: %d", in->opcode);
        if(in->opcode != 1)
            throw sim::exception("unsupported opcode: %d", in->opcode);
        try
        {
            auto *srv = srvHandles.get(in->serverOrClientHandle);
            auto *conn = connHandles.get(in->connectionHandle);
            conn->sendTextMessage(QString::fromStdString(in->data));
        }
        catch(...) {}
        try
        {
            auto *cli = cliHandles.get(in->serverOrClientHandle);
            auto *conn = connHandles.get(in->connectionHandle);
            conn->sendTextMessage(QString::fromStdString(in->data));
        }
        catch(...) {}
    }

    void setOpenHandler(setOpenHandler_in *in, setOpenHandler_out *out)
    {
        try
        {
            auto *srv = srvHandles.get(in->serverOrClientHandle);
            srv->openHandler = in->callbackFn;
        }
        catch(...) {}
        try
        {
            auto *cli = cliHandles.get(in->serverOrClientHandle);
            cli->openHandler = in->callbackFn;
        }
        catch(...) {}
    }

    void setFailHandler(setFailHandler_in *in, setFailHandler_out *out)
    {
        try
        {
            auto *srv = srvHandles.get(in->serverOrClientHandle);
            srv->failHandler = in->callbackFn;
        }
        catch(...) {}
        try
        {
            auto *cli = cliHandles.get(in->serverOrClientHandle);
            cli->failHandler = in->callbackFn;
        }
        catch(...) {}
    }

    void setCloseHandler(setCloseHandler_in *in, setCloseHandler_out *out)
    {
        try
        {
            auto *srv = srvHandles.get(in->serverOrClientHandle);
            srv->closeHandler = in->callbackFn;
        }
        catch(...) {}
        try
        {
            auto *cli = cliHandles.get(in->serverOrClientHandle);
            cli->closeHandler = in->callbackFn;
        }
        catch(...) {}
    }

    void setMessageHandler(setMessageHandler_in *in, setMessageHandler_out *out)
    {
        try
        {
            auto *srv = srvHandles.get(in->serverOrClientHandle);
            srv->messageHandler = in->callbackFn;
        }
        catch(...) {}
        try
        {
            auto *cli = cliHandles.get(in->serverOrClientHandle);
            cli->messageHandler = in->callbackFn;
        }
        catch(...) {}
    }

    void setHTTPHandler(setHTTPHandler_in *in, setHTTPHandler_out *out)
    {
        auto *srv = srvHandles.get(in->serverHandle);
        srv->httpHandler = in->callbackFn;
    }

private:
    sim::Handles<WebSocketServer*> srvHandles{"simWS.Server"};
    sim::Handles<QWebSocket*> connHandles{"simWS.Connection"};
    sim::Handles<WebSocketClient*> cliHandles{"simWS.Client"};
    bool verbose;
};

SIM_PLUGIN(Plugin)
#include "stubsPlusPlus.cpp"
