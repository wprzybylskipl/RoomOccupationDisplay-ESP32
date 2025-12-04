// ----------------------------------------------------
// Freenove ESP32-32E 4.0" LCD (320x480) SPI ST7796 + XPT2046
// ----------------------------------------------------
#define USER_SETUP_INFO "ESP32-32E_ST7796_SPI"

// ---- Display Driver ----
#define ST7796_DRIVER
//#define ILI9488_DRIVER   // leave commented

// Optional but recommended:
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
#define TFT_RGB_ORDER TFT_BGR   // typowe dla ST7796

// ---- SPI PINS ----
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1    // Uses EN pin

// ---- Backlight ----
#define TFT_BL   27
#define TFT_BACKLIGHT_ON HIGH

// ---- Touch ----
#define TOUCH_CS  33
#define TOUCH_IRQ 36
#define SPI_TOUCH_FREQUENCY 2500000

// ---- Fonts ----
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT

// ---- SPI Clock ----
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
