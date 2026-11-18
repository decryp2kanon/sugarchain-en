// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/uritests.h>

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QUrl>

void URITests::uriTests()
{
    SendCoinsRecipient rv;
    QUrl uri;

    // Bitcoin payment URIs must not be accepted as Sugarchain requests.
    uri.setUrl(QString("bitcoin:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W?amount=1"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri.toString(), &rv));
    uri.setUrl(QString("bitcoin:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=1"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri.toString(), &rv));
    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?req-dontexist="));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?dontexist="));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?label=Wikipedia Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.label == QString("Wikipedia Example Address"));
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=0.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100000);

    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=1.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100100000);

    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=100&label=Wikipedia Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Wikipedia Example"));

    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?message=Wikipedia Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.label == QString());

    QVERIFY(GUIUtil::parseBitcoinURI("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?message=Wikipedia Example Address", &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.label == QString());

    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?req-message=Wikipedia Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    // Commas in amounts are not allowed.
    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=1,000&label=Wikipedia Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=1,000.0&label=Wikipedia Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    // There are two amount specifications. The last value wins.
    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=100&amount=200&label=Wikipedia Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.amount == 20000000000LL);
    QVERIFY(rv.label == QString("Wikipedia Example"));

    // The first amount value is correct. However, the second amount value is not valid. Hence, the URI is not valid.
    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=100&amount=1,000&label=Wikipedia Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    // Test label containing a question mark ('?').
    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=100&label=?"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("?"));

    // Escape sequences are not supported.
    uri.setUrl(QString("sugarchain:STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC?amount=100&label=%3F"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("STNtYfNH3NdJu54X7P5wCqPiAC63MuQrCC"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("%3F"));
}
