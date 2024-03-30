#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFile>
#include <QPainter>
#include <QPicture>
#include <QMessageBox>
#include <QFileDialog>
#include <QProcess>
#include <QVector2D>

#include "LzmaLib.h"

//#define CANVAS_X 352
//#define CANVAS_Y 200
//#define CANVAS_X 704
//#define CANVAS_Y 448
//#define CANVAS_X 640
//#define CANVAS_Y 272
//#define CANVAS_X 320
//#define CANVAS_Y 224
//#define CANVAS_X 704
//#define CANVAS_Y 152
//#define FRAMES_MAX 6000
//#define TILES_MAX 121000
#define FPS 20

typedef struct Keyframe {
    int index;
    int frame_index;
    int raw_size;
    int compressed_size;
    int timecode_millisecond;
};

enum GTMCommand { // commandBits -> palette index (8 bits); V mirror (1 bit); H mirror (1 bit)
  SkipBlock = 0, // commandBits -> skip count - 1 (10 bits)
  ShortTileIdx = 1, // data -> tile index (16 bits)
  LongTileIdx = 2, // data -> tile index (32 bits)
  LoadPalette = 3, // data -> palette index (8 bits); palette format (8 bits) (00: RGBA32); RGBA bytes (32bits)
  ShortBlendTileIdx = 4, // data -> tile index (16 bits), blending (16 bits)
  LongBlendTileIdx = 5, // data -> tile index (32 bits), blending (16 bits)
    ShortAddlBlendTileIdx = 6,
    LongAddlBlendTileIdx = 7,
    ShortAdditionalTileIdx = 9,
    LongAdditionalTileIdx = 10,
  // new commands here
  FrameEnd = 28, // commandBits bit 0 -> keyframe end
  TileSet = 29, // data -> start tile (32 bits); end tile (32 bits); { indexes per tile (64 bytes) } * count; commandBits -> indexes count per palette
  SetDimensions = 30, // data -> height in tiles (16 bits); width in tiles (16 bits); frame length in nanoseconds (32 bits); tile count (32 bits);
  ExtendedCommand = 31, // data -> custom commands, proprietary extensions, ...; commandBits -> extended command index (10 bits)

  ReservedAreaBegin = 32, // reserving the MSB for future use
  ReservedAreaEnd = 63
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->lineEdit->setText(QString("E:/Saturn/TileMotion/bro_small2.gtm"));
    ui->lineEdit->setText(QString("E:/Saturn/TileMotion/TK_lo2_7_0.gtm"));
}

MainWindow::~MainWindow()
{
    delete ui;
}

uint32_t _get_int_from_bytearray(QByteArray * b, int index)
{
    uint8_t * p8 = (uint8_t *)b->data();
    uint32_t i = p8[index]+p8[index+1]*0x100+p8[index+2]*0x10000+p8[index+3]*0x1000000;
    return i;
}

uint16_t _get_short_from_bytearray(QByteArray * b, int index)
{
    uint8_t * p8 = (uint8_t *)b->data();
    int16_t i = p8[index]+p8[index+1]*0x100;
    return i;
}

void MainWindow::on_pushButton_clicked()
{
    QFile in_file(ui->lineEdit->text());
    in_file.open(QIODevice::ReadOnly);
    QByteArray _gtm_header = in_file.read(40);
    int32_t _full_header_size = _get_int_from_bytearray(&_gtm_header,2*4);
    int32_t _keyframes = _get_int_from_bytearray(&_gtm_header,6*4);
    int32_t _width = _get_int_from_bytearray(&_gtm_header,4*4);
    int32_t _height = _get_int_from_bytearray(&_gtm_header,5*4);
    int32_t _frame_count = _get_int_from_bytearray(&_gtm_header,7*4);
    int32_t _average_bps = _get_int_from_bytearray(&_gtm_header,8*4);
    QList<Keyframe> Keyframes_List;
    for (int i=0;i<_keyframes;i++)
    {
        QByteArray _keyframe_header = in_file.read(28);
        Keyframe k;
        k.index = _get_int_from_bytearray(&_keyframe_header,2*4);
        k.frame_index = _get_int_from_bytearray(&_keyframe_header,3*4);
        k.raw_size = _get_int_from_bytearray(&_keyframe_header,4*4);
        k.compressed_size = _get_int_from_bytearray(&_keyframe_header,5*4);
        k.timecode_millisecond = _get_int_from_bytearray(&_keyframe_header,6*4);
        Keyframes_List.append(k);
    }

    uint8_t c;

    //tiles and palletes will be filled as commands go in
    QVector<QByteArray> Tiles;
    QVector<QByteArray> Palettes;
    uint16_t Palette_Size = 16; //using 16-color palettes

    //unpacking the stream
    QByteArray _stream_data_packed = in_file.readAll();
    size_t size_packed = _stream_data_packed.size();

    QFile out_file_u("tmp_packed.bin");
    out_file_u.open(QIODevice::WriteOnly|QIODevice::Truncate);
    out_file_u.write(_stream_data_packed);
    out_file_u.close();

    QProcess process;
    QStringList proc_args;

    proc_args.clear();
    proc_args.append("e");
    proc_args.append("-so");
    proc_args.append("tmp_packed.bin");
    if (QFile::exists("C:\\Program Files\\7-Zip\\7z.exe"))
        process.setProgram("C:\\Program Files\\7-Zip\\7z");
    process.setArguments(proc_args);
    bool status = process.open();
    process.waitForFinished();
    QByteArray _stream_data = process.readAllStandardOutput();

    int iParseIndex = 0;
    uint16_t Command;
    uint8_t Command_Opcode;
    uint16_t Command_Param;

    bool bFrameEnd = false;
    int iCurrentTileX=0;
    int iCurrentTileY=0;
    int iCurrentTile;
    int iCurrentPalette;
    int iCurrentPaletteFormat;
    int iMirrorFlags;
    int index;
    int iTilesStart;
    int iTilesEnd;
    QColor color;
    QPainter pai;
    QPicture pic;
    QPicture pic2;
    QMessageBox msgBox;
    int iCanvasSizeX,iCanvasSizeY;
    int iFramesCount;
    int iTilesCount;
    int iLastOpcode=0;
    int iCurrentOpcodeBlock=-1;
    int iCurrentOpcodeBlockCount=0;
    int _size_x,_size_y;

    //--------------- PASS 1 - searching for X and Y sizes and calculating number of frames and tiles
    iCanvasSizeX = 0;
    iCanvasSizeY = 0;
    iFramesCount = 0;
    while ((iParseIndex < _stream_data.size()))
    {
        //read next command
        Command = _get_short_from_bytearray(&_stream_data,iParseIndex);
        Command_Opcode = Command & 0x3F;
        Command_Param = Command >> 6;
        iParseIndex+=2;
        switch (Command_Opcode)
        {
        case SkipBlock:
            break;
        case ShortTileIdx:
            iParseIndex+=2;
            break;
        case ShortBlendTileIdx:
            //dunno what to do with this, keep as normal tile for now
            iParseIndex+=2;
            iParseIndex+=2;
            break;
        case LongTileIdx:
            iParseIndex+=4;
            break;
        case LongBlendTileIdx:
            iParseIndex+=4;
            iParseIndex+=2;
            break;
        case ShortAddlBlendTileIdx:
            iParseIndex+=6;
            break;
        case LongAddlBlendTileIdx:
            iParseIndex+=8;
            break;
        case ShortAdditionalTileIdx:
            iParseIndex+=5;
            break;
        case LongAdditionalTileIdx:
            iParseIndex+=7;
            break;
        case LoadPalette:
            iParseIndex++;
            iParseIndex++;
            iParseIndex+=(Palette_Size*4);
            break;
        case FrameEnd:
            iFramesCount++;
            break;
        case TileSet:
            //loading some tiles!
            iTilesStart=_get_int_from_bytearray(&_stream_data,iParseIndex);
            iParseIndex+=4;
            iTilesEnd=_get_int_from_bytearray(&_stream_data,iParseIndex);
            iParseIndex+=4;
            for (int i=iTilesStart;i<=iTilesEnd;i++)
            {
                iParseIndex+=64;
                iTilesCount++;
            }
            break;
        case SetDimensions:
            // data -> width in tiles (16 bits); height in tiles (16 bits); frame length in nanoseconds (32 bits) (2^32-1: still frame); tile count (32 bits); commandBits -> none
            //verify that hardcoded dimensions correspond to the one specified in file
            iCanvasSizeX = 8*_get_short_from_bytearray(&_stream_data,iParseIndex);
            iParseIndex+=2;
            iCanvasSizeY = 8*_get_short_from_bytearray(&_stream_data,iParseIndex);
            iParseIndex+=2;
            iParseIndex+=8;
            break;
        case ExtendedCommand:
            //not supported?
            qDebug("ExtendedCommand at index 0x%x: %d, param %d",iParseIndex, Command_Opcode, Command_Param);
            break;
        default:
            //not supported?
            qDebug("Unknown opcode at index 0x%x: %d, param %d",iParseIndex, Command_Opcode, Command_Param);
            break;
        }
        if (iFramesCount % 500 == 0) qDebug("PASS 1 : frame %d",iFramesCount);
    }

    //--------------- PASS 1 done

    qDebug("Canvas %d x %d, Frames  %d, Tiles %d",iCanvasSizeX, iCanvasSizeY, iFramesCount, iTilesCount);

    QImage img(iCanvasSizeX,iCanvasSizeY,QImage::Format_ARGB32);
    //QRgb me_canvas[iCanvasSizeX][iCanvasSizeY]
    QVector<QVector<QRgb>> me_canvas;
    me_canvas.resize(iCanvasSizeX);
    for (int i=0;i<me_canvas.size();i++)
        me_canvas[i].resize(iCanvasSizeY);
    //uint8_t iTilesUsage[iFramesCount][iTilesCount];
    QVector<QVector<uint8_t>> iTilesUsage;
    iTilesUsage.resize(iFramesCount);
    for (int i=0;i<iTilesUsage.size();i++)
    {
        iTilesUsage[i].resize(iTilesCount);
        iTilesUsage[i].fill(0,iTilesCount);
    }
    //int Stream_bytes[iFramesCount];
    QVector<uint8_t> Stream_bytes;
    Stream_bytes.resize(iFramesCount);
    Tiles.resize(iTilesCount);

    iLastOpcode=0;
    iCurrentOpcodeBlock=-1;
    iCurrentOpcodeBlockCount=0;
    iParseIndex=0;

    //clean huge usage array
    /*for (int frame=0;frame<iFramesCount;frame++)
        for (int tile=0;tile<TILES_MAX;tile++)
            iTilesUsage[frame][tile]=0;*/

    //--------------- PASS 2 - loading palettes and tiles, getting tiles usage

    for (int frame=0;frame<iFramesCount;frame++)
    {
        Stream_bytes[frame]=0;
        while ((iParseIndex < _stream_data.size()) && (false == bFrameEnd))
        {
            //read next command
            Command = _get_short_from_bytearray(&_stream_data,iParseIndex);
            Command_Opcode = Command & 0x3F;
            Command_Param = Command >> 6;
            if ((Command_Opcode != iLastOpcode) && (Command_Opcode == LoadPalette))
            {
                iCurrentOpcodeBlock = LoadPalette;
                iCurrentOpcodeBlockCount=0;
                qDebug("LoadPalette block started at index 0x%x", iParseIndex);
            }
            if ((Command_Opcode != LoadPalette) && (iCurrentOpcodeBlock == LoadPalette))
            {
                iCurrentOpcodeBlock = -1;
                qDebug("LoadPalette block ended at index 0x%x, number of entries = %i", iParseIndex, iCurrentOpcodeBlockCount);
            }
            iCurrentOpcodeBlockCount++;
            iParseIndex+=2;
            Stream_bytes[frame]+=2;
            switch (Command_Opcode)
            {
            case SkipBlock:
                for (int i=0;i<=Command_Param;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                //out_file.write(QByteArray(1,0x01));
                //out_file.write(QByteArray(1,0x00));
                //out_file.write(QByteArray(1,Command_Param>>8));
                //out_file.write(QByteArray(1,Command_Param));
                break;
            case ShortTileIdx:
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_short_from_bytearray(&_stream_data,iParseIndex);
                if (iCurrentTile < 0)
                    qDebug("Negative tile at index 0x%x, number of entries = %i", iParseIndex, iCurrentOpcodeBlockCount);
                iTilesUsage[frame][iCurrentTile] = 1;
                iParseIndex+=2;
                Stream_bytes[frame]+=2;
                //draw the tile at X,Y
                /*for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            index = Tiles.at(iCurrentTile).at(x+y*8);
                            break;
                        case 1:
                            index = Tiles.at(iCurrentTile).at(y*8+7-x);
                            break;
                        case 2:
                            index = Tiles.at(iCurrentTile).at(56-y*8+x);
                            break;
                        case 3:
                            index = Tiles.at(iCurrentTile).at(63-y*8-x);
                            break;
                        }
                        color.setRed((uint8_t)(Palettes.at(iCurrentPalette).at(index*4)));
                        color.setGreen((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+1)));
                        color.setBlue((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                switch (iMirrorFlags)
                {
                case 0:
                    //out_file.write(QByteArray(1,0x00));
                    break;
                case 1:
                    //out_file.write(QByteArray(1,0x40));
                    break;
                case 2:
                    //out_file.write(QByteArray(1,0x80));
                    break;
                case 3:
                    //out_file.write(QByteArray(1,0xC0));
                    break;
                }*/

                c = iCurrentPalette;
                //out_file.write(QByteArray(1,c));
                c = (iCurrentTile+0x100)>>8;
                //out_file.write(QByteArray(1,c));
                c = iCurrentTile;
                //out_file.write(QByteArray(1,c));
                if (iCurrentTile!= 0)
                    c++;
                //move to next X,Y
                for (int i=0;i<=0;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                break;
            case ShortBlendTileIdx:
                qDebug("ShortBlendTileIdx at index 0x%x is unsupported", iParseIndex);
                //dunno what to do with this, keep as normal tile for now
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=2;
                Stream_bytes[frame]+=2;
                //iBlending = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=2;
                Stream_bytes[frame]+=2;
                iTilesUsage[frame][iCurrentTile] = 1;
                //draw the tile at X,Y
                /*for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            index = Tiles.at(iCurrentTile).at(x+y*8);
                            break;
                        case 1:
                            index = Tiles.at(iCurrentTile).at(y*8+7-x);
                            break;
                        case 2:
                            index = Tiles.at(iCurrentTile).at(56-y*8+x);
                            break;
                        case 3:
                            index = Tiles.at(iCurrentTile).at(63-y*8-x);
                            break;
                        }
                        color.setRed((uint8_t)(Palettes.at(iCurrentPalette).at(index*4)));
                        color.setGreen((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+1)));
                        color.setBlue((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                switch (iMirrorFlags)
                {
                case 0:
                    //out_file.write(QByteArray(1,0x00));
                    break;
                case 1:
                    //out_file.write(QByteArray(1,0x40));
                    break;
                case 2:
                    //out_file.write(QByteArray(1,0x80));
                    break;
                case 3:
                    //out_file.write(QByteArray(1,0xC0));
                    break;
                }*/

                c = iCurrentPalette;
                //out_file.write(QByteArray(1,c));
                c = (iCurrentTile+0x100)>>8;
                //out_file.write(QByteArray(1,c));
                c = iCurrentTile;
                //out_file.write(QByteArray(1,c));
                if (iCurrentTile!= 0)
                    c++;
                //move to next X,Y
                for (int i=0;i<=0;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                break;
            case LongTileIdx:
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_int_from_bytearray(&_stream_data,iParseIndex);
                iTilesUsage[frame][iCurrentTile] = 1;
                iParseIndex+=4;
                Stream_bytes[frame]+=4;
                //draw the tile at X,Y
                /*for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            index = Tiles.at(iCurrentTile).at(x+y*8);
                            break;
                        case 1:
                            index = Tiles.at(iCurrentTile).at(y*8+7-x);
                            break;
                        case 2:
                            index = Tiles.at(iCurrentTile).at(56-y*8+x);
                            break;
                        case 3:
                            index = Tiles.at(iCurrentTile).at(63-y*8-x);
                            break;
                        }
                        color.setRed((uint8_t)(Palettes.at(iCurrentPalette).at(index*4)));
                        color.setGreen((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+1)));
                        color.setBlue((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                switch (iMirrorFlags)
                {
                case 0:
                    //out_file.write(QByteArray(1,0x00));
                    break;
                case 1:
                    //out_file.write(QByteArray(1,0x40));
                    break;
                case 2:
                    //out_file.write(QByteArray(1,0x80));
                    break;
                case 3:
                    //out_file.write(QByteArray(1,0xC0));
                    break;
                }*/

                c = iCurrentPalette;
                //out_file.write(QByteArray(1,c));
                c = (iCurrentTile+0x100)>>8;
                //out_file.write(QByteArray(1,c));
                c = iCurrentTile;
                //out_file.write(QByteArray(1,c));
                if (iCurrentTile!= 0)
                    c++;
                //move to next X,Y
                for (int i=0;i<=0;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                break;
            case LongBlendTileIdx:
                qDebug("LongBlendTileIdx at index 0x%x is unsupported", iParseIndex);
                //dunno what to do with this, keep as normal tile for now
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_int_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=4;
                Stream_bytes[frame]+=4;
                //iBlending = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=2;
                Stream_bytes[frame]+=2;
                iTilesUsage[frame][iCurrentTile] = 1;
                //draw the tile at X,Y
                /*for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            index = Tiles.at(iCurrentTile).at(x+y*8);
                            break;
                        case 1:
                            index = Tiles.at(iCurrentTile).at(y*8+7-x);
                            break;
                        case 2:
                            index = Tiles.at(iCurrentTile).at(56-y*8+x);
                            break;
                        case 3:
                            index = Tiles.at(iCurrentTile).at(63-y*8-x);
                            break;
                        }
                        color.setRed((uint8_t)(Palettes.at(iCurrentPalette).at(index*4)));
                        color.setGreen((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+1)));
                        color.setBlue((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                switch (iMirrorFlags)
                {
                case 0:
                    //out_file.write(QByteArray(1,0x00));
                    break;
                case 1:
                    //out_file.write(QByteArray(1,0x40));
                    break;
                case 2:
                    //out_file.write(QByteArray(1,0x80));
                    break;
                case 3:
                    //out_file.write(QByteArray(1,0xC0));
                    break;
                }*/

                c = iCurrentPalette;
                //out_file.write(QByteArray(1,c));
                c = (iCurrentTile+0x100)>>8;
                //out_file.write(QByteArray(1,c));
                c = iCurrentTile;
                //out_file.write(QByteArray(1,c));
                if (iCurrentTile!= 0)
                    c++;
                //move to next X,Y
                for (int i=0;i<=0;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                break;
            case ShortAddlBlendTileIdx:
                qDebug("ShortAddlBlendTileIdx at index 0x%x is unsupported", iParseIndex);
                iParseIndex+=6;
                Stream_bytes[frame]+=6;
                break;
            case LongAddlBlendTileIdx:
                qDebug("LongAddlBlendTileIdx at index 0x%x is unsupported", iParseIndex);
                iParseIndex+=8;
                Stream_bytes[frame]+=8;
                break;
            case ShortAdditionalTileIdx:
                qDebug("ShortAdditionalTileIdx at index 0x%x is unsupported", iParseIndex);
                iParseIndex+=5;
                Stream_bytes[frame]+=5;
                break;
            case LongAdditionalTileIdx:
                qDebug("LongAdditionalTileIdx at index 0x%x is unsupported", iParseIndex);
                iParseIndex+=7;
                Stream_bytes[frame]+=7;
                break;
            case LoadPalette:
                //iCurrentPalette = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iCurrentPalette = _stream_data[iParseIndex];
                iParseIndex++;
                Stream_bytes[frame]++;
                iCurrentPaletteFormat = _stream_data[iParseIndex];
                iParseIndex++;
                Stream_bytes[frame]++;
                if (iCurrentPalette > 127)
                {
                    qDebug("Wrong palette number at index 0x%x : %d", iParseIndex, iCurrentPalette);
                    iCurrentPalette = 127;
                }
                if (iCurrentPaletteFormat != 0)
                {
                    qDebug("Wrong palette format at index 0x%x : %d", iParseIndex, iCurrentPaletteFormat);
                }
                while (Palettes.size() <= iCurrentPalette)
                    Palettes.append(QByteArray());
                Palettes[iCurrentPalette].clear();
                Palettes[iCurrentPalette].append(_stream_data.mid(iParseIndex,Palette_Size*4));
                iParseIndex+=(Palette_Size*4);
                Stream_bytes[frame]+=(Palette_Size*4);
                //out_file.write(QByteArray(1,0x03));
                //out_file.write(QByteArray(1,0x00));
                //out_file.write(QByteArray(1,0x00));
                //out_file.write(QByteArray(1,iCurrentPalette));
                for (int i=0;i<16;i++)
                {
                    c = 0x00 | (((Palettes[iCurrentPalette][i*4+2]) >> 3) << 2)  | ((Palettes[iCurrentPalette][i*4+1] >> 3) >> 3);
                    //out_file.write(QByteArray(1,c));
                    c = (((Palettes[iCurrentPalette][i*4+1]) >> 3) << 5)  | ((Palettes[iCurrentPalette][i*4] >> 3));
                    //out_file.write(QByteArray(1,c));
                }
                break;
            case FrameEnd:
                bFrameEnd = true;
                //out_file.write(QByteArray(1,0x02));
                //out_file.write(QByteArray(1,0x00));
                //out_file.write(QByteArray(1,0x00));
                //out_file.write(QByteArray(1,0x00));
                break;
            case TileSet:
                //loading some tiles!
                iTilesStart=_get_int_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=4;
                Stream_bytes[frame]+=4;
                iTilesEnd=_get_int_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=4;
                Stream_bytes[frame]+=4;
                qDebug("TileSet block, setting tiles from %i to %i", iTilesStart, iTilesEnd);
                for (int i=iTilesStart;i<=iTilesEnd;i++)
                {
                    while (Tiles.size() <= i)
                    {
                        Tiles.append(QByteArray(64,0));
                    }
                    Tiles[i].clear();
                    for (int i=0;i<32;i++)
                        Tiles[i].append( (_stream_data[iParseIndex+i*2]<<4) | (_stream_data[iParseIndex+i*2+1]) );
                    //Tiles[i].append(_stream_data.mid(iParseIndex,64));
                    iParseIndex+=64;
                    //Stream_bytes[frame]+=64; //tiles are not counted in stream
                }
                break;
            case SetDimensions:
                // data -> width in tiles (16 bits); height in tiles (16 bits); frame length in nanoseconds (32 bits) (2^32-1: still frame); tile count (32 bits); commandBits -> none
                //verify that hardcoded dimensions correspond to the one specified in file
                _size_x = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=2;
                Stream_bytes[frame]+=2;
                if ((iCanvasSizeX/8) != _size_x)
                {
                    qDebug("Wrong X size at index 0x%x: specified %d, detected %d", iParseIndex, _size_x, iCanvasSizeX/8);
                    return;
                }
                _size_y = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=2;
                Stream_bytes[frame]+=2;
                if ((iCanvasSizeY/8) != _size_y)
                {
                    qDebug("Wrong Y size at index 0x%x: specified %d, detected %d", iParseIndex, _size_y, iCanvasSizeY/8);
                    return;
                }
                iParseIndex+=8;
                Stream_bytes[frame]+=8;
                break;
            case ExtendedCommand:
                //not supported?
                qDebug("ExtendedCommand at index 0x%x: %d, param %d",iParseIndex, Command_Opcode, Command_Param);
                break;
            default:
                //not supported?
                qDebug("Unknown opcode at index 0x%x: %d, param %d",iParseIndex, Command_Opcode, Command_Param);
                break;
            }
            iLastOpcode = Command_Opcode;
        }

        //go to next frame
        bFrameEnd = false;
        if (frame % 500 == 0) qDebug("PASS 2 : frame %d",frame);
    }

    qDebug("Tiles loading done");

    //--------------- PASS 2 done

    //optimizing usage by removing blinking on/off tiles
#define GLUE_SIZE 25
    for (int tile=0;tile<iTilesCount;tile++)
    {
        for (int frame=0;frame<iFramesCount-GLUE_SIZE-1;frame++)
        {
            if ( (iTilesUsage[frame][tile]==1) && (iTilesUsage[frame+1][tile]==0)  )
            {
                for (int i=2; i< (GLUE_SIZE+2);i++)
                {
                    if (iTilesUsage[frame+i][tile]==1)
                         iTilesUsage[frame+1][tile]=1;
                }
            }
        }
        if (tile % 50000 == 0) qDebug("GLUEING : tile %d",tile);
    }
    qDebug("Glueing done");


    //--------------- PASS 3 - saving data

    QFile out_file("output.gts");
    out_file.open(QIODevice::WriteOnly|QIODevice::Truncate);
    //saving header
    out_file.write(QByteArray(1,iCanvasSizeX>>24));
    out_file.write(QByteArray(1,iCanvasSizeX>>16));
    out_file.write(QByteArray(1,iCanvasSizeX>>8));
    out_file.write(QByteArray(1,iCanvasSizeX>>0));
    out_file.write(QByteArray(1,iCanvasSizeY>>24));
    out_file.write(QByteArray(1,iCanvasSizeY>>16));
    out_file.write(QByteArray(1,iCanvasSizeY>>8));
    out_file.write(QByteArray(1,iCanvasSizeY>>0));
    out_file.write(QByteArray(1,iFramesCount>>24));
    out_file.write(QByteArray(1,iFramesCount>>16));
    out_file.write(QByteArray(1,iFramesCount>>8));
    out_file.write(QByteArray(1,iFramesCount>>0));
    while(out_file.size() < 2048)
        out_file.write(QByteArray(1,0));

    //saving chunks
    QByteArray chunk_header_tiles;
    QByteArray chunk_data_tiles;
    QByteArray chunk_header_commands;
    QByteArray chunk_data_commands;
    QByteArray chunk_header_palettes;
    QByteArray chunk_data_palettes;
    //VRAM is 512 kB, tilemap is 64*128*4 = 32 kB, remaining 480k = 15360 tiles
    QVector<int> TilesVRAMUsage;
    TilesVRAMUsage.fill(0,15360);
    QByteArray header_magic_tiles;
    QByteArray header_magic_palettes;
    QByteArray header_magic_commands;
    header_magic_tiles.append(QByteArray(1,0xDE));
    header_magic_tiles.append(QByteArray(1,0xAF));
    header_magic_tiles.append(QByteArray(1,0xFA));
    header_magic_tiles.append(QByteArray(1,0xCE));
    header_magic_palettes.append(QByteArray(1,0xBE));
    header_magic_palettes.append(QByteArray(1,0xEF));
    header_magic_palettes.append(QByteArray(1,0xDE));
    header_magic_palettes.append(QByteArray(1,0xAD));
    header_magic_commands.append(QByteArray(1,0xCA));
    header_magic_commands.append(QByteArray(1,0xCA));
    header_magic_commands.append(QByteArray(1,0xDE));
    header_magic_commands.append(QByteArray(1,0xFE));

    //tiles format : 128 bytes header (4 magic, 120 data, 2 dummy), 2048-128 = 60*32 bytes data
    //palettes format : 4 bytes header (4 magic), 2048-4 compressed data
    //commands format : 4 bytes header (4 magic), 2048-4 command data

    iLastOpcode=0;
    iCurrentOpcodeBlock=-1;
    iCurrentOpcodeBlockCount=0;
    iParseIndex=0;

    for (int frame=0;frame<iFramesCount;frame++)
    {
        //dropping tiles that are not used anymore
        if (frame > 0)
        {
            for (int tile=0;tile<iTilesCount;tile++)
            {
                if ( (iTilesUsage[frame][tile]==0) && (iTilesUsage[frame-1][tile]==1) )
                    TilesVRAMUsage[TilesVRAMUsage.indexOf(tile)] = 0;
            }
        }
        chunk_header_tiles.append(header_magic_tiles);
        if (frame == 0)
        {
            //first frame
            for (int tile=0;tile<iTilesCount;tile++)
            {
                if (iTilesUsage[frame][tile]==1)
                {
                    int location = TilesVRAMUsage.indexOf(0);
                    TilesVRAMUsage[location] = tile;
                    chunk_header_tiles.append(QByteArray(1,tile>>8));
                    chunk_header_tiles.append(QByteArray(1,tile));
                    chunk_data_tiles.append(Tiles[tile]);
                    if (chunk_data_tiles.size() >= 2048-128)
                    {
                        //enough datafor a sector, copying to sector
                        chunk_header_tiles.append(QByteArray(128-chunk_header_tiles.size(),0));
                        //chunk_data.append(QByteArray(2048-128-chunk_data.size(),0));
                        assert(chunk_header_tiles.size()+chunk_data_tiles.size() == 2048);
                        out_file.write(chunk_header_tiles);
                        out_file.write(chunk_data_tiles);
                        chunk_header_tiles.clear();
                        chunk_data_tiles.clear();
                    }
                }
            }
        }
        else
        {
            //non-first frame
            for (int tile=0;tile<iTilesCount;tile++)
            {
                if ( (iTilesUsage[frame][tile]==1) && (iTilesUsage[frame-1][tile]==0) )
                {
                    int location = TilesVRAMUsage.indexOf(0);
                    TilesVRAMUsage[location] = tile;
                    chunk_header_tiles.append(QByteArray(1,0));
                    chunk_header_tiles.append(QByteArray(1,1));//1 = tile data
                    chunk_header_tiles.append(QByteArray(1,tile>>8));
                    chunk_header_tiles.append(QByteArray(1,tile));
                    chunk_data_tiles.append(Tiles[tile]);
                    if (chunk_data_tiles.size() >= 2048-128)
                    {
                        //enough datafor a sector, copying to sector
                        chunk_header_tiles.append(QByteArray(128-chunk_header_tiles.size(),0));
                        //chunk_data.append(QByteArray(2048-128-chunk_data.size(),0));
                        assert(chunk_header_tiles.size()+chunk_data_tiles.size() == 2048);
                        out_file.write(chunk_header_tiles);
                        out_file.write(chunk_data_tiles);
                        chunk_header_tiles.clear();
                        chunk_data_tiles.clear();
                    }
                }
            }
        }
        //finalizing tile data sector
        if (chunk_header_tiles.size())
        {
            chunk_header_tiles.append(QByteArray(128-chunk_header_tiles.size(),0));
            chunk_data_tiles.append(QByteArray(2048-128-chunk_data_tiles.size(),0));
            out_file.write(chunk_header_tiles);
            out_file.write(chunk_data_tiles);
            chunk_header_tiles.clear();
            chunk_data_tiles.clear();
        }

        chunk_header_commands.clear();
        chunk_data_commands.clear();

        chunk_header_commands.append(header_magic_commands);

        //now saving the stream data for the frame, it's either tiles or palettes
        while ((iParseIndex < _stream_data.size()) && (false == bFrameEnd))
        {
            //read next command
            Command = _get_short_from_bytearray(&_stream_data,iParseIndex);
            Command_Opcode = Command & 0x3F;
            Command_Param = Command >> 6;
            if ((Command_Opcode != iLastOpcode) && (Command_Opcode == LoadPalette))
            {
                iCurrentOpcodeBlock = LoadPalette;
                iCurrentOpcodeBlockCount=0;
                //qDebug("LoadPalette block started at index 0x%x", iParseIndex);
            }
            if ((Command_Opcode != LoadPalette) && (iCurrentOpcodeBlock == LoadPalette))
            {
                iCurrentOpcodeBlock = -1;
                //qDebug("LoadPalette block ended at index 0x%x, number of entries = %i", iParseIndex, iCurrentOpcodeBlockCount);
            }
            iCurrentOpcodeBlockCount++;
            iParseIndex+=2;
            switch (Command_Opcode)
            {
            case SkipBlock:
                for (int i=0;i<=Command_Param;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                out_file.write(QByteArray(1,0x01));
                out_file.write(QByteArray(1,0x00));
                out_file.write(QByteArray(1,Command_Param>>8));
                out_file.write(QByteArray(1,Command_Param));
                break;
            case ShortTileIdx:
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_short_from_bytearray(&_stream_data,iParseIndex);
                if (iCurrentTile < 0)
                    qDebug("Negative tile at index 0x%x, number of entries = %i", iParseIndex, iCurrentOpcodeBlockCount);
                iTilesUsage[frame][iCurrentTile] = 1;
                iParseIndex+=2;
                //draw the tile at X,Y
                for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            index = Tiles.at(iCurrentTile).at(x+y*8);
                            break;
                        case 1:
                            index = Tiles.at(iCurrentTile).at(y*8+7-x);
                            break;
                        case 2:
                            index = Tiles.at(iCurrentTile).at(56-y*8+x);
                            break;
                        case 3:
                            index = Tiles.at(iCurrentTile).at(63-y*8-x);
                            break;
                        }
                        color.setRed((uint8_t)(Palettes.at(iCurrentPalette).at(index*4)));
                        color.setGreen((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+1)));
                        color.setBlue((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                switch (iMirrorFlags)
                {
                case 0:
                    out_file.write(QByteArray(1,0x00));
                    break;
                case 1:
                    out_file.write(QByteArray(1,0x40));
                    break;
                case 2:
                    out_file.write(QByteArray(1,0x80));
                    break;
                case 3:
                    out_file.write(QByteArray(1,0xC0));
                    break;
                }

                //c = iMirrorFlags<<6;
                //out_file.write(QByteArray(1,c));
                c = iCurrentPalette;
                out_file.write(QByteArray(1,c));
                c = (iCurrentTile+0x100)>>8;
                out_file.write(QByteArray(1,c));
                c = iCurrentTile;
                out_file.write(QByteArray(1,c));
                if (iCurrentTile!= 0)
                    c++;
                //move to next X,Y
                for (int i=0;i<=0;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                break;
            case ShortBlendTileIdx:
                qDebug("ShortBlendTileIdx at index 0x%x is unsupported", iParseIndex);
                //dunno what to do with this, keep as normal tile for now
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=2;
                //iBlending = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=2;
                iTilesUsage[frame][iCurrentTile] = 1;
                //draw the tile at X,Y
                for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            index = Tiles.at(iCurrentTile).at(x+y*8);
                            break;
                        case 1:
                            index = Tiles.at(iCurrentTile).at(y*8+7-x);
                            break;
                        case 2:
                            index = Tiles.at(iCurrentTile).at(56-y*8+x);
                            break;
                        case 3:
                            index = Tiles.at(iCurrentTile).at(63-y*8-x);
                            break;
                        }
                        color.setRed((uint8_t)(Palettes.at(iCurrentPalette).at(index*4)));
                        color.setGreen((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+1)));
                        color.setBlue((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                switch (iMirrorFlags)
                {
                case 0:
                    out_file.write(QByteArray(1,0x00));
                    break;
                case 1:
                    out_file.write(QByteArray(1,0x40));
                    break;
                case 2:
                    out_file.write(QByteArray(1,0x80));
                    break;
                case 3:
                    out_file.write(QByteArray(1,0xC0));
                    break;
                }

                c = iCurrentPalette;
                out_file.write(QByteArray(1,c));
                c = (iCurrentTile+0x100)>>8;
                out_file.write(QByteArray(1,c));
                c = iCurrentTile;
                out_file.write(QByteArray(1,c));
                if (iCurrentTile!= 0)
                    c++;
                //move to next X,Y
                for (int i=0;i<=0;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                break;
            case LongTileIdx:
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_int_from_bytearray(&_stream_data,iParseIndex);
                iTilesUsage[frame][iCurrentTile] = 1;
                iParseIndex+=4;
                //draw the tile at X,Y
                for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            index = Tiles.at(iCurrentTile).at(x+y*8);
                            break;
                        case 1:
                            index = Tiles.at(iCurrentTile).at(y*8+7-x);
                            break;
                        case 2:
                            index = Tiles.at(iCurrentTile).at(56-y*8+x);
                            break;
                        case 3:
                            index = Tiles.at(iCurrentTile).at(63-y*8-x);
                            break;
                        }
                        color.setRed((uint8_t)(Palettes.at(iCurrentPalette).at(index*4)));
                        color.setGreen((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+1)));
                        color.setBlue((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                switch (iMirrorFlags)
                {
                case 0:
                    out_file.write(QByteArray(1,0x00));
                    break;
                case 1:
                    out_file.write(QByteArray(1,0x40));
                    break;
                case 2:
                    out_file.write(QByteArray(1,0x80));
                    break;
                case 3:
                    out_file.write(QByteArray(1,0xC0));
                    break;
                }

                c = iCurrentPalette;
                out_file.write(QByteArray(1,c));
                c = (iCurrentTile+0x100)>>8;
                out_file.write(QByteArray(1,c));
                c = iCurrentTile;
                out_file.write(QByteArray(1,c));
                if (iCurrentTile!= 0)
                    c++;
                //move to next X,Y
                for (int i=0;i<=0;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                break;
            case LongBlendTileIdx:
                qDebug("LongBlendTileIdx at index 0x%x is unsupported", iParseIndex);
                //dunno what to do with this, keep as normal tile for now
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_int_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=4;
                //iBlending = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=2;
                iTilesUsage[frame][iCurrentTile] = 1;
                //draw the tile at X,Y
                for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            index = Tiles.at(iCurrentTile).at(x+y*8);
                            break;
                        case 1:
                            index = Tiles.at(iCurrentTile).at(y*8+7-x);
                            break;
                        case 2:
                            index = Tiles.at(iCurrentTile).at(56-y*8+x);
                            break;
                        case 3:
                            index = Tiles.at(iCurrentTile).at(63-y*8-x);
                            break;
                        }
                        color.setRed((uint8_t)(Palettes.at(iCurrentPalette).at(index*4)));
                        color.setGreen((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+1)));
                        color.setBlue((uint8_t)(Palettes.at(iCurrentPalette).at(index*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                switch (iMirrorFlags)
                {
                case 0:
                    out_file.write(QByteArray(1,0x00));
                    break;
                case 1:
                    out_file.write(QByteArray(1,0x40));
                    break;
                case 2:
                    out_file.write(QByteArray(1,0x80));
                    break;
                case 3:
                    out_file.write(QByteArray(1,0xC0));
                    break;
                }

                c = iCurrentPalette;
                out_file.write(QByteArray(1,c));
                c = (iCurrentTile+0x100)>>8;
                out_file.write(QByteArray(1,c));
                c = iCurrentTile;
                out_file.write(QByteArray(1,c));
                if (iCurrentTile!= 0)
                    c++;
                //move to next X,Y
                for (int i=0;i<=0;i++)
                {
                    iCurrentTileX++;
                    if (iCurrentTileX >= iCanvasSizeX/8)
                    {
                        iCurrentTileX = 0;
                        iCurrentTileY++;
                        if (iCurrentTileY >= iCanvasSizeY/8)
                            iCurrentTileY = 0;
                    }
                }
                break;
            case ShortAddlBlendTileIdx:
                qDebug("ShortAddlBlendTileIdx at index 0x%x is unsupported", iParseIndex);
                iParseIndex+=6;
                break;
            case LongAddlBlendTileIdx:
                qDebug("LongAddlBlendTileIdx at index 0x%x is unsupported", iParseIndex);
                iParseIndex+=8;
                break;
            case ShortAdditionalTileIdx:
                qDebug("ShortAdditionalTileIdx at index 0x%x is unsupported", iParseIndex);
                iParseIndex+=5;
                break;
            case LongAdditionalTileIdx:
                qDebug("LongAdditionalTileIdx at index 0x%x is unsupported", iParseIndex);
                iParseIndex+=7;
                break;
            case LoadPalette:
                //iCurrentPalette = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iCurrentPalette = _stream_data[iParseIndex];
                iParseIndex++;
                iCurrentPaletteFormat = _stream_data[iParseIndex];
                iParseIndex++;
                if (iCurrentPalette > 127)
                {
                    qDebug("Wrong palette number at index 0x%x : %d", iParseIndex, iCurrentPalette);
                    iCurrentPalette = 127;
                }
                if (iCurrentPaletteFormat != 0)
                {
                    qDebug("Wrong palette format at index 0x%x : %d", iParseIndex, iCurrentPaletteFormat);
                }
                while (Palettes.size() <= iCurrentPalette)
                    Palettes.append(QByteArray());
                Palettes[iCurrentPalette].clear();
                Palettes[iCurrentPalette].append(_stream_data.mid(iParseIndex,Palette_Size*4));
                iParseIndex+=(Palette_Size*4);
                out_file.write(QByteArray(1,0x03));
                out_file.write(QByteArray(1,0x00));
                out_file.write(QByteArray(1,0x00));
                out_file.write(QByteArray(1,iCurrentPalette));
                for (int i=0;i<16;i++)
                {
                    c = 0x00 | (((Palettes[iCurrentPalette][i*4+2]) >> 3) << 2)  | ((Palettes[iCurrentPalette][i*4+1] >> 3) >> 3);
                    out_file.write(QByteArray(1,c));
                    c = (((Palettes[iCurrentPalette][i*4+1]) >> 3) << 5)  | ((Palettes[iCurrentPalette][i*4] >> 3));
                    out_file.write(QByteArray(1,c));
                }
                break;
            case FrameEnd:
                bFrameEnd = true;
                out_file.write(QByteArray(1,0x02));
                out_file.write(QByteArray(1,0x00));
                out_file.write(QByteArray(1,0x00));
                out_file.write(QByteArray(1,0x00));
                break;
            case TileSet:
                //loading some tiles!
                iTilesStart=_get_int_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=4;
                iTilesEnd=_get_int_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=4;
                for (int i=iTilesStart;i<=iTilesEnd;i++)
                    iParseIndex+=64;
                break;
            case SetDimensions:
                // data -> width in tiles (16 bits); height in tiles (16 bits); frame length in nanoseconds (32 bits) (2^32-1: still frame); tile count (32 bits); commandBits -> none
                iParseIndex+=12;
                break;
            case ExtendedCommand:
                //not supported?
                qDebug("ExtendedCommand at index 0x%x: %d, param %d",iParseIndex, Command_Opcode, Command_Param);
                break;
            default:
                //not supported?
                qDebug("Unknown opcode at index 0x%x: %d, param %d",iParseIndex, Command_Opcode, Command_Param);
                break;
            }
            iLastOpcode = Command_Opcode;
        }

        //go to next frame
        bFrameEnd = false;
    }

    out_file.close();

    //--------------- PASS 3 done

    //dumping tiles usage report
    QFile out_file_t("tiles_usage.txt");
    out_file_t.open(QIODevice::WriteOnly|QIODevice::Truncate);
    int Loads[iFramesCount];
    int Loads_per_sec[iFramesCount];
    for (int frame=0;frame<(iFramesCount-1);frame++)
    {
        //calculating tile count in-frame
        int Usage = 0;
        for (int tile=0;tile<iTilesCount;tile++)
            if (1==iTilesUsage[frame][tile])
                Usage++;
        if (Usage > 7900)
        {
            out_file_t.write(QString("Tiles count overflow at frame %1, count %2\r\n").arg(frame).arg(Usage).toLatin1());
        }
        //calculating tiles to load for this frame
        Loads[frame] = 0;
        if (0==frame)
                Loads[frame] = Usage;
        else
        {
            for (int tile=0;tile<iTilesCount;tile++)
                if ( (1==iTilesUsage[frame][tile]) && (0==iTilesUsage[frame-1][tile]))
                    Loads[frame]++;
        }
        //calculating load amount per last second
        if (frame<(FPS-1))
        {
            Loads_per_sec[frame]=0;
        }
        else
        {
            Loads_per_sec[frame]=0;
            for (int i=0;i<FPS;i++)
            {
                Loads_per_sec[frame]+=Loads[frame-i]*32;
                Loads_per_sec[frame]+=Stream_bytes[frame-i];
            }
            if (Loads_per_sec[frame] > 250000)
            {
                out_file_t.write(QString("Tiles load amount overflow at frame %1, amount %2\r\n").arg(frame).arg(Loads_per_sec[frame]).toLatin1());
            }
        }
        //calculating tiles to drop for this frame
        int Drop = 0;
        for (int tile=0;tile<iTilesCount;tile++)
            if ( (1==iTilesUsage[frame][tile]) && (0==iTilesUsage[frame+1][tile]))
                Drop++;

        out_file_t.write(QString("Frame %1 : tiles %2 load %3 drop %4, stream %5 load per second %6 bytes\r\n").arg(frame).arg(Usage).arg(Loads[frame]).arg(Drop).arg(Stream_bytes[frame]).arg(Loads_per_sec[frame]).toLatin1());
    }
    out_file_t.close();

    for (int x=0;x<iCanvasSizeX;x++)
    {
        for (int y=0;y<iCanvasSizeY;y++)
        {
            img.setPixel(x,y,me_canvas[x][y]);
        }
    }

    ui->label->setPixmap(QPixmap::fromImage(img));

    in_file.close();

    qDebug("Processing complete");
}


void MainWindow::on_pushButton_2_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open Video"), "", tr("Tilemotion Files (*.gtm)"));
    if (fileName.size())
        ui->lineEdit->setText(fileName);
}

