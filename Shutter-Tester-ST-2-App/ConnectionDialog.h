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
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include "SerialController.h"

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(SerialController *controller, bool isStartup = true, QWidget *parent = nullptr);

private slots:
    void onConnectClicked();
    void onSkipCancelClicked();
    void onAvailablePortsChanged();
    void onIsConnectedChanged();
    void onEchoResponseReceived(bool success, const QString &deviceName, const QString &hwVersion, const QString &swVersion);
    void onErrorOccurred(const QString &errorString);

private:
    SerialController *m_controller;
    QComboBox *m_portComboBox;
    QPushButton *m_connectButton;
    QPushButton *m_skipCancelButton;
    bool m_isStartup;
    QString m_pendingPortName;
};
