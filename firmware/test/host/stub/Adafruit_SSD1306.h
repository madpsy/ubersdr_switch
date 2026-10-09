#pragma once
// Host-side SSD1306 stand-in that RECORDS what is drawn. clearDisplay() starts a new
// frame; display() publishes it to g_frames, so a test can read what the panel shows:
// every text item with its position and font ("12pt", "9pt", "size1", "size2"), plus
// pixel/line/rect counts (the padlock, the screen-saver dot, the banner border).
#include "Adafruit_GFX.h"
#include "Wire.h"
#include <vector>

#define SSD1306_SWITCHCAPVCC 0x02
#define SSD1306_WHITE 1
#define SSD1306_BLACK 0

struct OledText { std::string text, font; int x, y; };
struct OledFrame {
    std::vector<OledText> texts;
    int pixels = 0, lines = 0, rects = 0, fills = 0;
    std::string all() const { std::string s; for (auto &t : texts) { if (!s.empty()) s += " | "; s += t.text; } return s; }
    bool has(const std::string &t) const { for (auto &x : texts) if (x.text == t) return true; return false; }
    const OledText *find(const std::string &t) const { for (auto &x : texts) if (x.text == t) return &x; return nullptr; }
    bool blank() const { return texts.empty() && !pixels && !lines && !rects && !fills; }
};
extern std::vector<OledFrame> g_frames;     // every frame pushed with display()
extern bool g_oledBeginOk;
extern uint8_t g_oledBeginAddr;

class Adafruit_SSD1306 {
public:
    Adafruit_SSD1306(int w, int h, TwoWire *, int rst) : _w(w), _h(h), _rst(rst) {}
    bool begin(int, uint8_t addr, bool = true, bool = true) { g_oledBeginAddr = addr; return g_oledBeginOk; }
    void setTextColor(int) {}
    void cp437(bool) {}
    void setFont(const GFXfont *f) { _font = f; }
    void setTextSize(int s) { _size = s; }
    void setCursor(int x, int y) { _x = x; _y = y; }
    void getTextBounds(const char *t, int, int, int16_t *bx, int16_t *by, uint16_t *bw, uint16_t *bh) {
        int n = (int)strlen(t);
        if (_font) { *bx = 0; *by = (int16_t)-_font->height; *bw = (uint16_t)(n * _font->charW); *bh = (uint16_t)_font->height; }
        else { *bx = 0; *by = 0; *bw = (uint16_t)(n * 6 * _size); *bh = (uint16_t)(8 * _size); }
    }
    void print(const char *t) { _cur.texts.push_back({t, _font ? _font->name : (_size == 2 ? "size2" : _size == 1 ? "size1" : "size" + std::to_string(_size)), _x, _y}); }
    void print(const String &t) { print(t.c_str()); }
    void clearDisplay() { _cur = OledFrame(); }
    void display() { g_frames.push_back(_cur); }
    void drawPixel(int, int, int) { _cur.pixels++; }
    void drawLine(int, int, int, int, int) { _cur.lines++; }
    void drawRect(int, int, int, int, int) { _cur.rects++; }
    void fillRect(int, int, int, int, int) { _cur.fills++; }
    void invertDisplay(bool) {}
    void dim(bool) {}
    int rstPin() const { return _rst; }
private:
    int _w, _h, _rst, _x = 0, _y = 0, _size = 1;
    const GFXfont *_font = nullptr;
    OledFrame _cur;
};
