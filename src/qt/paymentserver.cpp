// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/paymentserver.h>

#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/paymentipc.h>

#include <chainparams.h>
#include <common/args.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <node/interface_ui.h>
#include <policy/policy.h>
#include <wallet/wallet.h>

#include <cstdlib>
#include <memory>

#include <QApplication>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QFileOpenEvent>
#include <QHash>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QStringList>
#include <QUrlQuery>

const QString BITCOIN_IPC_PREFIX("sugarchain:");

//
// Create a name that is unique for:
//  testnet / non-testnet
//  data directory
//
static QString ipcServerName()
{
    return PaymentIpcServerName(GUIUtil::PathToQString(gArgs.GetDataDirNet()));
}

//
// We store payment URIs and requests received before
// the main GUI window is up and ready to ask the user
// to send payment.

static QSet<QString> savedPaymentRequests;

//
// Sending to the server is done synchronously, at startup.
// If the server isn't already running, startup continues,
// and the items in savedPaymentRequest will be handled
// when uiReady() is called.
//
// Warning: ipcSendCommandLine() is called early in init,
// so don't use "Q_EMIT message()", but "QMessageBox::"!
//
void PaymentServer::ipcParseCommandLine(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++)
    {
        QString arg(argv[i]);
        if (arg.startsWith("-")) continue;

        if (arg.startsWith(BITCOIN_IPC_PREFIX, Qt::CaseInsensitive)) // sugarchain: URI
        {
            savedPaymentRequests.insert(arg);
        }
    }
}

//
// Sending to the server is done synchronously, at startup.
// If the server isn't already running, startup continues,
// and the items in savedPaymentRequest will be handled
// when uiReady() is called.
//
bool PaymentServer::ipcSendCommandLine(QString& error)
{
    const QString datadir = GUIUtil::PathToQString(gArgs.GetDataDirNet());
    const auto result = SendPaymentIpc({PaymentIpcServerName(datadir), PaymentIpcServerName(datadir, true)}, savedPaymentRequests, error);
    if (result == PaymentIpcResult::SENT) savedPaymentRequests.clear();
    return result == PaymentIpcResult::SENT;
}

PaymentServer::PaymentServer(QObject* parent, bool startLocalServer)
    : QObject(parent)
{
    // Install global event filter to catch QFileOpenEvents
    // on Mac: sent when you click sugarchain: links
    // other OSes: helpful when dealing with payment request files
    if (parent)
        parent->installEventFilter(this);

    if (startLocalServer) {
        const QString name = ipcServerName();
        const QByteArray id = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex();
        const QString lock_path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                                  QStringLiteral("/Sugarchain/.en-ipc/") + QString::fromLatin1(id) + QStringLiteral(".lock");
        uriServer = new QLocalServer(this);
        QString error;
        if (!ListenPaymentIpc(*uriServer, name, lock_path, ipcClaim, error)) {
            QMessageBox::critical(nullptr, tr("Payment request error"), error);
        } else {
            connect(uriServer, &QLocalServer::newConnection, this, &PaymentServer::handleURIConnection);
        }
    }
}

PaymentServer::~PaymentServer()
{
    // Close/remove our socket before releasing the lifetime startup claim.
    delete uriServer;
}

bool PaymentServer::isListening() const
{
    return uriServer && uriServer->isListening();
}

//
// OSX-specific way of handling sugarchain: URIs
//
bool PaymentServer::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *fileEvent = static_cast<QFileOpenEvent*>(event);
        if (!fileEvent->file().isEmpty())
            handleURIOrFile(fileEvent->file());
        else if (!fileEvent->url().isEmpty())
            handleURIOrFile(fileEvent->url().toString());

        return true;
    }

    return QObject::eventFilter(object, event);
}

void PaymentServer::uiReady()
{
    saveURIs = false;
    for (const QString& s : savedPaymentRequests)
    {
        handleURIOrFile(s);
    }
    savedPaymentRequests.clear();
}

void PaymentServer::handleURIOrFile(const QString& s)
{
    if (saveURIs)
    {
        savedPaymentRequests.insert(s);
        return;
    }

    if (s.startsWith("sugarchain://", Qt::CaseInsensitive))
    {
        Q_EMIT message(tr("URI handling"), tr("'sugarchain://' is not a valid URI. Use 'sugarchain:' instead."),
            CClientUIInterface::MSG_ERROR);
    }
    else if (s.startsWith(BITCOIN_IPC_PREFIX, Qt::CaseInsensitive)) // sugarchain: URI
    {
        QUrlQuery uri((QUrl(s)));
        // normal URI
        {
            SendCoinsRecipient recipient;
            if (GUIUtil::parseBitcoinURI(s, &recipient))
            {
                std::string error_msg;
                const CTxDestination dest = DecodeDestination(recipient.address.toStdString(), error_msg);

                if (!IsValidDestination(dest)) {
                    if (uri.hasQueryItem("r")) {  // payment request
                        Q_EMIT message(tr("URI handling"),
                            tr("Cannot process payment request because BIP70 is not supported.\n"
                               "Due to widespread security flaws in BIP70 it's strongly recommended that any merchant instructions to switch wallets be ignored.\n"
                               "If you are receiving this error you should request the merchant provide a BIP21 compatible URI."),
                            CClientUIInterface::ICON_WARNING);
                    }
                    Q_EMIT message(tr("URI handling"), QString::fromStdString(error_msg),
                        CClientUIInterface::MSG_ERROR);
                }
                else
                    Q_EMIT receivedPaymentRequest(recipient);
            }
            else
                Q_EMIT message(tr("URI handling"),
                    tr("URI cannot be parsed! This can be caused by an invalid Sugarchain address or malformed URI parameters."),
                    CClientUIInterface::ICON_WARNING);

            return;
        }
    }

    if (QFile::exists(s)) // payment request file
    {
        Q_EMIT message(tr("Payment request file handling"),
            tr("Cannot process payment request because BIP70 is not supported.\n"
               "Due to widespread security flaws in BIP70 it's strongly recommended that any merchant instructions to switch wallets be ignored.\n"
               "If you are receiving this error you should request the merchant provide a BIP21 compatible URI."),
            CClientUIInterface::ICON_WARNING);
    }
}

void PaymentServer::handleURIConnection()
{
    QLocalSocket* client = uriServer->nextPendingConnection();
    connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
    const auto read_request = [this, client] {
        QDataStream in(client);
        in.setVersion(QDataStream::Qt_4_0);
        in.startTransaction();
        QString request;
        in >> request;
        if (!in.commitTransaction()) return;
        // A connection carries one request, matching the legacy protocol.
        client->disconnect(this);
        handleURIOrFile(request);
        client->disconnectFromServer();
        client->deleteLater();
    };
    connect(client, &QLocalSocket::readyRead, this, read_request);
    read_request();
}

void PaymentServer::setOptionsModel(OptionsModel *_optionsModel)
{
    this->optionsModel = _optionsModel;
}
