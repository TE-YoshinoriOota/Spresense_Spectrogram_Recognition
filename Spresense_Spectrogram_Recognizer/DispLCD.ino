#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define CS  10
#define DC   9   
#define RST  8
Adafruit_ILI9341 tft = Adafruit_ILI9341(CS, DC, RST);

#define SPECTRO_WIDTH  (64)
#define SPECTRO_HEIGHT (320)
static uint16_t frameBuffer[SPECTRO_HEIGHT][SPECTRO_WIDTH];


void setupLCD() {
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(35, 210);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.println("FFT Spectrogram Viewer");
  tft.setRotation(2);
  memset(frameBuffer, 255, SPECTRO_WIDTH*SPECTRO_HEIGHT*sizeof(uint16_t));
}

void clearResult() {
  tft.setRotation(3);
  tft.fillRect(35, 80, 140, 30, ILI9341_BLACK);  
  tft.setRotation(2);  
}

void printResult(int index, float probability) {
  tft.setRotation(3);

  tft.fillRect(35, 80, 140, 30, ILI9341_BLACK);  
  tft.setCursor(35, 80);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  
  if (index == 0) {
    tft.println("ZERO");    
  } else if (index == 1) {
    tft.println("NINE");
  } else if (index == 2) {
    tft.println("NOISE");
  }
  tft.setCursor(110, 80);
  tft.setTextColor(ILI9341_GREEN);
  tft.println(String(probability));
  tft.setRotation(2);
}

void DispLCD(float *data) {

  int i, j;
 
  for (i = 1; i < SPECTRO_HEIGHT; ++i) {
    for (j = 0; j < SPECTRO_WIDTH; ++j) {
      frameBuffer[i-1][j] = frameBuffer[i][j];
    }
  }

  // display range:0:0Hz - 200:9.375kHz
  for (i = 0; i < SPECTRO_WIDTH; ++i) {
    uint16_t val_6 = (uint16_t)(data[i]) * 64 / 256;
    uint16_t val_5 = (uint16_t)(data[i]) * 32 / 256;
    uint16_t val = val_5 << 11 | val_6 << 5 | val_5; 
    frameBuffer[SPECTRO_HEIGHT-1][i] = val;
  }
  
  tft.drawRGBBitmap(40, 0, (uint16_t*)frameBuffer, SPECTRO_WIDTH, SPECTRO_HEIGHT);
  tft.drawRect(40, 320-DATA_HEIGHT, DATA_WIDTH, DATA_HEIGHT, ILI9341_RED);
}
