#pragma once

#include <QDialog>

// Modal "About" dialog: the app icon beside the name, version, build date,
// copyright, GitHub link, and a Check for Updates button (compares the newest
// GitHub release tag against APP_VERSION), with a single OK button below.
// Reached from the toolbar's About action (see MainWindow).
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
