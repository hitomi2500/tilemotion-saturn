#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFile>
#include <QPainter>
#include <QPicture>
#include <QMessageBox>
#include <QFileDialog>
#include <QProcess>
#include <QVector2D>
#include <QThread>

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

struct Keyframe {
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
    //ui->lineEdit->setText(QString("E:/Saturn/TileMotion/bro_small2.gtm"));
    ui->lineEdit->setText(QString("E:/Saturn/TileMotion/yui2.gtm"));
    //ui->lineEdit->setText(QString("E:/Saturn/TileMotion/yanzi.gtm"));
    //ui->lineEdit->setText(QString("E:/Saturn/TileMotion/TK_lo2_7_0.gtm"));

    ui->lineEdit_2->setText(QString("E:/Saturn/TileMotion/build-gtm_decoder-Desktop_Qt_5_15_2_MinGW_64_bit-Debug/output.gts"));
    ui->spinBox_flaamelimit->setValue(2);
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
    UNUSED(_full_header_size);
    int32_t _keyframes = _get_int_from_bytearray(&_gtm_header,6*4);
    int32_t _width = _get_int_from_bytearray(&_gtm_header,4*4);
    UNUSED(_width);
    int32_t _height = _get_int_from_bytearray(&_gtm_header,5*4);
    UNUSED(_height);
    int32_t _frame_count = _get_int_from_bytearray(&_gtm_header,7*4);
    UNUSED(_frame_count);
    int32_t _average_bps = _get_int_from_bytearray(&_gtm_header,8*4);
    UNUSED(_average_bps);
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

    //tiles and palletes will be filled as commands go in
    QVector<QByteArray> Tiles;
    QVector<QByteArray> CurrentPalettes;
    CurrentPalettes.resize(128);
    uint16_t Palette_Size = 16; //using 16-color palettes

    //unpacking the stream
    QByteArray _stream_data_packed = in_file.readAll();
    //size_t size_packed = _stream_data_packed.size();

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
    process.open();
    process.waitForFinished();
    QByteArray _stream_data = process.readAllStandardOutput();

    QFile out_file_u2("tmp_unpacked.bin");
    out_file_u2.open(QIODevice::WriteOnly|QIODevice::Truncate);
    out_file_u2.write(_stream_data);
    out_file_u2.close();

    int iParseIndex = 0;
    uint16_t Command;
    uint8_t Command_Opcode;
    uint16_t Command_Param;

    bool bFrameEnd = false;
    int iCurrentTile;
    int iCurrentPalette;
    int iCurrentPaletteFormat;
    int iMirrorFlags;
    uint8_t uindex8;
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
    int _size_x,_size_y;

    //block size is 256 bytes
    //tiles block : 32 bytes header (4 magic, 14 indexes, 14 dummy), 256-32 = 224 = 7*32 bytes data
    //palettes format : 4 bytes header (4 magic), 238 = 7*34 palette data, 14 unused
    //commands format : 4 bytes header (4 magic), 252 command data
#define BLOCK_SIZE 256
#define BLOCK_TILE_HEADER 32
#define BLOCK_CMDS_HEADER 4
#define BLOCK_CMDS_HEADER 4

    //overriding frame count
    int iFrameLimit = ui->spinBox_flaamelimit->value();

    //--------------- PASS 1 - searching for X and Y sizes and calculating number of frames and tiles
    iCanvasSizeX = 0;
    iCanvasSizeY = 0;
    iFramesCount = 0;
    iTilesCount = 0;
    while ((iParseIndex < _stream_data.size()) && (iFramesCount < iFrameLimit))
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
            iParseIndex+=4;
            break;
        case LongTileIdx:
            iParseIndex+=4;
            break;
        case LongBlendTileIdx:
            iParseIndex+=6;
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
            iParseIndex+=2;
            iParseIndex+=(Palette_Size*4);
            break;
        case FrameEnd:
            if (iFramesCount % 500 == 0) qDebug("PASS 1 : frame %d",iFramesCount);
            iFramesCount++;
            break;
        case TileSet:
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
            qDebug("ExtendedCommand at index 0x%x: %d, param %d is unsupported",iParseIndex, Command_Opcode, Command_Param);
            break;
        default:
            qDebug("Unknown opcode at index 0x%x: %d, param %d is unsupported",iParseIndex, Command_Opcode, Command_Param);
            break;
        }
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
    QVector<int> Stream_bytes;
    Stream_bytes.resize(iFramesCount);
    Tiles.clear();
    Tiles.resize(iTilesCount);

    QVector<int> iScreenUsage;
    iScreenUsage.resize(iCanvasSizeX*iCanvasSizeY/64);
    iScreenUsage.fill(-1,iCanvasSizeX*iCanvasSizeY/64);

    iLastOpcode=0;
    iCurrentOpcodeBlock=-1;
    int iCurrentOpcodeBlockCount=0;
    iParseIndex=0;

    int iCurrentTileX=0;
    int iCurrentTileY=0;
    int bScreenUnused;

    //--------------- PASS 2 - loading tiles, getting tiles usage

    int iPaletteBlocks = 0;
    for (int frame=0;frame<iFramesCount;frame++)
    {
        Stream_bytes[frame]=0;
        //for every tile still on screen updating the usage
        for (int i=0;i<iScreenUsage.size();i++)
        {
            if (iScreenUsage[i] != -1)
            {
                iTilesUsage[frame][iScreenUsage[i]] = 1;
            }
        }
        //moving on with stream parsing
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
                iPaletteBlocks++;
                //qDebug("LoadPalette block started at index 0x%x", iParseIndex);
            }
            if ((Command_Opcode != LoadPalette) && (iCurrentOpcodeBlock == LoadPalette))
            {
                iCurrentOpcodeBlock = -1;
                if (128 != iCurrentOpcodeBlockCount)
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
                break;
            case ShortTileIdx:
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iTilesUsage[frame][iCurrentTile] = 1;
                iScreenUsage[iCurrentTileY*iCanvasSizeX/8+iCurrentTileX] = iCurrentTile;
                iParseIndex+=2;
                Stream_bytes[frame]+=2;
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
                iParseIndex+=4;
                Stream_bytes[frame]+=4;
                break;
            case LongTileIdx:
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_int_from_bytearray(&_stream_data,iParseIndex);
                iTilesUsage[frame][iCurrentTile] = 1;
                iScreenUsage[iCurrentTileY*iCanvasSizeX/8+iCurrentTileX] = iCurrentTile;
                iParseIndex+=4;
                Stream_bytes[frame]+=4;
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
                iParseIndex+=6;
                Stream_bytes[frame]+=6;
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
                iParseIndex+=(Palette_Size*4);
                Stream_bytes[frame]+=(Palette_Size*4);
                //after palette update marking all screen as unused
                for (int i=0;i<iScreenUsage.size();i++)
                    iScreenUsage[i] = -1;
                break;
            case FrameEnd:
                bFrameEnd = true;
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
                    Tiles[i].clear();
                    for (int j=0;j<32;j++)
                        Tiles[i].append( (_stream_data[iParseIndex+j*2]<<4) | (_stream_data[iParseIndex+j*2+1]) );
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
        //before going to next frame verifying that all screen is used
        bScreenUnused = 0;
        for (int i=0;i<iScreenUsage.size();i++)
            if (-1 == iScreenUsage[i])
                bScreenUnused = 1;
        if (bScreenUnused) qDebug("PASS 2 : frame %d have unused tiles",frame);

        //go to next frame
        bFrameEnd = false;
        if (frame % 500 == 0) qDebug("PASS 2 : frame %d",frame);
    }

    qDebug("Tiles loading done");

    //--------------- PASS 2 done

    //optimizing usage by removing blinking on/off tiles
#define GLUE_SIZE 25
    for (int frame=0;frame<iFramesCount-GLUE_SIZE-1;frame++)
    {
        for (int tile=0;tile<iTilesCount;tile++)
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
        if (frame % 500 == 0) qDebug("GLUEING : frame %d",frame);
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
    while(out_file.size() < BLOCK_SIZE)
        out_file.write(QByteArray(1,0));

    //saving chunks
    QByteArray chunk_tiles;
    QByteArray chunk_commands;
    QByteArray chunk_palettes;
    //VRAM is 512 kB, tilemap is 64*128*4 = 32 kB, remaining 480k = 15360 tiles
    QVector<int> TilesVRAMUsage;
    TilesVRAMUsage.fill(-1,15360);

    iParseIndex=0;
    uint8_t c;
    int iCurrentTileIndexUpdated;

    for (int frame=0;frame<iFramesCount;frame++)
    {
        chunk_tiles.clear();
        if (frame == 0)
        {
            //first frame
            for (int tile=0;tile<iTilesCount;tile++)
            {
                if (iTilesUsage[frame][tile]==1)
                {
                    assert (TilesVRAMUsage.contains(-1));
                    int location = TilesVRAMUsage.indexOf(-1);
                    TilesVRAMUsage[location] = tile;
                    chunk_tiles.append(QByteArray(1,location>>8));
                    chunk_tiles.append(QByteArray(1,location));
                    chunk_tiles.append(Tiles[tile]);
                }
            }
        }
        else
        {
            //non-first frame
            //dropping tiles that are not used anymore
            for (int tile=0;tile<iTilesCount;tile++)
            {
                if ( (iTilesUsage[frame][tile]==0) && (iTilesUsage[frame-1][tile]==1) )
                    TilesVRAMUsage[TilesVRAMUsage.indexOf(tile)] = -1;
            }
            //adding tiles
            for (int tile=0;tile<iTilesCount;tile++)
            {
                if ( (iTilesUsage[frame][tile]==1) && (iTilesUsage[frame-1][tile]==0) )
                {
                    assert (TilesVRAMUsage.contains(-1));
                    int location = TilesVRAMUsage.indexOf(-1);
                    TilesVRAMUsage[location] = tile;
                    chunk_tiles.append(QByteArray(1,location>>8));
                    chunk_tiles.append(QByteArray(1,location));
                    chunk_tiles.append(Tiles[tile]);
                }
            }
        }

        chunk_commands.clear();
        chunk_palettes.clear();

        //now saving the stream data for the frame, it's either commands or palettes
        //commands:
        // skip block : opcode 01 dummy 00 skip amount SS SS
        // short tile : opcode 00/40/80/C0 palette PP tile number TT TT
        //  long tile : opcode 05/45/85/C5 dummy 00 00 palette PP tile number TT TT TT TT
        //  frame end : opcode 02 dummy 00 00 00
        //palettes:
        // usage UU index II palette in RGB555, 16 colors 16 x (PP PP)
        while ((iParseIndex < _stream_data.size()) && (false == bFrameEnd))
        {
            //read next command
            Command = _get_short_from_bytearray(&_stream_data,iParseIndex);
            Command_Opcode = Command & 0x3F;
            Command_Param = Command >> 6;
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
                // skip block : opcode 01 dummy 00 skip amount SS SS
                chunk_commands.append(QByteArray(1,0x01));
                chunk_commands.append(QByteArray(1,0x00));
                chunk_commands.append(QByteArray(1,Command_Param>>8));
                chunk_commands.append(QByteArray(1,Command_Param));
                //if (Command_Param <= 0)
                  //  assert(0);
                break;
            case ShortTileIdx:
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_short_from_bytearray(&_stream_data,iParseIndex);
                iCurrentTileIndexUpdated = TilesVRAMUsage.indexOf(iCurrentTile);
                if (iCurrentTileIndexUpdated < 0)
                    qDebug("Lost tile at index 0x%x, iCurrentTile = %i", iParseIndex, iCurrentTile);
                if (iCurrentTileIndexUpdated > 32767)
                    qDebug("Lost at top tile at index 0x%x, iCurrentTile = %i", iParseIndex, iCurrentTile);
                iParseIndex+=2;
                //draw the tile at X,Y
                for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            uindex8 = Tiles.at(iCurrentTile).at((x+y*8)/2);
                            if ((x+y*8)%2) uindex8 &= 0x0F; else uindex8 >>= 4;
                            break;
                        case 1:
                            uindex8 = Tiles.at(iCurrentTile).at((y*8+7-x)/2);
                            if ((y*8+7-x)%2) uindex8 &= 0x0F; else uindex8 >>= 4;
                            break;
                        case 2:
                            uindex8 = Tiles.at(iCurrentTile).at((56-y*8+x)/2);
                            if ((56-y*8+x)%2) uindex8 &= 0x0F; else uindex8 >>= 4;
                            break;
                        case 3:
                            uindex8 = Tiles.at(iCurrentTile).at((63-y*8-x)/2);
                            if ((63-y*8-x)%2) uindex8 &= 0x0F; else uindex8 >>= 4;
                            break;
                        }
                        color.setRed((uint8_t)(CurrentPalettes.at(iCurrentPalette).at(uindex8*4)));
                        color.setGreen((uint8_t)(CurrentPalettes.at(iCurrentPalette).at(uindex8*4+1)));
                        color.setBlue((uint8_t)(CurrentPalettes.at(iCurrentPalette).at(uindex8*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                // short tile : opcode 00/40/80/C0 palette PP tile number TT TT
                switch (iMirrorFlags)
                {
                case 0:
                    chunk_commands.append(QByteArray(1,0x00));
                    break;
                case 1:
                    chunk_commands.append(QByteArray(1,0x40));
                    break;
                case 2:
                    chunk_commands.append(QByteArray(1,0x80));
                    break;
                case 3:
                    chunk_commands.append(QByteArray(1,0xC0));
                    break;
                }
                c = iCurrentPalette;
                chunk_commands.append(QByteArray(1,c));
                c = (iCurrentTileIndexUpdated)>>8;
                chunk_commands.append(QByteArray(1,c));
                c = iCurrentTileIndexUpdated;
                chunk_commands.append(QByteArray(1,c));
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
                iParseIndex+=4;
                break;
            case LongTileIdx:
                iCurrentPalette = Command_Param>>2;
                iMirrorFlags = Command_Param & 0x03;
                iCurrentTile = _get_int_from_bytearray(&_stream_data,iParseIndex);
                iCurrentTileIndexUpdated = TilesVRAMUsage.indexOf(iCurrentTile);
                if (iCurrentTileIndexUpdated < 0)
                    qDebug("Lost tile at index 0x%x, iCurrentTile = %i", iParseIndex, iCurrentTile);
                if (iCurrentTileIndexUpdated > 32767)
                    qDebug("Lost at top tile at index 0x%x, iCurrentTile = %i", iParseIndex, iCurrentTile);
                iParseIndex+=4;
                //draw the tile at X,Y
                for (int x=0;x<8;x++)
                {
                    for (int y=0;y<8;y++)
                    {
                        switch(iMirrorFlags)
                        {
                        case 0:
                            uindex8 = Tiles.at(iCurrentTile).at((x+y*8)/2);
                            if ((x+y*8)%2) uindex8 &= 0x0F; else uindex8 >>= 4;
                            break;
                        case 1:
                            uindex8 = Tiles.at(iCurrentTile).at((y*8+7-x)/2);
                            if ((y*8+7-x)%2) uindex8 &= 0x0F; else uindex8 >>= 4;
                            break;
                        case 2:
                            uindex8 = Tiles.at(iCurrentTile).at((56-y*8+x)/2);
                            if ((56-y*8+x)%2) uindex8 &= 0x0F; else uindex8 >>= 4;
                            break;
                        case 3:
                            uindex8 = Tiles.at(iCurrentTile).at((63-y*8-x)/2);
                            if ((63-y*8-x)%2) uindex8 &= 0x0F; else uindex8 >>= 4;
                            break;
                        }
                        color.setRed((uint8_t)(CurrentPalettes.at(iCurrentPalette).at(uindex8*4)));
                        color.setGreen((uint8_t)(CurrentPalettes.at(iCurrentPalette).at(uindex8*4+1)));
                        color.setBlue((uint8_t)(CurrentPalettes.at(iCurrentPalette).at(uindex8*4+2)));

                        me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                    }
                }
                //  long tile : opcode 05/45/85/C5 dummy 00 00 palette PP tile number TT TT TT TT
                switch (iMirrorFlags)
                {
                case 0:
                    chunk_commands.append(QByteArray(1,0x00));
                    break;
                case 1:
                    chunk_commands.append(QByteArray(1,0x40));
                    break;
                case 2:
                    chunk_commands.append(QByteArray(1,0x80));
                    break;
                case 3:
                    chunk_commands.append(QByteArray(1,0xC0));
                    break;
                }
                c = iCurrentPalette;
                chunk_commands.append(QByteArray(1,c));
                c = (iCurrentTileIndexUpdated)>>8;
                chunk_commands.append(QByteArray(1,c));
                c = iCurrentTileIndexUpdated;
                chunk_commands.append(QByteArray(1,c));
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
                iParseIndex+=6;
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
                CurrentPalettes[iCurrentPalette].clear();
                CurrentPalettes[iCurrentPalette].append(_stream_data.mid(iParseIndex,Palette_Size*4));
                iParseIndex+=(Palette_Size*4);
                // usage UU index II palette in RGB555, 16 colors 16 x (PP PP)
                chunk_palettes.append(QByteArray(1,0x01));//usage
                chunk_palettes.append(QByteArray(1,iCurrentPalette));//palette number
                for (int i=0;i<16;i++)
                {
                    c = 0x00 | ((((uint8_t)CurrentPalettes[iCurrentPalette][i*4+2]) & 0xF8) >> 1)  | (((uint8_t)CurrentPalettes[iCurrentPalette][i*4+1] & 0xC0) >> 6);
                    chunk_palettes.append(QByteArray(1,c));
                    c = ((((uint8_t)CurrentPalettes[iCurrentPalette][i*4+1]) & 0x38) << 2)  | (((uint8_t)CurrentPalettes[iCurrentPalette][i*4] & 0xF8 ) >> 3);
                    chunk_palettes.append(QByteArray(1,c));
                }
                break;
            case FrameEnd:
                bFrameEnd = true;
                //  frame end : opcode 02 dummy 00 00 00
                chunk_commands.append(QByteArray(1,0x02));
                chunk_commands.append(QByteArray(1,0x00));
                chunk_commands.append(QByteArray(1,0x00));
                chunk_commands.append(QByteArray(1,0x00));
                break;
            case TileSet:
                //tiles are already loaded, skipping
                iTilesStart=_get_int_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=4;
                iTilesEnd=_get_int_from_bytearray(&_stream_data,iParseIndex);
                iParseIndex+=4;
                for (int i=iTilesStart;i<=iTilesEnd;i++)
                    iParseIndex+=64;
                break;
            case SetDimensions:
                //skippping
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
        }

        //write the frame header
        out_file.write(QByteArray("FRME"));//header magic
        assert(chunk_tiles.size() % 34 == 0);
        int tiles = chunk_tiles.size() / 34;
        out_file.write(QByteArray(1,tiles>>24));
        out_file.write(QByteArray(1,tiles>>16));
        out_file.write(QByteArray(1,tiles>>8));
        out_file.write(QByteArray(1,tiles));
        assert(chunk_palettes.size() % 34 == 0);
        int palettes = chunk_palettes.size() / 34;
        out_file.write(QByteArray(1,palettes>>24));
        out_file.write(QByteArray(1,palettes>>16));
        out_file.write(QByteArray(1,palettes>>8));
        out_file.write(QByteArray(1,palettes));
        int command_bytes = chunk_commands.size();
        out_file.write(QByteArray(1,command_bytes>>24));
        out_file.write(QByteArray(1,command_bytes>>16));
        out_file.write(QByteArray(1,command_bytes>>8));
        out_file.write(QByteArray(1,command_bytes));

        //write the frame data
        out_file.write(chunk_tiles);
        out_file.write(chunk_palettes);
        out_file.write(chunk_commands);

        //filling the block end
        while (out_file.size() % BLOCK_SIZE) out_file.write(QByteArray(1,0));

        //go to next frame
        if (frame % 500 == 0) qDebug("PASS 3 : frame %d",frame);
        bFrameEnd = false;
    }

    out_file.close();

    //--------------- PASS 3 done

    //dumping tiles usage report
    /*QFile out_file_t("tiles_usage.txt");
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
            //if (Loads_per_sec[frame] > 250000)
            //    out_file_t.write(QString("Tiles load amount overflow at frame %1, amount %2\r\n").arg(frame).arg(Loads_per_sec[frame]).toLatin1());
        }
        //calculating tiles to drop for this frame
        int Drop = 0;
        for (int tile=0;tile<iTilesCount;tile++)
            if ( (1==iTilesUsage[frame][tile]) && (0==iTilesUsage[frame+1][tile]))
                Drop++;

        out_file_t.write(QString("Frame %1 : tiles %2 load %3 drop %4, stream %5 load per second %6 bytes\r\n").arg(frame).arg(Usage).arg(Loads[frame]).arg(Drop).arg(Stream_bytes[frame]).arg(Loads_per_sec[frame]).toLatin1());
    }
    out_file_t.close();*/

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


void MainWindow::on_pushButton_3_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open Video"), "", tr("Sega Saturn Tilemotion Files (*.gts)"));
    if (fileName.size())
        ui->lineEdit_2->setText(fileName);
}


void MainWindow::on_pushButton_4_clicked()
{
    WorkerThread *workerThread = new WorkerThread();
    //connect(workerThread, &WorkerThread::resultReady, this, &MyObject::handleResults);
    connect(workerThread, &WorkerThread::finished, workerThread, &QObject::deleteLater);
    connect(workerThread, &WorkerThread::outputFrame, this, &MainWindow::drawFrame);
    workerThread->setFilename(ui->lineEdit_2->text());
    workerThread->start();
    //delete(workerThread);
}

void MainWindow::drawFrame(QRgb * frame_data, int size_x, int size_y)
{
    //drawing the image
    QImage img(size_x,size_y,QImage::Format_ARGB32);

    for (int x=0;x<size_x;x++)
    {
        for (int y=0;y<size_y;y++)
        {
            img.setPixel(x,y,frame_data[y*size_x + x]);
        }
    }

    ui->label_3->setPixmap(QPixmap::fromImage(img));
}

bool WorkerThread::setFilename(QString filename)
{
    this->filename = filename;
    me_frame_data = 0;
}

void WorkerThread::run()
{
    QByteArray frame_data;

    //playing back the saturn-optimized tilemotion file
    QFile in_file(filename);
    in_file.open(QIODevice::ReadOnly);
    QByteArray stream = in_file.readAll();
    in_file.close();

    int index = 256;
    int frame = 0;
    int size_x = (uint8_t)stream[3] + (uint8_t)stream[2] * 0x100;
    int size_y = (uint8_t)stream[7] + (uint8_t)stream[6] * 0x100;
    int frames = (uint8_t)stream[11] + (uint8_t)stream[10] * 0x100;
    int skip;

    if (me_frame_data)
        free(me_frame_data);
    me_frame_data = (QRgb *)malloc(size_x*size_y*sizeof(QRgb));

    QVector<QByteArray> Tiles;
    Tiles.resize(8000);

    QVector<QByteArray> Palettes;
    Palettes.resize(128);

    int Screen_indexes[64][64];
    int Screen_rotations[64][64];
    int Screen_palettes[64][64];
    for (int i=0;i<64;i++)
        for (int j=0;j<64;j++)
        {
            Screen_indexes[i][j] = -1;
            Screen_rotations[i][j] = -1;
            Screen_palettes[i][j] = -1;
        }

    while (frame < (frames))
    {
        if (90 == frame)
            volatile int dummy=0;
        if (stream[index] != 'F')
            assert(0);
        if (stream[index+1] != 'R')
            assert(0);
        if (stream[index+2] != 'M')
            assert(0);
        if (stream[index+3] != 'E')
            assert(0);
        index+=4;
        int frame_tiles = (uint8_t)stream[index+3] + (uint8_t)stream[index+2] * 0x100;
        index+=4;
        int frame_palettes = (uint8_t)stream[index+3] + (uint8_t)stream[index+2] * 0x100;
        index+=4;
        int frame_commands = (uint8_t)stream[index+3] + (uint8_t)stream[index+2] * 0x100 + (uint8_t)stream[index+1] * 0x10000;
        frame_commands /= 4;
        index+=4;

        for (int i=0;i<frame_tiles;i++)
        {
            int tile_index = (uint8_t)stream[index+1] + (uint8_t)stream[index] * 0x100;
            index+=2;
            Tiles[tile_index].clear();
            Tiles[tile_index].append(stream.mid(index,32));
            QByteArray _dbg(Tiles[tile_index]);
            index+=32;
        }

        for (int i=0;i<frame_palettes;i++)
        {
            int palette_index = (uint8_t)stream[index+1];
            index+=2;
            Palettes[palette_index].clear();
            Palettes[palette_index].append(stream.mid(index,32));
            index+=32;
        }

        int x=0;
        int y=0;
        for (int i=0;i<frame_commands;i++)
        {
            int command = (uint8_t)stream[index];
            index++;
            switch (command)
            {
            case 0x01:
                //skip block
                index++;
                skip = (uint8_t)stream[index+1] + (uint8_t)stream[index] * 0x100;
                skip++;
                while (skip>0)
                {
                    x++;
                    if (x == size_x/8)
                    {
                        y++;
                        x = 0;
                    }
                    skip--;
                }
                index+=2;
                break;
            case 0x00:
                //ShortTileIdx, no rotation
                Screen_palettes[x][y] = (uint8_t)stream[index];
                assert(Screen_palettes[x][y]<128);
                assert(Screen_palettes[x][y]>=0);
                index++;
                Screen_indexes[x][y] = (uint8_t)stream[index+1] + (uint8_t)stream[index] * 0x100;
                index+=2;
                Screen_rotations[x][y] = 0;
                x++;
                if (x == size_x/8)
                {
                    y++;
                    x = 0;
                }
                break;
            case 0x40:
                //ShortTileIdx, rotation
                Screen_palettes[x][y] = (uint8_t)stream[index];
                assert(Screen_palettes[x][y]<128);
                assert(Screen_palettes[x][y]>=0);
                index++;
                Screen_indexes[x][y] = (uint8_t)stream[index+1] + (uint8_t)stream[index] * 0x100;
                index+=2;
                Screen_rotations[x][y] = 0x40;
                x++;
                if (x == size_x/8)
                {
                    y++;
                    x = 0;
                }
                break;
            case 0x80:
                //ShortTileIdx, rotation
                Screen_palettes[x][y] = (uint8_t)stream[index];
                assert(Screen_palettes[x][y]<128);
                assert(Screen_palettes[x][y]>=0);
                index++;
                Screen_indexes[x][y] = (uint8_t)stream[index+1] + (uint8_t)stream[index] * 0x100;
                index+=2;
                Screen_rotations[x][y] = 0x80;
                x++;
                if (x == size_x/8)
                {
                    y++;
                    x = 0;
                }
                break;
            case 0xC0:
                //ShortTileIdx, rotation
                Screen_palettes[x][y] = (uint8_t)stream[index];
                assert(Screen_palettes[x][y]<128);
                assert(Screen_palettes[x][y]>=0);
                index++;
                Screen_indexes[x][y] = (uint8_t)stream[index+1] + (uint8_t)stream[index] * 0x100;
                index+=2;
                Screen_rotations[x][y] = 0xC0;
                x++;
                if (x == size_x/8)
                {
                    y++;
                    x = 0;
                    assert(y<=size_y/8);
                }
                break;
            case 0x02:
                //frame end, advance to 256 bytes block edge
                //while (fifo_read_index%256 != 0)
                //	fifo_read_index++;
                index+=3;
                break;
            }
        }

        while (index%256 != 0)
        {
            index++;
        }

        //rendering the image
        //QVector<QVector<QRgb>> me_canvas;
        //me_canvas.resize(size_x);
        //for (int i=0;i<me_canvas.size();i++)
            //me_canvas[i].resize(size_y);

        QColor color;
        int tile_index,rot;
        QByteArray palette;
        int pixel;
        int pixel555;
        int _x,_y;
        for (int tile_y=0;tile_y<size_y/8;tile_y++)
        {
            for (int tile_x=0;tile_x<size_x/8;tile_x++)
            {
                for (int y=0;y<8;y++)
                {
                    for (int x=0;x<8;x++)
                    {
                        palette = Palettes[Screen_palettes[tile_x][tile_y]];
                        tile_index = Screen_indexes[tile_x][tile_y];
                        //QByteArray _dbg(Tiles[tile_index]);
                        rot = Screen_rotations[tile_x][tile_y];
                        switch (rot)
                        {
                        case 0x00:
                            _x=x;_y=y;
                            break;
                        case 0x40:
                            _x=7-x;_y=y;
                            break;
                        case 0x80:
                            _x=x;_y=7-y;
                            break;
                        case 0xC0:
                            _x=7-x;_y=7-y;
                            break;
                        default:
                            _x=x;_y=y;
                            break;
                        }
                        assert(_y*4+_x/2 < 32);
                        assert(_y*4+_x/2 >= 0);
                        if (tile_index>=Tiles.size())
                            assert(0);
                        if (tile_index < 0)
                            assert(0);
                        if (Tiles[tile_index].size() < 32)
                            assert(0);//Tiles[tile_index].append(QByteArray(32,0));
                        pixel = (uint8_t)Tiles[tile_index][_y*4+_x/2];
                        if (_x%2)
                            pixel = (pixel&0x0F);
                        else
                            pixel = ((pixel>>4)&0x0F);
                        pixel555 = (uint8_t)palette[pixel*2+1] + (uint8_t)palette[pixel*2] * 0x100;
                        color.setBlue((uint8_t)((pixel555&0x7C00)>>7));
                        color.setGreen((uint8_t)((pixel555&0x03E0)>>2));
                        color.setRed((uint8_t)((pixel555&0x001F)<<3));
                        me_frame_data[tile_x*8+x + (tile_y*8+y)*size_x] = color.rgb();
                    }
                }
            }
        }

        emit outputFrame(me_frame_data,size_x,size_y);

        qDebug("Rendered frame %d of %d", frame, frames);

        //frame processing done
        frame++;

        //sleep(1000);
    }

};

