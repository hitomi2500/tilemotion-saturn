#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

struct Block_Type{
    int chunk;
    int block;
    int type;
} ;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void ProcessChunk(QString filename);

private slots:
    void on_pushButton_clicked();

private:
    Ui::MainWindow *ui;
    QList<QByteArray> Tile_Streams;
    QList<QByteArray> Command_Streams;
    QList<Block_Type> Blocks_Streams;
};

#endif // MAINWINDOW_H
