/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWebEngine module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "../util.h"
#include <QtCore/qbuffer.h>
#include <QtTest/QtTest>
#include <QtWebEngineCore/qwebengineurlrequestjob.h>
#include <QtWebEngineCore/qwebengineurlschemehandler.h>
#include <QtWebEngineWidgets/qwebengineprofile.h>
#include <QtWebEngineWidgets/qwebenginepage.h>
#include <QtWebEngineWidgets/qwebenginesettings.h>
#include <QtWebEngineWidgets/qwebengineview.h>
#include <QtWebEngineWidgets/qwebenginedownloaditem.h>

class tst_QWebEngineProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaultProfile();
    void profileConstructors();
    void clearDataFromCache();
    void disableCache();
    void urlSchemeHandlers();
    void urlSchemeHandlerFailRequest();
    void urlSchemeHandlerFailOnRead();
    void urlSchemeHandlerStreaming();
    void customUserAgent();
    void httpAcceptLanguage();
    void downloadItem();
    void changePersistentPath();
};

void tst_QWebEngineProfile::defaultProfile()
{
    QWebEngineProfile *profile = QWebEngineProfile::defaultProfile();
    QVERIFY(profile);
    QVERIFY(!profile->isOffTheRecord());
    QCOMPARE(profile->storageName(), QStringLiteral("Default"));
    QCOMPARE(profile->httpCacheType(), QWebEngineProfile::DiskHttpCache);
    QCOMPARE(profile->persistentCookiesPolicy(), QWebEngineProfile::AllowPersistentCookies);
}

void tst_QWebEngineProfile::profileConstructors()
{
    QWebEngineProfile otrProfile;
    QWebEngineProfile diskProfile(QStringLiteral("Test"));

    QVERIFY(otrProfile.isOffTheRecord());
    QVERIFY(!diskProfile.isOffTheRecord());
    QCOMPARE(diskProfile.storageName(), QStringLiteral("Test"));
    QCOMPARE(otrProfile.httpCacheType(), QWebEngineProfile::MemoryHttpCache);
    QCOMPARE(diskProfile.httpCacheType(), QWebEngineProfile::DiskHttpCache);
    QCOMPARE(otrProfile.persistentCookiesPolicy(), QWebEngineProfile::NoPersistentCookies);
    QCOMPARE(diskProfile.persistentCookiesPolicy(), QWebEngineProfile::AllowPersistentCookies);
}

void tst_QWebEngineProfile::clearDataFromCache()
{
    QWebEnginePage page;

    QDir cacheDir("./tst_QWebEngineProfile_cacheDir");
    cacheDir.makeAbsolute();
    if (cacheDir.exists())
        cacheDir.removeRecursively();
    cacheDir.mkpath(cacheDir.path());

    QWebEngineProfile *profile = page.profile();
    profile->setCachePath(cacheDir.path());
    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);

    QSignalSpy loadFinishedSpy(&page, SIGNAL(loadFinished(bool)));
    page.load(QUrl("http://qt-project.org"));
    if (!loadFinishedSpy.wait(10000) || !loadFinishedSpy.at(0).at(0).toBool())
        QSKIP("Couldn't load page from network, skipping test.");

    cacheDir.refresh();
    QVERIFY(cacheDir.entryList().contains("Cache"));
    cacheDir.cd("./Cache");
    int filesBeforeClear = cacheDir.entryList().count();

    QFileSystemWatcher fileSystemWatcher;
    fileSystemWatcher.addPath(cacheDir.path());
    QSignalSpy directoryChangedSpy(&fileSystemWatcher, SIGNAL(directoryChanged(const QString &)));

    // It deletes most of the files, but not all of them.
    profile->clearHttpCache();
    QTest::qWait(1000);
    QTRY_VERIFY(directoryChangedSpy.count() > 0);

    cacheDir.refresh();
    QVERIFY(filesBeforeClear > cacheDir.entryList().count());

    cacheDir.removeRecursively();
}

void tst_QWebEngineProfile::disableCache()
{
    QWebEnginePage page;
    QDir cacheDir("./tst_QWebEngineProfile_cacheDir");
    if (cacheDir.exists())
        cacheDir.removeRecursively();
    cacheDir.mkpath(cacheDir.path());

    QWebEngineProfile *profile = page.profile();
    profile->setCachePath(cacheDir.path());
    QVERIFY(!cacheDir.entryList().contains("Cache"));

    profile->setHttpCacheType(QWebEngineProfile::NoCache);
    QSignalSpy loadFinishedSpy(&page, SIGNAL(loadFinished(bool)));
    page.load(QUrl("http://qt-project.org"));
    if (!loadFinishedSpy.wait(10000) || !loadFinishedSpy.at(0).at(0).toBool())
        QSKIP("Couldn't load page from network, skipping test.");

    cacheDir.refresh();
    QVERIFY(!cacheDir.entryList().contains("Cache"));

    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    page.load(QUrl("http://qt-project.org"));
    if (!loadFinishedSpy.wait(10000) || !loadFinishedSpy.at(1).at(0).toBool())
        QSKIP("Couldn't load page from network, skipping test.");

    cacheDir.refresh();
    QVERIFY(cacheDir.entryList().contains("Cache"));

    cacheDir.removeRecursively();
}

class RedirectingUrlSchemeHandler : public QWebEngineUrlSchemeHandler
{
public:
    void requestStarted(QWebEngineUrlRequestJob *job)
    {
        job->redirect(QUrl(QStringLiteral("data:text/plain;charset=utf-8,")
                           + job->requestUrl().fileName()));
    }
};

class ReplyingUrlSchemeHandler : public QWebEngineUrlSchemeHandler
{
    QBuffer m_buffer;
    QByteArray m_bufferData;
public:
    ReplyingUrlSchemeHandler(QObject *parent = nullptr)
        : QWebEngineUrlSchemeHandler(parent)
    {
        m_buffer.setBuffer(&m_bufferData);
    }

    void requestStarted(QWebEngineUrlRequestJob *job)
    {
        m_bufferData = job->requestUrl().toString().toUtf8();
        job->reply("text/plain;charset=utf-8", &m_buffer);
    }
};

class StreamingIODevice : public QIODevice {
    Q_OBJECT
public:
    StreamingIODevice(QObject *parent) : QIODevice(parent), m_bytesRead(0), m_bytesAvailable(0)
    {
        setOpenMode(QIODevice::ReadOnly);
        m_timer.start(100, this);
    }
    void close() override
    {
        QMutexLocker lock(&m_mutex);
        QIODevice::close();
        deleteLater();
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override
    { return m_bytesAvailable; }
    bool atEnd() const override
    {
        return (m_data.size() >= 1000 && m_bytesRead >= 1000);
    }
protected:
    void timerEvent(QTimerEvent *) override
    {
        QMutexLocker lock(&m_mutex);
        m_bytesAvailable += 200;
        m_data.append(200, 'c');
        emit readyRead();
        if (m_data.size() >= 1000) {
            m_timer.stop();
            emit readChannelFinished();
        }
    }

    qint64 readData(char *data, qint64 maxlen) override
    {
        QMutexLocker lock(&m_mutex);
        qint64 len = qMin(qint64(m_bytesAvailable), maxlen);
        if (len) {
            memcpy(data, m_data.constData() + m_bytesRead, len);
            m_bytesAvailable -= len;
            m_bytesRead += len;
        } else if (m_data.size() > 0)
            return -1;

        return len;
    }
    qint64 writeData(const char *, qint64) override
    {
        return 0;
    }

private:
    QMutex m_mutex;
    QByteArray m_data;
    QBasicTimer m_timer;
    int m_bytesRead;
    int m_bytesAvailable;
};

class StreamingUrlSchemeHandler : public QWebEngineUrlSchemeHandler
{
public:
    StreamingUrlSchemeHandler(QObject *parent = nullptr)
        : QWebEngineUrlSchemeHandler(parent)
    {
    }

    void requestStarted(QWebEngineUrlRequestJob *job)
    {
        job->reply("text/plain;charset=utf-8", new StreamingIODevice(this));
    }
};

static bool loadSync(QWebEngineView *view, const QUrl &url, int timeout = 5000)
{
    // Ripped off QTRY_VERIFY.
    QSignalSpy loadFinishedSpy(view, SIGNAL(loadFinished(bool)));
    view->load(url);
    if (loadFinishedSpy.isEmpty())
        QTest::qWait(0);
    for (int i = 0; i < timeout; i += 50) {
        if (!loadFinishedSpy.isEmpty())
            return true;
        QTest::qWait(50);
    }
    return false;
}

void tst_QWebEngineProfile::urlSchemeHandlers()
{
    RedirectingUrlSchemeHandler lettertoHandler;
    QWebEngineProfile profile(QStringLiteral("urlSchemeHandlers"));
    profile.installUrlSchemeHandler("letterto", &lettertoHandler);
    QWebEngineView view;
    view.setPage(new QWebEnginePage(&profile, &view));
    view.settings()->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
    QString emailAddress = QStringLiteral("egon@olsen-banden.dk");
    QVERIFY(loadSync(&view, QUrl(QStringLiteral("letterto:") + emailAddress)));
    QCOMPARE(toPlainTextSync(view.page()), emailAddress);

    // Install a gopher handler after the view has been fully initialized.
    ReplyingUrlSchemeHandler gopherHandler;
    profile.installUrlSchemeHandler("gopher", &gopherHandler);
    QUrl url = QUrl(QStringLiteral("gopher://olsen-banden.dk/benny"));
    QVERIFY(loadSync(&view, url));
    QCOMPARE(toPlainTextSync(view.page()), url.toString());

    // Remove the letterto scheme, and check whether it is not handled anymore.
    profile.removeUrlScheme("letterto");
    emailAddress = QStringLiteral("kjeld@olsen-banden.dk");
    QVERIFY(loadSync(&view, QUrl(QStringLiteral("letterto:") + emailAddress)));
    QVERIFY(toPlainTextSync(view.page()) != emailAddress);

    // Check if gopher is still working after removing letterto.
    url = QUrl(QStringLiteral("gopher://olsen-banden.dk/yvonne"));
    QVERIFY(loadSync(&view, url));
    QCOMPARE(toPlainTextSync(view.page()), url.toString());

    // Does removeAll work?
    profile.removeAllUrlSchemeHandlers();
    url = QUrl(QStringLiteral("gopher://olsen-banden.dk/harry"));
    QVERIFY(loadSync(&view, url));
    QVERIFY(toPlainTextSync(view.page()) != url.toString());

    // Install a handler that is owned by the view. Make sure this doesn't crash on shutdown.
    profile.installUrlSchemeHandler("aviancarrier", new ReplyingUrlSchemeHandler(&view));
    url = QUrl(QStringLiteral("aviancarrier:inspector.mortensen@politistyrke.dk"));
    QVERIFY(loadSync(&view, url));
    QCOMPARE(toPlainTextSync(view.page()), url.toString());
}

class FailingUrlSchemeHandler : public QWebEngineUrlSchemeHandler
{
public:
    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        job->fail(QWebEngineUrlRequestJob::UrlInvalid);
    }
};

class FailingIODevice : public QIODevice
{
public:
    FailingIODevice(QWebEngineUrlRequestJob *job) : m_job(job)
    {
    }

    qint64 readData(char *, qint64) Q_DECL_OVERRIDE
    {
        m_job->fail(QWebEngineUrlRequestJob::RequestFailed);
        return -1;
    }
    qint64 writeData(const char *, qint64) Q_DECL_OVERRIDE
    {
        m_job->fail(QWebEngineUrlRequestJob::RequestFailed);
        return -1;
    }
    void close() Q_DECL_OVERRIDE
    {
        QIODevice::close();
        deleteLater();
    }

private:
    QWebEngineUrlRequestJob *m_job;
};

class FailOnReadUrlSchemeHandler : public QWebEngineUrlSchemeHandler
{
public:
    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        job->reply(QByteArrayLiteral("text/plain"), new FailingIODevice(job));
    }
};


void tst_QWebEngineProfile::urlSchemeHandlerFailRequest()
{
    FailingUrlSchemeHandler handler;
    QWebEngineProfile profile;
    profile.installUrlSchemeHandler("foo", &handler);
    QWebEngineView view;
    QSignalSpy loadFinishedSpy(&view, SIGNAL(loadFinished(bool)));
    view.setPage(new QWebEnginePage(&profile, &view));
    view.settings()->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
    view.load(QUrl(QStringLiteral("foo://bar")));
    QVERIFY(loadFinishedSpy.wait());
    QCOMPARE(toPlainTextSync(view.page()), QString());
}

void tst_QWebEngineProfile::urlSchemeHandlerFailOnRead()
{
    FailOnReadUrlSchemeHandler handler;
    QWebEngineProfile profile;
    profile.installUrlSchemeHandler("foo", &handler);
    QWebEngineView view;
    QSignalSpy loadFinishedSpy(&view, SIGNAL(loadFinished(bool)));
    view.setPage(new QWebEnginePage(&profile, &view));
    view.settings()->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
    view.load(QUrl(QStringLiteral("foo://bar")));
    QVERIFY(loadFinishedSpy.wait());
    QCOMPARE(toPlainTextSync(view.page()), QString());
}

void tst_QWebEngineProfile::urlSchemeHandlerStreaming()
{
    StreamingUrlSchemeHandler handler;
    QWebEngineProfile profile;
    profile.installUrlSchemeHandler("stream", &handler);
    QWebEngineView view;
    QSignalSpy loadFinishedSpy(&view, SIGNAL(loadFinished(bool)));
    view.setPage(new QWebEnginePage(&profile, &view));
    view.settings()->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
    view.load(QUrl(QStringLiteral("stream://whatever")));
    QVERIFY(loadFinishedSpy.wait());
    QByteArray result;
    result.append(1000, 'c');
    QCOMPARE(toPlainTextSync(view.page()), QString::fromLatin1(result));
}

void tst_QWebEngineProfile::customUserAgent()
{
    QString defaultUserAgent = QWebEngineProfile::defaultProfile()->httpUserAgent();
    QWebEnginePage page;
    QSignalSpy loadFinishedSpy(&page, SIGNAL(loadFinished(bool)));
    page.setHtml(QStringLiteral("<html><body>Hello world!</body></html>"));
    QTRY_COMPARE(loadFinishedSpy.count(), 1);

    // First test the user-agent is default
    QCOMPARE(evaluateJavaScriptSync(&page, QStringLiteral("navigator.userAgent")).toString(), defaultUserAgent);

    const QString testUserAgent = QStringLiteral("tst_QWebEngineProfile 1.0");
    QWebEngineProfile testProfile;
    testProfile.setHttpUserAgent(testUserAgent);

    // Test a new profile with custom user-agent works
    QWebEnginePage page2(&testProfile);
    QSignalSpy loadFinishedSpy2(&page2, SIGNAL(loadFinished(bool)));
    page2.setHtml(QStringLiteral("<html><body>Hello again!</body></html>"));
    QTRY_COMPARE(loadFinishedSpy2.count(), 1);
    QCOMPARE(evaluateJavaScriptSync(&page2, QStringLiteral("navigator.userAgent")).toString(), testUserAgent);
    QCOMPARE(evaluateJavaScriptSync(&page, QStringLiteral("navigator.userAgent")).toString(), defaultUserAgent);

    // Test an existing page and profile with custom user-agent works
    QWebEngineProfile::defaultProfile()->setHttpUserAgent(testUserAgent);
    QCOMPARE(evaluateJavaScriptSync(&page, QStringLiteral("navigator.userAgent")).toString(), testUserAgent);
}

void tst_QWebEngineProfile::httpAcceptLanguage()
{
    QWebEnginePage page;
    QSignalSpy loadFinishedSpy(&page, SIGNAL(loadFinished(bool)));
    page.setHtml(QStringLiteral("<html><body>Hello world!</body></html>"));
    QTRY_COMPARE(loadFinishedSpy.count(), 1);

    QStringList defaultLanguages = evaluateJavaScriptSync(&page, QStringLiteral("navigator.languages")).toStringList();

    const QString testLang = QStringLiteral("xx-YY");
    QWebEngineProfile testProfile;
    testProfile.setHttpAcceptLanguage(testLang);

    // Test a completely new profile
    QWebEnginePage page2(&testProfile);
    QSignalSpy loadFinishedSpy2(&page2, SIGNAL(loadFinished(bool)));
    page2.setHtml(QStringLiteral("<html><body>Hello again!</body></html>"));
    QTRY_COMPARE(loadFinishedSpy2.count(), 1);
    QCOMPARE(evaluateJavaScriptSync(&page2, QStringLiteral("navigator.languages")).toStringList(), QStringList(testLang));
    // Test the old one wasn't affected
    QCOMPARE(evaluateJavaScriptSync(&page, QStringLiteral("navigator.languages")).toStringList(), defaultLanguages);

    // Test changing an existing page and profile
    QWebEngineProfile::defaultProfile()->setHttpAcceptLanguage(testLang);
    QCOMPARE(evaluateJavaScriptSync(&page, QStringLiteral("navigator.languages")).toStringList(), QStringList(testLang));
}

void tst_QWebEngineProfile::downloadItem()
{
    qRegisterMetaType<QWebEngineDownloadItem *>();
    QWebEngineProfile testProfile;
    QWebEnginePage page(&testProfile);
    QSignalSpy downloadSpy(&testProfile, SIGNAL(downloadRequested(QWebEngineDownloadItem *)));
    connect(&testProfile, &QWebEngineProfile::downloadRequested, this, [=] (QWebEngineDownloadItem *item) { item->accept(); });
    page.load(QUrl::fromLocalFile(QCoreApplication::applicationFilePath()));
    QTRY_COMPARE(downloadSpy.count(), 1);
}

void tst_QWebEngineProfile::changePersistentPath()
{
    QWebEngineProfile testProfile(QStringLiteral("Test"));
    const QString oldPath = testProfile.persistentStoragePath();
    QVERIFY(oldPath.endsWith(QStringLiteral("Test")));

    // Make sure the profile has been used and the url-request-context-getter instantiated:
    QWebEnginePage page(&testProfile);
    QSignalSpy loadFinishedSpy(&page, SIGNAL(loadFinished(bool)));
    page.load(QUrl("http://qt-project.org"));
    if (!loadFinishedSpy.wait(10000) || !loadFinishedSpy.at(0).at(0).toBool())
        QSKIP("Couldn't load page from network, skipping test.");

    // Test we do not crash (QTBUG-55322):
    testProfile.setPersistentStoragePath(oldPath + QLatin1Char('2'));
    const QString newPath = testProfile.persistentStoragePath();
    QVERIFY(newPath.endsWith(QStringLiteral("Test2")));
}

QTEST_MAIN(tst_QWebEngineProfile)
#include "tst_qwebengineprofile.moc"
