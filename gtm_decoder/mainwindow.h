#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

#define UNUSED(x) ((x)=(x))

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

    void on_pushButton_3_clicked();

    void on_pushButton_4_clicked();
    void drawFrame(QRgb * frame_data, int size_x, int size_y);

private:
    Ui::MainWindow *ui;
};

class WorkerThread : public QThread
{
    Q_OBJECT
public:
    //QThread(QObject *parent = nullptr);
    void run();
    bool setFilename(QString filename);
signals:
    //void resultReady(const QString &s);
    void outputFrame(QRgb * frame_data, int size_x, int size_y);
private:
    QString filename;
    QRgb * me_frame_data;
};

#endif // MAINWINDOW_H
