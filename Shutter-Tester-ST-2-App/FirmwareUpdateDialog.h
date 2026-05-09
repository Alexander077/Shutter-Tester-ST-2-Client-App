/*
 * Copyright (C) 2026 Alexander Litvinov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <QDialog>
#include <QString>
#include <QScopedPointer>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;
QT_END_NAMESPACE

class SerialController;

class FirmwareUpdateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FirmwareUpdateDialog(SerialController *controller, QWidget *parent = nullptr);
    ~FirmwareUpdateDialog();

private slots:
    void onBrowseClicked();
    void onUpdateClicked();
    void onProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onStatus(const QString &status);
    void onFinished(bool success, const QString &message);
    void onConnectedChanged();

private:
    SerialController *m_controller;
    QLineEdit *m_filePathEdit;
    QPushButton *m_browseButton;
    QPushButton *m_updateButton;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
};
