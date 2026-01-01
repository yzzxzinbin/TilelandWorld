#include "UnicodeTableScreen.h"
#include "TuiUtils.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace TilelandWorld {
namespace UI {

namespace {
    const BoxStyle kModernFrame{"╭", "╮", "╰", "╯", "─", "│"};
}

UnicodeTableScreen::UnicodeTableScreen()
    : surface(100, 40),
      input(std::make_unique<InputController>()) {
    initBlocks();
    lastCaretToggle = std::chrono::steady_clock::now();
}

void UnicodeTableScreen::initBlocks() {
    blocks = {
        {"基本拉丁字母", 0x0000, 0x007F},
        {"拉丁字母补充1", 0x0080, 0x00FF},
        {"拉丁字母扩展A", 0x0100, 0x017F},
        {"拉丁字母扩展B", 0x0180, 0x024F},
        {"国际音标扩展", 0x0250, 0x02AF},
        {"占位修饰符号", 0x02B0, 0x02FF},
        {"组合附加符号", 0x0300, 0x036F},
        {"Greek and Coptic", 0x0370, 0x03FF},
        {"西里尔字母", 0x0400, 0x04FF},
        {"西里尔字母补充", 0x0500, 0x052F},
        {"亚美尼亚字母", 0x0530, 0x058F},
        {"希伯来文字母", 0x0590, 0x05FF},
        {"阿拉伯文字母", 0x0600, 0x06FF},
        {"Syriac", 0x0700, 0x074F},
        {"阿拉伯文补充", 0x0750, 0x077F},
        {"它拿字母", 0x0780, 0x07BF},
        {"西非书面语言", 0x07C0, 0x07FF},
        {"撒玛利亚字母", 0x0800, 0x083F},
        {"曼达字母", 0x0840, 0x085F},
        {"叙利亚文补充", 0x0860, 0x086F},
        {"阿拉伯字母扩展B", 0x0870, 0x089F},
        {"阿拉伯字母扩展A", 0x08A0, 0x08FF},
        {"天城文", 0x0900, 0x097F},
        {"孟加拉文", 0x0980, 0x09FF},
        {"果鲁穆奇字母", 0x0A00, 0x0A7F},
        {"古吉拉特文", 0x0A80, 0x0AFF},
        {"奥里亚文", 0x0B00, 0x0B7F},
        {"泰米尔文", 0x0B80, 0x0BFF},
        {"泰卢固文", 0x0C00, 0x0C7F},
        {"卡纳达文", 0x0C80, 0x0CFF},
        {"马拉雅拉姆文", 0x0D00, 0x0D7F},
        {"僧伽罗文", 0x0D80, 0x0DFF},
        {"泰文", 0x0E00, 0x0E7F},
        {"老挝文", 0x0E80, 0x0EFF},
        {"藏文", 0x0F00, 0x0FFF},
        {"缅甸文", 0x1000, 0x109F},
        {"格鲁吉亚字母", 0x10A0, 0x10FF},
        {"谚文字母", 0x1100, 0x11FF},
        {"埃塞俄比亚字母", 0x1200, 0x137F},
        {"埃塞俄比亚字母补充", 0x1380, 0x139F},
        {"切罗基字母", 0x13A0, 0x13FF},
        {"统一加拿大原住民音节文字", 0x1400, 0x167F},
        {"欧甘字母", 0x1680, 0x169F},
        {"卢恩字母", 0x16A0, 0x16FF},
        {"他加禄字母", 0x1700, 0x171F},
        {"哈努诺文", 0x1720, 0x173F},
        {"布希德字母", 0x1740, 0x175F},
        {"塔格班瓦字母", 0x1760, 0x177F},
        {"高棉文", 0x1780, 0x17FF},
        {"蒙古文", 0x1800, 0x18AF},
        {"统一加拿大原住民音节文字扩展", 0x18B0, 0x18FF},
        {"林布文", 0x1900, 0x194F},
        {"德宏傣文", 0x1950, 0x197F},
        {"新傣仂文", 0x1980, 0x19DF},
        {"高棉文符号", 0x19E0, 0x19FF},
        {"布吉文", 0x1A00, 0x1A1F},
        {"老傣文", 0x1A20, 0x1AAF},
        {"组合附加符号扩展", 0x1AB0, 0x1AFF},
        {"巴厘字母", 0x1B00, 0x1B7F},
        {"巽他字母", 0x1B80, 0x1BBF},
        {"巴塔克文", 0x1BC0, 0x1BFF},
        {"雷布查字母", 0x1C00, 0x1C4F},
        {"桑塔利文", 0x1C50, 0x1C7F},
        {"西里尔字母扩展C", 0x1C80, 0x1C8F},
        {"格鲁吉亚字母扩展", 0x1C90, 0x1CBF},
        {"巽他字母补充", 0x1CC0, 0x1CCF},
        {"吠陀梵文", 0x1CD0, 0x1CFF},
        {"语音学扩展", 0x1D00, 0x1D7F},
        {"语音学扩展补充", 0x1D80, 0x1DBF},
        {"组合附加符号补充", 0x1DC0, 0x1DFF},
        {"拉丁字母扩展附加", 0x1E00, 0x1EFF},
        {"希腊字母扩展", 0x1F00, 0x1FFF},
        {"常用标点", 0x2000, 0x206F},
        {"上标及下标", 0x2070, 0x209F},
        {"货币符号", 0x20A0, 0x20CF},
        {"组合用记号", 0x20D0, 0x20FF},
        {"字母式符号", 0x2100, 0x214F},
        {"数字形式", 0x2150, 0x218F},
        {"箭头", 0x2190, 0x21FF},
        {"数学运算符", 0x2200, 0x22FF},
        {"杂项工业符号", 0x2300, 0x23FF},
        {"控制图片", 0x2400, 0x243F},
        {"光学识别符", 0x2440, 0x245F},
        {"带圈或括号的字母数字", 0x2460, 0x24FF},
        {"制表符", 0x2500, 0x257F},
        {"方块元素", 0x2580, 0x259F},
        {"几何图形", 0x25A0, 0x25FF},
        {"杂项符号", 0x2600, 0x26FF},
        {"印刷符号", 0x2700, 0x27BF},
        {"杂项数学符号A", 0x27C0, 0x27EF},
        {"追加箭头A", 0x27F0, 0x27FF},
        {"盲文点字模型", 0x2800, 0x28FF},
        {"追加箭头B", 0x2900, 0x297F},
        {"杂项数学符号B", 0x2980, 0x29FF},
        {"追加数学运算符", 0x2A00, 0x2AFF},
        {"杂项符号和箭头", 0x2B00, 0x2BFF},
        {"格拉哥里字母", 0x2C00, 0x2C5F},
        {"拉丁字母扩展C", 0x2C60, 0x2C7F},
        {"科普特字母", 0x2C80, 0x2CFF},
        {"格鲁吉亚字母补充", 0x2D00, 0x2D2F},
        {"提非纳文", 0x2D30, 0x2D7F},
        {"埃塞俄比亚字母扩展", 0x2D80, 0x2DDF},
        {"Cyrillic Extended-A", 0x2DE0, 0x2DFF},
        {"追加标点", 0x2E00, 0x2E7F},
        {"中日韩部首补充", 0x2E80, 0x2EFF},
        {"康熙部首", 0x2F00, 0x2FDF},
        {"表意文字描述符", 0x2FF0, 0x2FFF},
        {"中日韩符号和标点", 0x3000, 0x303F},
        {"日文平假名", 0x3040, 0x309F},
        {"日文片假名", 0x30A0, 0x30FF},
        {"注音字母", 0x3100, 0x312F},
        {"谚文兼容字母", 0x3130, 0x318F},
        {"象形字注释标志", 0x3190, 0x319F},
        {"注音字母扩展", 0x31A0, 0x31BF},
        {"中日韩笔画", 0x31C0, 0x31EF},
        {"日文片假名语音扩展", 0x31F0, 0x31FF},
        {"带圈中日韩字母和月份", 0x3200, 0x32FF},
        {"中日韩字符集兼容", 0x3300, 0x33FF},
        {"中日韩统一表意文字扩展区A", 0x3400, 0x4DBF},
        {"易经六十四卦符号", 0x4DC0, 0x4DFF},
        {"中日韩统一表意文字", 0x4E00, 0x9FFF},
        {"彝文音节", 0xA000, 0xA48F},
        {"彝文字根", 0xA490, 0xA4CF},
        {"傈僳文", 0xA4D0, 0xA4FF},
        {"Vai", 0xA500, 0xA63F},
        {"Cyrillic Extended-B", 0xA640, 0xA69F},
        {"巴姆穆文", 0xA6A0, 0xA6FF},
        {"声调修饰字母", 0xA700, 0xA71F},
        {"拉丁字母扩展D", 0xA720, 0xA7FF},
        {"Syloti Nagri", 0xA800, 0xA82F},
        {"Common Indic Number Forms", 0xA830, 0xA83F},
        {"八思巴文", 0xA840, 0xA87F},
        {"索拉什特拉文", 0xA880, 0xA8DF},
        {"Devanagari Extended", 0xA8E0, 0xA8FF},
        {"克耶字母", 0xA900, 0xA92F},
        {"勒姜字母", 0xA930, 0xA95F},
        {"Hangul Jamo Extended-A", 0xA960, 0xA97F},
        {"Javanese", 0xA980, 0xA9DF},
        {"缅甸文扩展B", 0xA9E0, 0xA9FF},
        {"Cham", 0xAA00, 0xAA5F},
        {"Myanmar Extended-A", 0xAA60, 0xAA7F},
        {"Tai Viet", 0xAA80, 0xAADF},
        {"Meetei Mayek Extensions", 0xAAE0, 0xAAFF},
        {"埃塞俄比亚字母扩展A", 0xAB00, 0xAB2F},
        {"拉丁字母扩展E", 0xAB30, 0xAB6F},
        {"切罗基文补充", 0xAB70, 0xABBF},
        {"Meetei Mayek", 0xABC0, 0xABFF},
        {"谚文音节", 0xAC00, 0xD7AF},
        {"谚文字母扩展B", 0xD7B0, 0xD7FF},
        {"私用区", 0xE000, 0xF8FF},
        {"中日韩兼容表意文字", 0xF900, 0xFAFF},
        {"字母表达形式", 0xFB00, 0xFB4F},
        {"阿拉伯文表达形式A", 0xFB50, 0xFDFF},
        {"异体字选择符", 0xFE00, 0xFE0F},
        {"竖排形式", 0xFE10, 0xFE1F},
        {"组合用记号", 0xFE20, 0xFE2F},
        {"中日韩兼容形式", 0xFE30, 0xFE4F},
        {"小写变体形式", 0xFE50, 0xFE6F},
        {"阿拉伯文表达形式B", 0xFE70, 0xFEFF},
        {"半角及全角形式", 0xFF00, 0xFFEF},
        {"特殊", 0xFFF0, 0xFFFF},
        {"线形文字B音节文字", 0x10000, 0x1007F},
        {"线形文字B表意文字", 0x10080, 0x100FF},
        {"爱琴海数字", 0x10100, 0x1013F},
        {"古希腊数字", 0x10140, 0x1018F},
        {"古代符号", 0x10190, 0x101CF},
        {"斐斯托斯圆盘", 0x101D0, 0x101FF},
        {"吕基亚字母", 0x10280, 0x1029F},
        {"卡里亚字母", 0x102A0, 0x102DF},
        {"科普特闰余数字", 0x102E0, 0x102FF},
        {"古意大利字母", 0x10300, 0x1032F},
        {"哥特字母", 0x10330, 0x1034F},
        {"古彼尔姆文", 0x10350, 0x1037F},
        {"乌加里特字母", 0x10380, 0x1039F},
        {"古波斯楔形文字", 0x103A0, 0x103DF},
        {"德瑟雷特字母", 0x10400, 0x1044F},
        {"萧伯纳字母", 0x10450, 0x1047F},
        {"奥斯曼亚字母", 0x10480, 0x104AF},
        {"欧塞奇字母", 0x104B0, 0x104FF},
        {"爱尔巴桑字母", 0x10500, 0x1052F},
        {"高加索阿尔巴尼亚字母", 0x10530, 0x1056F},
        {"维斯库奇文", 0x10570, 0x105BF},
        {"Todhri", 0x105C0, 0x105FF},
        {"线形文字A", 0x10600, 0x1077F},
        {"拉丁字母扩展F", 0x10780, 0x107BF},
        {"塞浦路斯音节文字", 0x10800, 0x1083F},
        {"帝国亚拉姆文", 0x10840, 0x1085F},
        {"帕尔迈拉字母", 0x10860, 0x1087F},
        {"纳巴泰字母", 0x10880, 0x108AF},
        {"哈特拉文", 0x108E0, 0x108FF},
        {"腓尼基字母", 0x10900, 0x1091F},
        {"吕底亚字母", 0x10920, 0x1093F},
        {"Sidetic", 0x10940, 0x1095F},
        {"麦罗埃文圣书体", 0x10980, 0x1099F},
        {"麦罗埃文草书体", 0x109A0, 0x109FF},
        {"佉卢文", 0x10A00, 0x10A5F},
        {"古南阿拉伯字母", 0x10A60, 0x10A7F},
        {"古北阿拉伯字母", 0x10A80, 0x10A9F},
        {"摩尼字母", 0x10AC0, 0x10AFF},
        {"阿维斯陀字母", 0x10B00, 0x10B3F},
        {"碑刻帕提亚文", 0x10B40, 0x10B5F},
        {"碑刻巴列维文", 0x10B60, 0x10B7F},
        {"诗篇巴列维文", 0x10B80, 0x10BAF},
        {"古突厥文", 0x10C00, 0x10C4F},
        {"古匈牙利字母", 0x10C80, 0x10CFF},
        {"哈乃斐罗兴亚文字", 0x10D00, 0x10D3F},
        {"Garay", 0x10D40, 0x10D8F},
        {"卢米文数字", 0x10E60, 0x10E7F},
        {"雅兹迪文", 0x10E80, 0x10EBF},
        {"阿拉伯字母扩展C", 0x10EC0, 0x10EFF},
        {"古粟特字母", 0x10F00, 0x10F2F},
        {"粟特字母", 0x10F30, 0x10F6F},
        {"回鹘字母", 0x10F70, 0x10FAF},
        {"花剌子模字母", 0x10FB0, 0x10FDF},
        {"埃利迈文", 0x10FE0, 0x10FFF},
        {"婆罗米文", 0x11000, 0x1107F},
        {"凯提文", 0x11080, 0x110CF},
        {"索拉僧平文字", 0x110D0, 0x110FF},
        {"查克马文", 0x11100, 0x1114F},
        {"马哈佳尼文", 0x11150, 0x1117F},
        {"夏拉达文", 0x11180, 0x111DF},
        {"古僧伽罗文数字", 0x111E0, 0x111FF},
        {"可吉文", 0x11200, 0x1124F},
        {"穆尔塔尼文", 0x11280, 0x112AF},
        {"库达瓦迪文", 0x112B0, 0x112FF},
        {"古兰塔文", 0x11300, 0x1137F},
        {"Tulu-Tigalari", 0x11380, 0x113FF},
        {"纽瓦字母", 0x11400, 0x1147F},
        {"底罗仆多文", 0x11480, 0x114DF},
        {"悉昙文字", 0x11580, 0x115FF},
        {"莫迪文", 0x11600, 0x1165F},
        {"蒙古文补充", 0x11660, 0x1167F},
        {"塔克里文", 0x11680, 0x116CF},
        {"缅甸扩展-C", 0x116D0, 0x116FF},
        {"阿洪姆文", 0x11700, 0x1174F},
        {"多格拉文", 0x11800, 0x1184F},
        {"瓦兰齐地文", 0x118A0, 0x118FF},
        {"岛屿字母", 0x11900, 0x1195F},
        {"南迪城文", 0x119A0, 0x119FF},
        {"札那巴札尔方形字母", 0x11A00, 0x11A4F},
        {"索永布文字", 0x11A50, 0x11AAF},
        {"加拿大原住民音节文字扩展A", 0x11AB0, 0x11ABF},
        {"包钦豪文", 0x11AC0, 0x11AFF},
        {"天城文扩展A", 0x11B00, 0x11B5F},
        {"Sharada Supplement", 0x11B60, 0x11B7F},
        {"Sunuwar", 0x11BC0, 0x11BFF},
        {"拜克舒基文", 0x11C00, 0x11C6F},
        {"玛钦文", 0x11C70, 0x11CBF},
        {"马萨拉姆贡德文字", 0x11D00, 0x11D5F},
        {"贡贾拉贡德文字", 0x11D60, 0x11DAF},
        {"Tolong Siki", 0x11DB0, 0x11DEF},
        {"望加锡文", 0x11EE0, 0x11EFF},
        {"卡维文", 0x11F00, 0x11F5F},
        {"老傈僳文补充", 0x11FB0, 0x11FBF},
        {"泰米尔文补充", 0x11FC0, 0x11FFF},
        {"楔形文字", 0x12000, 0x123FF},
        {"楔形文字数字和标点符号", 0x12400, 0x1247F},
        {"早期王朝楔形文字", 0x12480, 0x1254F},
        {"塞浦路斯-米诺斯文字", 0x12F90, 0x12FFF},
        {"埃及圣书体", 0x13000, 0x1342F},
        {"埃及圣书体格式控制", 0x13430, 0x1345F},
        {"埃及象形文字扩展-A", 0x13460, 0x143FF},
        {"安纳托利亚象形文字", 0x14400, 0x1467F},
        {"Gurung Khema", 0x16100, 0x1613F},
        {"巴姆穆文字补充", 0x16800, 0x16A3F},
        {"默禄文", 0x16A40, 0x16A6F},
        {"唐萨文", 0x16A70, 0x16ACF},
        {"巴萨文", 0x16AD0, 0x16AFF},
        {"救世苗文", 0x16B00, 0x16B8F},
        {"Kirat Rai", 0x16D40, 0x16D7F},
        {"梅德法伊德林文", 0x16E40, 0x16E9F},
        {"Beria Erfe", 0x16EA0, 0x16EDF},
        {"柏格理苗文", 0x16F00, 0x16F9F},
        {"表意符号和标点符号", 0x16FE0, 0x16FFF},
        {"西夏文", 0x17000, 0x187FF},
        {"西夏文部件", 0x18800, 0x18AFF},
        {"契丹小字", 0x18B00, 0x18CFF},
        {"西夏文补充", 0x18D00, 0x18D7F},
        {"Tangut Components Supplement", 0x18D80, 0x18DFF},
        {"假名扩展B", 0x1AFF0, 0x1AFFF},
        {"假名补充", 0x1B000, 0x1B0FF},
        {"假名扩展A", 0x1B100, 0x1B12F},
        {"小型假名扩展", 0x1B130, 0x1B16F},
        {"女书", 0x1B170, 0x1B2FF},
        {"杜普雷速记", 0x1BC00, 0x1BC9F},
        {"速记格式控制符", 0x1BCA0, 0x1BCAF},
        {"遗留计算补充符号", 0x1CC00, 0x1CEBF},
        {"Miscellaneous Symbols Supplement", 0x1CEC0, 0x1CEFF},
        {"赞玫尼圣歌音乐符号", 0x1CF00, 0x1CFCF},
        {"拜占庭音乐符号", 0x1D000, 0x1D0FF},
        {"音乐符号", 0x1D100, 0x1D1FF},
        {"古希腊音乐记号", 0x1D200, 0x1D24F},
        {"卡克托维克数字", 0x1D2C0, 0x1D2DF},
        {"玛雅数字", 0x1D2E0, 0x1D2FF},
        {"太玄经符号", 0x1D300, 0x1D35F},
        {"算筹", 0x1D360, 0x1D37F},
        {"字母和数字符号", 0x1D400, 0x1D7FF},
        {"萨顿书写符号", 0x1D800, 0x1DAAF},
        {"拉丁字母扩展G", 0x1DF00, 0x1DFFF},
        {"格拉哥里字母补充", 0x1E000, 0x1E02F},
        {"西里尔字母扩展D", 0x1E030, 0x1E08F},
        {"创世纪苗文", 0x1E100, 0x1E14F},
        {"投投文", 0x1E290, 0x1E2BF},
        {"文乔字母", 0x1E2C0, 0x1E2FF},
        {"蒙达里字母", 0x1E4D0, 0x1E4FF},
        {"Ol Onal", 0x1E5D0, 0x1E5FF},
        {"Tai Yo", 0x1E6C0, 0x1E6FF},
        {"埃塞俄比亚字母扩展B", 0x1E7E0, 0x1E7FF},
        {"门德基卡库文", 0x1E800, 0x1E8DF},
        {"阿德拉姆字母", 0x1E900, 0x1E95F},
        {"印度西亚格数字", 0x1EC70, 0x1ECBF},
        {"奥斯曼西亚格数字", 0x1ED00, 0x1ED4F},
        {"阿拉伯字母数字符号", 0x1EE00, 0x1EEFF},
        {"麻将牌", 0x1F000, 0x1F02F},
        {"多米诺骨牌", 0x1F030, 0x1F09F},
        {"扑克牌", 0x1F0A0, 0x1F0FF},
        {"带圈字母数字补充", 0x1F100, 0x1F1FF},
        {"带圈表意文字补充", 0x1F200, 0x1F2FF},
        {"杂项符号和象形文字", 0x1F300, 0x1F5FF},
        {"表情符号", 0x1F600, 0x1F64F},
        {"装饰符号", 0x1F650, 0x1F67F},
        {"交通和地图符号", 0x1F680, 0x1F6FF},
        {"炼金术符号", 0x1F700, 0x1F77F},
        {"几何图形扩展", 0x1F780, 0x1F7FF},
        {"追加箭头C", 0x1F800, 0x1F8FF},
        {"补充符号和象形文字", 0x1F900, 0x1F9FF},
        {"棋类符号", 0x1FA00, 0x1FA6F},
        {"符号和象形文字扩展A", 0x1FA70, 0x1FAFF},
        {"遗留计算符号", 0x1FB00, 0x1FBFF},
        {"中日韩统一表意文字扩展区B", 0x20000, 0x2A6DF},
        {"中日韩统一表意文字扩展区C", 0x2A700, 0x2B73F},
        {"中日韩统一表意文字扩展区D", 0x2B740, 0x2B81F},
        {"中日韩统一表意文字扩展区E", 0x2B820, 0x2CEAF},
        {"中日韩统一表意文字扩展区F", 0x2CEB0, 0x2EBEF},
        {"CJK Unified Ideographs Extension I", 0x2EBF0, 0x2EE5F},
        {"中日韩兼容表意文字补充区", 0x2F800, 0x2FA1F},
        {"中日韩统一表意文字扩展区G", 0x30000, 0x3134F},
        {"中日韩统一表意文字扩展区H", 0x31350, 0x323AF},
        {"CJK Unified Ideographs Extension J", 0x323B0, 0x3347F}
    };
}

void UnicodeTableScreen::show() {
    input->start();
    bool running = true;
    std::cout << "\x1b[?25l" << "\x1b[2J\x1b[H" << std::flush;;

    while (running) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCaretToggle).count() > 500) {
            searchCaretOn = !searchCaretOn;
            lastCaretToggle = now;
        }

        renderFrame();
        painter.present(surface);

        auto events = input->pollEvents();
        for (const auto& ev : events) {
            if (ev.type == InputEvent::Type::Key) {
                handleKey(ev, running);
            } else if (ev.type == InputEvent::Type::Mouse) {
                handleMouse(ev, running);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    input->stop();
    std::cout << "\x1b[?25h";
}

void UnicodeTableScreen::renderFrame() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int w = std::max(80, info.srWindow.Right - info.srWindow.Left + 1);
        int h = std::max(24, info.srWindow.Bottom - info.srWindow.Top + 1);
        surface.resize(w, h);
    }
#endif

    int w = surface.getWidth();
    int h = surface.getHeight();

    surface.clear(theme.itemFg, theme.background, " ");
    // Top and bottom accent bars to match other screens
    surface.fillRect(0, 0, w, 1, theme.accent, theme.accent, " ");
    surface.fillRect(0, h - 1, w, 1, theme.accent, theme.accent, " ");
    surface.drawText(2, 1, "Unicode Table Browser", {0,0,0}, theme.accent);

    // Search Bar
    searchX = 4;
    searchY = 3;
    searchW = 24;
    surface.drawText(searchX, searchY, "Search (Hex): ", theme.itemFg, theme.background);
    int fieldX = searchX + 14;
    surface.fillRect(fieldX, searchY, searchW, 1, theme.focusFg, searchFocused ? theme.focusBg : theme.panel, " ");
    std::string dispSearch = searchQuery + (searchFocused && searchCaretOn ? "|" : "");
    surface.drawText(fieldX + 1, searchY, dispSearch, theme.focusFg, searchFocused ? theme.focusBg : theme.panel);

    // Layout (left block list + right grid)
    int padding = 4;
    blockListX = padding;
    blockListY = 5;
    blockListW = std::max(24, w / 3);
    blockListH = h - blockListY - 3; // leave one row for bottom info

    gridX = blockListX + blockListW + padding;
    gridY = blockListY;
    gridW = w - gridX - padding;
    gridH = blockListH;

    // Bottom info bar
    surface.fillRect(0, h - 2, w, 1, theme.panel, theme.panel, " ");
    surface.drawCenteredText(0, h - 2, w, "Arrows: navigate | Enter: select | F or / to search | Q/Esc: back", theme.hintFg, theme.panel);

    drawBlockList();
    drawCharGrid();
}

void UnicodeTableScreen::drawBlockList() {
    // Clear the entire area first
    surface.fillRect(blockListX, blockListY, blockListW, blockListH, theme.itemFg, theme.panel, " ");

    // Draw anchored frame
    setAnchored(blockListX, blockListY, kModernFrame.topLeft, theme.itemFg, theme.panel);
    setAnchored(blockListX + blockListW - 1, blockListY, kModernFrame.topRight, theme.itemFg, theme.panel);
    setAnchored(blockListX, blockListY + blockListH - 1, kModernFrame.bottomLeft, theme.itemFg, theme.panel);
    setAnchored(blockListX + blockListW - 1, blockListY + blockListH - 1, kModernFrame.bottomRight, theme.itemFg, theme.panel);

    for (int i = 1; i < blockListW - 1; ++i) {
        setAnchored(blockListX + i, blockListY, kModernFrame.horizontal, theme.itemFg, theme.panel);
        setAnchored(blockListX + i, blockListY + blockListH - 1, kModernFrame.horizontal, theme.itemFg, theme.panel);
    }
    for (int j = 1; j < blockListH - 1; ++j) {
        setAnchored(blockListX, blockListY + j, kModernFrame.vertical, theme.itemFg, theme.panel);
        setAnchored(blockListX + blockListW - 1, blockListY + j, kModernFrame.vertical, theme.itemFg, theme.panel);
    }

    // Title
    std::string listTitle = " Planes/Blocks ";
    for (size_t i = 0; i < listTitle.size(); ++i) {
        setAnchored(blockListX + 2 + i, blockListY, listTitle.substr(i, 1), theme.title, theme.panel);
    }

    int visibleRows = blockListH - 2;
    int start = blockScrollOffset;
    for (int i = 0; i < visibleRows && (start + i) < static_cast<int>(blocks.size()); ++i) {
        int idx = start + i;
        int ry = blockListY + 1 + i;
        bool selected = (idx == selectedBlockIdx);
        RGBColor fg = selected ? theme.focusFg : theme.itemFg;
        RGBColor bg = selected ? theme.focusBg : theme.panel;
        
        // Clear row background
        for (int x = 1; x < blockListW - 1; ++x) {
            setAnchored(blockListX + x, ry, " ", fg, bg);
        }

        std::string name = blocks[idx].name;
        if (TuiUtils::calculateUtf8VisualWidth(name) > (size_t)(blockListW - 4)) {
            name = TuiUtils::trimToUtf8VisualWidth(name, blockListW - 7) + "...";
        }
        
        int nx = blockListX + 2;
        for (size_t k = 0; k < name.size(); ) {
            auto info = TuiUtils::nextUtf8Char(name, k);
            setAnchored(nx, ry, name.substr(k, info.length), fg, bg, (int)info.visualWidth);
            nx += (int)info.visualWidth;
            k += info.length;
        }
    }
}

void UnicodeTableScreen::drawCharGrid() {
    // Clear the entire grid area including the border to ensure no artifacts remain.
    surface.fillRect(gridX, gridY, gridW, gridH, theme.itemFg, theme.panel, " ");

    // Draw anchored frame
    setAnchored(gridX, gridY, kModernFrame.topLeft, theme.itemFg, theme.panel);
    setAnchored(gridX + gridW - 1, gridY, kModernFrame.topRight, theme.itemFg, theme.panel);
    setAnchored(gridX, gridY + gridH - 1, kModernFrame.bottomLeft, theme.itemFg, theme.panel);
    setAnchored(gridX + gridW - 1, gridY + gridH - 1, kModernFrame.bottomRight, theme.itemFg, theme.panel);

    for (int i = 1; i < gridW - 1; ++i) {
        setAnchored(gridX + i, gridY, kModernFrame.horizontal, theme.itemFg, theme.panel);
        setAnchored(gridX + i, gridY + gridH - 1, kModernFrame.horizontal, theme.itemFg, theme.panel);
    }
    for (int j = 1; j < gridH - 1; ++j) {
        setAnchored(gridX, gridY + j, kModernFrame.vertical, theme.itemFg, theme.panel);
        setAnchored(gridX + gridW - 1, gridY + j, kModernFrame.vertical, theme.itemFg, theme.panel);
    }

    if (selectedBlockIdx < 0 || selectedBlockIdx >= static_cast<int>(blocks.size())) return;
    const auto& block = blocks[selectedBlockIdx];
    
    // Draw anchored title
    char titleBuf[128];
    sprintf(titleBuf, " %s (%04X-%04X) ", block.name.c_str(), (uint32_t)block.start, (uint32_t)block.end);
    std::string titleStr = titleBuf;
    int tx = gridX + 2;
    for (size_t i = 0; i < titleStr.size(); ) {
        auto info = TuiUtils::nextUtf8Char(titleStr, i);
        setAnchored(tx, gridY, titleStr.substr(i, info.length), theme.title, theme.panel, (int)info.visualWidth);
        tx += (int)info.visualWidth;
        i += info.length;
    }

    int cellW = 6; // 4 for hex + 2 spacing
    int cellH = 3; // 1 for char, 1 for hex, 1 spacing
    // Reserve 1 cell padding on the left and 1 row padding below the title
    int cols = std::max(1, (gridW - 3) / cellW);
    int rows = std::max(0, (gridH - 3) / cellH);

    char32_t startChar = block.start + (gridScrollOffset * cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            char32_t cp = startChar + (r * cols + c);
            if (cp > block.end) break;

            // Shift content one cell right and one row down from the inner frame
            int cx = gridX + 2 + c * cellW;
            int cy = gridY + 2 + r * cellH;

            // Char (skip C0 control characters and DEL to avoid terminal beeps)
            std::string utf8;
            if (cp <= 0x1F || cp == 0x7F) {
                utf8 = " ";
            } else {
                utf8 = TuiUtils::encodeUtf8(cp);
            }
            
            int charX = cx + 1;
            int charY = cy;
            auto info = TuiUtils::nextUtf8Char(utf8, 0);
            setAnchored(charX, charY, utf8, theme.accent, theme.panel, (int)info.visualWidth);
            
            // Hex (show lower 4 hex digits for compact layout)
            std::stringstream ss;
            ss << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << (uint32_t)(cp & 0xFFFF);
            std::string hexStr = ss.str();
            for (size_t i = 0; i < hexStr.size(); ++i) {
                setAnchored(cx + i, cy + 1, hexStr.substr(i, 1), theme.hintFg, theme.panel);
            }
        }
    }
}

void UnicodeTableScreen::setAnchored(int px, int py, const std::string& g, const RGBColor& fg, const RGBColor& bg, int vW) {
    if (TuiCell* cell = surface.editCell(px, py)) {
        cell->glyph = "\x1b[" + std::to_string(py + 1) + ";" + std::to_string(px + 1) + "H" + g;
        cell->fg = fg;
        cell->bg = bg;
        cell->hasBg = true;
        cell->isContinuation = false;
    }
    for (int i = 1; i < vW; ++i) {
        if (TuiCell* nextCell = surface.editCell(px + i, py)) {
            nextCell->glyph = "";
            nextCell->isContinuation = true;
            nextCell->hasBg = true;
            nextCell->bg = bg;
        }
    }
}

void UnicodeTableScreen::handleKey(const InputEvent& ev, bool& running) {
    if (searchFocused) {
        if (ev.key == InputKey::Character) {
            if (ev.ch == '\b') {
                if (!searchQuery.empty()) searchQuery.pop_back();
            } else if (std::isxdigit(ev.ch)) {
                if (searchQuery.size() < 6) searchQuery.push_back(std::toupper(ev.ch));
            }
        } else if (ev.key == InputKey::Enter) {
            if (!searchQuery.empty()) {
                try {
                    char32_t code = std::stoul(searchQuery, nullptr, 16);
                    jumpToCode(code);
                } catch (...) {}
            }
            searchFocused = false;
        } else if (ev.key == InputKey::Escape) {
            searchFocused = false;
        }
        return;
    }

    if (ev.key == InputKey::Escape || (ev.key == InputKey::Character && (ev.ch == 'q' || ev.ch == 'Q'))) {
        running = false;
    } else if (ev.key == InputKey::ArrowUp) {
        if (selectedBlockIdx > 0) {
            selectedBlockIdx--;
            gridScrollOffset = 0;
            ensureBlockVisible();
        }
    } else if (ev.key == InputKey::ArrowDown) {
        if (selectedBlockIdx < static_cast<int>(blocks.size()) - 1) {
            selectedBlockIdx++;
            gridScrollOffset = 0;
            ensureBlockVisible();
        }
    } else if (ev.key == InputKey::Character && ev.ch == '\t') {
        searchFocused = !searchFocused;
    } else if (ev.key == InputKey::Character && (ev.ch == 'f' || ev.ch == 'F' || ev.ch == '/')) {
        searchFocused = true;
        searchQuery.clear();
    }
}

void UnicodeTableScreen::handleMouse(const InputEvent& ev, bool& running) {
    if (ev.wheel != 0) {
        if (ev.x >= gridX && ev.x < gridX + gridW) {
            gridScrollOffset = std::max(0, gridScrollOffset - ev.wheel);
        } else {
            blockScrollOffset = std::clamp(blockScrollOffset - ev.wheel, 0, std::max(0, (int)blocks.size() - (blockListH - 2)));
        }
    }
    
    if (ev.button == 0 && ev.pressed) {
        if (ev.y == searchY && ev.x >= searchX + 14 && ev.x < searchX + 14 + searchW) {
            searchFocused = true;
        } else if (ev.x >= blockListX && ev.x < blockListX + blockListW && ev.y > blockListY && ev.y < blockListY + blockListH - 1) {
            int idx = blockScrollOffset + (ev.y - blockListY - 1);
            if (idx >= 0 && idx < static_cast<int>(blocks.size())) {
                selectedBlockIdx = idx;
                gridScrollOffset = 0;
            }
        }
    }
}

void UnicodeTableScreen::ensureBlockVisible() {
    int visibleRows = std::max(0, blockListH - 2);
    if (selectedBlockIdx < blockScrollOffset) {
        blockScrollOffset = selectedBlockIdx;
    } else if (selectedBlockIdx >= blockScrollOffset + visibleRows) {
        blockScrollOffset = selectedBlockIdx - visibleRows + 1;
    }
}

void UnicodeTableScreen::jumpToCode(char32_t code) {
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        if (code >= blocks[i].start && code <= blocks[i].end) {
            selectedBlockIdx = i;
            ensureBlockVisible();
            
            int cellW = 6;
            int cols = std::max(1, (gridW - 3) / cellW);
            gridScrollOffset = (code - blocks[i].start) / cols;
            return;
        }
    }
}

} // namespace UI
} // namespace TilelandWorld
