/*
 * Copyright (C) 2014  Christian Kaiser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include "qtimgur.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QXmlStreamReader>

QtImgur::QtImgur(const QString &APIKey, QObject *parent) : QObject(parent), mAPIKey(APIKey), directUrl(false)
{
  mNetworkManager = new QNetworkAccessManager(this);

  connect(mNetworkManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(reply(QNetworkReply*)));
}

void QtImgur::cancelAll()
{
  foreach (QNetworkReply *reply, mFiles.keys()) {
    reply->abort();
  }

  mFiles.clear();
}

void QtImgur::cancel(const QString &fileName)
{
  QNetworkReply *reply = mFiles.key(fileName);

  if (!reply) {
    return;
  }

  mFiles.remove(reply);
  reply->abort();
}

void QtImgur::upload(const QString &fileName)
{
  QFile file(fileName);

  if (!file.open(QIODevice::ReadOnly)) {
    emit error(fileName, QtImgur::ErrorFile);
    return;
  }

  QByteArray data;
  data.append(QString("key=").toUtf8());
  data.append(QUrl::toPercentEncoding(mAPIKey));
  data.append(QString("&image=").toUtf8());
  data.append(QUrl::toPercentEncoding(file.readAll().toBase64()));
  file.close();

  QNetworkRequest request(QUrl("http://api.imgur.com/2/upload"));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

  QNetworkReply *reply = mNetworkManager->post(request, data);
  connect(reply, SIGNAL(uploadProgress(qint64, qint64)), this, SLOT(progress(qint64, qint64)));

  mFiles.insert(reply, fileName);
}

void QtImgur::progress(qint64 bytesSent, qint64 bytesTotal)
{
  qint64 totalSent, totalTotal;

  QNetworkReply *senderReply = qobject_cast<QNetworkReply*>(sender());

  senderReply->setProperty("bytesSent", bytesSent);
  senderReply->setProperty("bytesTotal", bytesTotal);

  totalSent = bytesSent;
  totalTotal = bytesTotal;

  foreach (QNetworkReply *reply, mFiles.keys()) {
    if (reply != senderReply) {
      totalSent  += reply->property("bytesSent").toLongLong();
      totalTotal += reply->property("bytesTotal").toLongLong();
    }
  }

  emit uploadProgress(totalSent, totalTotal);
}

void QtImgur::reply(QNetworkReply *reply)
{
  QString fileName = mFiles[reply];

  mFiles.remove(reply);

  if (reply->error() != QNetworkReply::NoError) {
    if (reply->error() == QNetworkReply::OperationCanceledError)
      emit error(fileName, QtImgur::ErrorCancel);
    else
      emit error(fileName, QtImgur::ErrorNetwork);

    reply->deleteLater();
    return;
  }

  if (reply->rawHeader("X-RateLimit-Remaining") == "0") {
    emit error(fileName, QtImgur::ErrorCredits);
    reply->deleteLater();
    return;
  }

  QXmlStreamReader reader(reply->readAll());

  QString url_option = "imgur_page";

  if (directUrl)
      url_option = "original";

  QString url;
  QString deleteHash;


  bool hasError = false;

  while (!reader.atEnd()) {
    reader.readNext();

    if (reader.isStartElement()) {
      if (reader.name() == "error") {
        hasError = true;
      }

      if (reader.name() == "deletehash") {
        deleteHash = reader.readElementText();
      }

      if (reader.name() == url_option) {
        url = reader.readElementText();
      }
    }
  }

  if (deleteHash.isEmpty() || url.isEmpty() || hasError) {
    emit error(fileName, QtImgur::ErrorUpload);
  }
  else {
    emit uploaded(fileName, url, deleteHash);
  }


  reply->deleteLater();
}
