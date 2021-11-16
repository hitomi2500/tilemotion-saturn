#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFile>
#include <QPainter>
#include <QPicture>

//#define CANVAS_X 352
//#define CANVAS_Y 200
#define CANVAS_X 704
#define CANVAS_Y 448

QRgb me_canvas[CANVAS_X][CANVAS_Y];

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
}

MainWindow::~MainWindow()
{
    delete ui;
}

int32_t _get_int_from_bytearray(QByteArray * b, int index)
{
    uint8_t * p8 = (uint8_t *)b->data();
    int32_t i = p8[index]+p8[index+1]*0x100+p8[index+2]*0x10000+p8[index+3]*0x1000000;
    return i;
}

int16_t _get_short_from_bytearray(QByteArray * b, int index)
{
    uint8_t * p8 = (uint8_t *)b->data();
    int16_t i = p8[index]+p8[index+1]*0x100;
    return i;
}

//binary format:
// 1st few blocks are a metaheader: contains a number of blocks in file and a type of each one
// types :
// 00 - tiles data
// 01 - commands data
// 00 - tiles data
// 00 - tiles data

void MainWindow::on_pushButton_clicked()
{
    QFile out_file("V001.GTY");
    out_file.open(QIODevice::WriteOnly|QIODevice::Truncate);

    ProcessChunk("ba.tmv0");

    //write outfile header
    uint8_t c;
    c = Tiles.size()>>8;
    out_file.write(QByteArray(1,c));
    c = Tiles.size();
    out_file.write(QByteArray(1,c));
    out_file.write(QByteArray(2046,0));
}


void ProcessChunk(QString filename)
{
    QFile in_file(filename);
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

    //read tiles list
    QByteArray _dims_data = in_file.read(14);
    QByteArray _tiles_cmd_data = in_file.read(10);
    uint16_t Palette_Size = _get_short_from_bytearray(&_tiles_cmd_data,0);
    Palette_Size = Palette_Size >> 6;
    uint32_t TilesStart = _get_int_from_bytearray(&_tiles_cmd_data,2);
    uint32_t TilesEnd = _get_int_from_bytearray(&_tiles_cmd_data,6);
    QByteArray _tiles_data = in_file.read((TilesEnd-TilesStart+1)*64);
    QList<QByteArray> Tiles;
    for (int i=0;i<_tiles_data.size()/64;i++)
    {
        Tiles.append(_tiles_data.mid(i*64,64));
    }

    uint8_t c;
    c = Tiles.size()>>8;
    out_file.write(QByteArray(1,c));
    c = Tiles.size();
    out_file.write(QByteArray(1,c));
    out_file.write(QByteArray(2046,0));
    for (int i=0;i<Tiles.size();i++)
    {
        for (int j=0;j<32;j++)
        {
            c = Tiles.at(i)[j*2] << 4;
            c |= Tiles.at(i)[j*2+1] & 0x0F;
            out_file.write(QByteArray(1,c));
        }

    }
    //round up
    int leftover = out_file.size()%2048;
    if (leftover != 0) leftover = 2048-leftover;
    out_file.write(QByteArray(leftover,'\0'));

    QList<QByteArray> Palettes;
    for (int i=0;i<128;i++)
        Palettes.append(QByteArray());

    //trying to render keyframe 0
    //QByteArray _keyframe_data = in_file.read(Keyframes_List[0].compressed_size);
    QByteArray _keyframe_data = in_file.readAll();

    int iParseIndex = 0;
    uint16_t Command;

    uint8_t Command_Opcode;
    uint16_t Command_Param;

    bool bFrameEnd = false;
    int iCurrentTileX=0;
    int iCurrentTileY=0;
    int iCurrentTile;
    int iCurrentPalette;
    int iMirrorFlags;
    int index;
    QColor color;
    QPainter pai;
    QPicture pic;
    QPicture pic2;
    QImage img(CANVAS_X,CANVAS_Y,QImage::Format_ARGB32);

    for (int frame=0;frame<2000;frame++)
{
        //while (/*(iParseIndex < _keyframe_data.size()) &&*/ (false == bFrameEnd))
        while ((iParseIndex < _keyframe_data.size()) && (false == bFrameEnd))
    {
        //read next command
        Command = _get_short_from_bytearray(&_keyframe_data,iParseIndex);
        iParseIndex+=2;
        Command_Opcode = Command & 0x3F;
        Command_Param = Command >> 6;
        switch (Command_Opcode)
        {
        case SkipBlock:
            for (int i=0;i<=Command_Param;i++)
            {
                iCurrentTileX++;
                if (iCurrentTileX >= CANVAS_X/8)
                {
                    iCurrentTileX = 0;
                    iCurrentTileY++;
                    if (iCurrentTileY >= CANVAS_Y/8)
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
            iCurrentTile = _get_short_from_bytearray(&_keyframe_data,iParseIndex);
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
            c = (iCurrentTile+0x400)>>8;  //was 0x100 for lowres
            out_file.write(QByteArray(1,c));
            c = iCurrentTile;
            out_file.write(QByteArray(1,c));
            //if (iCurrentTile!= 0)
            //  c++;

            //move to next X,Y
            for (int i=0;i<=0;i++)
            {
                iCurrentTileX++;
                if (iCurrentTileX >= CANVAS_X/8)
                {
                    iCurrentTileX = 0;
                    iCurrentTileY++;
                    if (iCurrentTileY >= CANVAS_Y/8)
                        iCurrentTileY = 0;
                }
            }
            break;
        case LongTileIdx:
            iCurrentPalette = Command_Param>>2;
            iMirrorFlags = Command_Param & 0x03;
            iCurrentTile = _get_short_from_bytearray(&_keyframe_data,iParseIndex);
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
                    color.setRed(Palettes.at(iCurrentPalette).at(index*4));
                    color.setGreen(Palettes.at(iCurrentPalette).at(index*4+1));
                    color.setBlue(Palettes.at(iCurrentPalette).at(index*4+2));
                    color.setAlpha(255);

                    me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgba();
                }
            }
            //move to next X,Y
            for (int i=0;i<=0;i++)
            {
                iCurrentTileX++;
                if (iCurrentTileX >= CANVAS_X/8)
                {
                    iCurrentTileX = 0;
                    iCurrentTileY++;
                    if (iCurrentTileY >= CANVAS_Y/8)
                        iCurrentTileY = 0;
                }
            }
            iCurrentTile = _get_short_from_bytearray(&_keyframe_data,iParseIndex);
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
                    color.setRed(Palettes.at(iCurrentPalette).at(index*4));
                    color.setGreen(Palettes.at(iCurrentPalette).at(index*4+1));
                    color.setBlue(Palettes.at(iCurrentPalette).at(index*4+2));

                    me_canvas[iCurrentTileX*8+x][iCurrentTileY*8+y] = color.rgb();
                }
            }
            //move to next X,Y
            for (int i=0;i<=0;i++)
            {
                iCurrentTileX++;
                if (iCurrentTileX >= CANVAS_X/8)
                {
                    iCurrentTileX = 0;
                    iCurrentTileY++;
                    if (iCurrentTileY >= CANVAS_Y/8)
                        iCurrentTileY = 0;
                }
            }
            break;
        case LoadPalette:
            iCurrentPalette = _get_short_from_bytearray(&_keyframe_data,iParseIndex);
            iParseIndex+=2;
            if (iCurrentPalette > 127)
                iCurrentPalette = 127;
            Palettes[iCurrentPalette].clear();
            Palettes[iCurrentPalette].append(_keyframe_data.mid(iParseIndex,Palette_Size*4));
            iParseIndex+=(Palette_Size*4);
            out_file.write(QByteArray(1,0x03));
            out_file.write(QByteArray(1,0x00));
            out_file.write(QByteArray(1,0x00));
            out_file.write(QByteArray(1,iCurrentPalette));
            for (int i=0;i<16;i++)
            {
                c = 0x80 | (((Palettes[iCurrentPalette][i*4+2]) >> 3) << 2)  | ((Palettes[iCurrentPalette][i*4+1] >> 3) >> 3);
                out_file.write(QByteArray(1,c));
                c = (((Palettes[iCurrentPalette][i*4+1]) >> 3) << 5)  | ((Palettes[iCurrentPalette][i*4] >> 3));
                out_file.write(QByteArray(1,c));
            }
            /*out_file.write(Palettes[iCurrentPalette]);
            if (iCurrentPalette == 127)
            {
                //round up
                leftover = out_file.size()%2048;
                if (leftover != 0) leftover = 2048-leftover;
                out_file.write(QByteArray(leftover,'\0'));
            }*/
            break;
        case FrameEnd:
            bFrameEnd = true;
            out_file.write(QByteArray(1,0x02));
            out_file.write(QByteArray(1,0x00));
            out_file.write(QByteArray(1,0x00));
            out_file.write(QByteArray(1,0x00));
            break;
        case TileSet:
            //skip another tiles set for now
            break;
        case SetDimensions:
            //ignore for now
            iParseIndex+=12;
            break;
        case ExtendedCommand:
            //not supported?
            break;
        default:
            //not supported?
            break;
        }
    }

    //draw image

    bFrameEnd = false;

}
    for (int x=0;x<CANVAS_X;x++)
    {
        for (int y=0;y<CANVAS_Y;y++)
        {
            img.setPixel(x,y,me_canvas[x][y]);
        }
    }

    ui->label->setPixmap(QPixmap::fromImage(img));

    in_file.close();
    out_file.close();

}
