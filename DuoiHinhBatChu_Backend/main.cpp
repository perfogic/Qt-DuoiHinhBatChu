
#include <QCoreApplication>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <QList>
#include <QJsonArray>
#include <QStringList>
#include <QRandomGenerator>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QVector>
#include <QMetaObject>
#include <QSslSocket>
#include <QThread>
#include <QMutex>

// Components
#include "auth.h"
#include "room.h"
#include "user.h"
#include "rank.h"
#include "question.h"
#include "database.h"
#include "protocolSocket.h"

#define _DATABASE_NAME "SP2"

QMap<QString, QJsonObject> roomDataMap;
QMap<int, QString> userToRoomId;
QMap<QString, QList<QTcpSocket*>> connectionTable; // Store Mapping roomId => joinUsersInARoom
QList<QTcpSocket*> activeUsers;
QMutex mutex;


Auth* authService;
Room* roomService;
User* userService;
Rank* rankService;

void handleIncomingData(QTcpSocket *socket);
int getUserIdFromSessionId(QString *sessionId);
QByteArray convertJsonToByteArray(QJsonObject val);

void connectToSocket() {
    QTcpServer *server = new QTcpServer();  // Tạo đối tượng QTcpServer

    if (!server->listen(QHostAddress::LocalHost, 9454)) {  // Khởi động server và liên kết đến port 1234
        qInfo() << "Server could not start";
        return;
    }

    qInfo() << "Server socket started on" << server->serverAddress().toString() << ":" << server->serverPort();

    QObject::connect(server, &QTcpServer::newConnection, [=]() {
        QThread* thread = new QThread();
        QObject::connect(thread, &QThread::started, [=]() {
            QTcpSocket* socket = server->nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, [=]() {
                handleIncomingData(socket);
            });
        });
        thread->start();
    });
}

//void connectToSslSocket() {
//    QSslSocket *sslSocket = new QSslSocket();
//    QSslConfiguration sslConfig = sslSocket->sslConfiguration();
//    sslConfig.setProtocol(QSsl::TlsV1_2);
//    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
//    sslSocket->setSslConfiguration(sslConfig);
//}

void createDBConnection(QCoreApplication *a) {
    QSqlDatabase database = Database::getInstance().getDatabase();

    QObject::connect(a, &QCoreApplication::aboutToQuit, [&]() {
        database.close();
        QSqlDatabase::removeDatabase(database.connectionName());
    });
}


QString generateRoomId()
{
    const QString alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    const int length = 6;

    QString roomId;
    QRandomGenerator generator = QRandomGenerator::securelySeeded();

    for (int i = 0; i < length; ++i) {
        const int index = generator.bounded(alphabet.length());
        roomId.append(alphabet.at(index));
    }

    return roomId;
}

QString getSessionIdFromHeader(const QHttpServerRequest &req) {
    QList<QPair<QByteArray, QByteArray>> headers = req.headers();
    for (const QPair<QByteArray, QByteArray>& header : headers) {
        if (header.first == "Authorization") {
            QString bearerToken = QString(header.second).replace("Bearer ", "");
            // sử dụng bearerToken ở đây để thực hiện các hoạt động khác
            return bearerToken;
        }
    }
    return nullptr;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    authService = new Auth;
    roomService = new Room;
    userService = new User;
    rankService = new Rank;

    createDBConnection(&a);
    connectToSocket();

    QHttpServer server;

    // Auth Apis
    server.route("/api/auth/login", QHttpServerRequest::Method::Post, [authService](const QHttpServerRequest &req) {
        QByteArray requestData = req.body();
        QJsonObject json = QJsonDocument::fromJson(requestData).object();

        // Lấy tài khoản và mật khẩu từ yêu cầu Api
        QString username = json.value("username").toString();
        QString password = json.value("password").toString();

        qInfo() << username << password;

        return authService->login(&username, &password);
    });

    server.route("/api/auth/register", QHttpServerRequest::Method::Post, [authService](const QHttpServerRequest &req) {
        QByteArray requestData = req.body();
        QJsonObject json = QJsonDocument::fromJson(requestData).object();

        // Lấy tài khoản và mật khẩu từ yêu cầu Api
        QString username = json.value("username").toString();
        QString password = json.value("password").toString();

        return authService->registerAccount(&username, &password);
    });

    // Room Apis
    server.route("/api/room/all", QHttpServerRequest::Method::Get, [roomService](const QHttpServerRequest &req) {
        QByteArray requestData = req.body();

        return roomService->getAllRoom(roomDataMap);
    });

    // Rank Apis
    server.route("/api/rank", QHttpServerRequest::Method::Get, [rankService](const QHttpServerRequest &req) {
        QUrl url = req.url();
        QUrlQuery query(url.query());

        // Lấy giá trị của tham số "page"
        QString pageStr = query.queryItemValue("page");
        int page = pageStr.toInt();

        // Lấy giá trị của tham số "limit"
        QString limitStr = query.queryItemValue("limit");
        int limit = limitStr.toInt();

        return rankService->getAllRanks(page, limit);
    });

    // Random Questions
    server.route("/api/question/random", QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) {
        QJsonObject data = Question::getRandomQuestion();
        return QHttpServerResponse(QJsonObject{ {"message", "Get all rank successfully" }, {"data", data} }, QHttpServerResponder::StatusCode::Ok);
    });

    server.listen(QHostAddress::LocalHost, 8000);


    return a.exec();
}

void handleIncomingData(QTcpSocket * socket) {
    QByteArray clientData = socket->readAll();
    QList<QByteArray> byteArrays = clientData.split('\n');
    qInfo() << "[+] Byte Arrays: " << clientData;

    mutex.lock();
    for (int i = 0; i < byteArrays.size() - 1; i++) {
        qInfo() << "[+] Client Data: " << byteArrays.at(i);
        QJsonDocument jsonDoc = QJsonDocument::fromJson(byteArrays.at(i));
        QJsonObject jsonObj = jsonDoc.object();

        qInfo() << "[+] Incoming Data: " << jsonObj;
        QString sessionId = jsonObj["sessionId"].toString();
        bool isSendToCaller = jsonObj["isSendToCaller"].toBool();

        QJsonObject data;
        QString roomId;
        QString content;
        QList<QTcpSocket*> tableData;
        int status;
        int userId = userService->getUserFromSessionId(&sessionId, activeUsers, socket);

        if (userId == 0) {
            if (activeUsers.contains(socket)) {
                userService->removeActiveUser(userId, socket, &activeUsers);
            }
            return;
        }
        switch(jsonObj["type"].toInt()) {
        case static_cast<int>(SocketType::REQUEST_SAVE_ACTIVE_USER):
            data = userService->addActiveUser(userId, socket, &activeUsers);
            socket->write(convertJsonToByteArray(data));
            socket->flush();
            break;
        case static_cast<int>(SocketType::REQUEST_DELETE_ACTIVE_USER):
            userService->removeActiveUser(userId, socket, &activeUsers);
            break;
        case static_cast<int>(SocketType::SEND_REQUEST_ALL_ROOM):
            data = roomService->getAllCurrentRoom(userId, roomDataMap);

            for (int i = 0; i < activeUsers.length(); i++) {
                QTcpSocket* _socket = activeUsers.at(i);

                // Nếu như thằng create room thì sẽ k nhận cái socket này
                if (!isSendToCaller && socket == _socket) {
                    continue;
                }

                // Sử dụng socket ở đây
                _socket->write(convertJsonToByteArray(data));
                _socket->flush();
            }

            break;
        case static_cast<int>(SocketType::CREATE_ROOM):
            roomId = nullptr;
            do {
                roomId = generateRoomId();
            } while (roomDataMap.contains(roomId));

            tableData = connectionTable.take(roomId);
            tableData.append(socket);
            connectionTable.insert(roomId, tableData);

            data = roomService->createRoom(userId, &roomDataMap, &roomId, &userToRoomId);

            socket->write(convertJsonToByteArray(data));

            break;

        case static_cast<int>(SocketType::LEAVE_ROOM):
            roomId = jsonObj["roomId"].toString();

            data = roomService->leaveRoom(userId, &roomDataMap, &roomId, &userToRoomId);

            tableData = connectionTable.take(roomId);

            for (int i = 0; i < tableData.length(); i++) {
                QTcpSocket* _socket = tableData.at(i);

                // Sử dụng socket ở đây
                _socket->write(convertJsonToByteArray(data));
                _socket->flush();
            }

            tableData.removeAt(tableData.indexOf(socket));
            connectionTable.insert(roomId, tableData);
            break;
        case static_cast<int>(SocketType::JOIN_ROOM):
            roomId = jsonObj["roomId"].toString();

            data = roomService->joinRoom(userId, &roomDataMap, &roomId, &userToRoomId);
            status = data.value("code").toInt();

            if (status != 200) {
                socket->write(convertJsonToByteArray(data));
                socket->flush();
            }
            else {
                tableData = connectionTable.take(roomId);
                tableData.append(socket);

                for (int i = 0; i < tableData.length(); i++) {
                    QTcpSocket* _socket = tableData.at(i);

                    qInfo() << "\nSend data" << data;
                    _socket->write(convertJsonToByteArray(data));
                    _socket->flush();
                }

                connectionTable.insert(roomId, tableData);
            }

            break;
        case static_cast<int>(SocketType::SEND_ANSWER):
            roomId = jsonObj["roomId"].toString();
            content = jsonObj["content"].toString();

            data = roomService->sendAnswer(userId, &roomDataMap, &roomId, &userToRoomId, content);

            tableData = connectionTable.take(roomId);

            for (int i = 0; i < tableData.length(); i++) {
                QTcpSocket* _socket = tableData.at(i);

                // Sử dụng socket ở đây
                _socket->write(convertJsonToByteArray(data));
                _socket->flush();
            }

            connectionTable.insert(roomId, tableData);
            break;
        case static_cast<int>(SocketType::START_ROOM):
            roomId = jsonObj["roomId"].toString();

            data = roomService->startRoom(userId, &roomDataMap, &roomId, &userToRoomId);

            tableData = connectionTable.take(roomId);

            for (int i = 0; i < tableData.length(); i++) {
                QTcpSocket* _socket = tableData.at(i);

                // Sử dụng socket ở đây
                _socket->write(convertJsonToByteArray(data));
                _socket->flush();
            }

            connectionTable.insert(roomId, tableData);
            break;
        case static_cast<int>(SocketType::NEXT_QUESTION):
            roomId = jsonObj["roomId"].toString();

            data = roomService->nextQuestion(userId, &roomDataMap, &roomId, &userToRoomId);

            qInfo() << "RUN HERE" << data;

            tableData = connectionTable.take(roomId);

            for (int i = 0; i < tableData.length(); i++) {
                QTcpSocket* _socket = tableData.at(i);

                // Sử dụng socket ở đây
                _socket->write(convertJsonToByteArray(data));
                _socket->flush();
            }

            connectionTable.insert(roomId, tableData);
            break;
        case static_cast<int>(SocketType::GET_USER_INFO):
            data = userService->getUserInfo(userId);
            socket->write(convertJsonToByteArray(data));
            socket->flush();
            break;
        default:
            qInfo() << "Running into Default case";
            qInfo() << "[+] Default Data: " << jsonObj;
            break;
        }
    }
    mutex.unlock();

}

QByteArray convertJsonToByteArray(QJsonObject val) {
    QJsonDocument jsonDoc(val);
    QByteArray byteArray = jsonDoc.toJson(QJsonDocument::Compact);
    return byteArray + "\n";
}
