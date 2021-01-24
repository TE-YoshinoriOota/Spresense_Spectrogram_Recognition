#ifdef SUBCORE
#error "Core selection is wrong!!"
#endif

#include <MP.h>
#include <MPMutex.h>
#include <SDHCI.h>
#include <pthread.h>
#define SUBCORE1 1

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

#include <Audio.h>
#include <FFT.h>
#include <IIR.h>
#include <DNNRT.h>
#define FFT_LEN  128
#define CHANNEL_NUM 1


#define SMA_ENABLE
#define SMA_WINDOW 8


FFTClass<CHANNEL_NUM, FFT_LEN> FFT;
SDClass theSD;

#define DNN_ENABLE
#ifdef DNN_ENABLE
DNNRT dnnrt;
#endif

AudioClass *theAudio = AudioClass::getInstance();
static const int32_t buffer_size = FFT_LEN * sizeof(int16_t);
static const uint32_t sampling_rate = AS_SAMPLINGRATE_16000;
static char buff[buffer_size];

static float pDst[FFT_LEN];
static float pOut[FFT_LEN];

#define DATA_WIDTH (39)
#define DATA_HEIGHT (24)

static uint8_t addr[DATA_HEIGHT][DATA_WIDTH];


#ifdef SMA_ENABLE
static float pSMA[SMA_WINDOW][FFT_LEN];

void applySMA(float sma[SMA_WINDOW][FFT_LEN], float dst[FFT_LEN]) 
{
  int i, j;
  static int g_counter = 0;
  if (g_counter == SMA_WINDOW) g_counter = 0;
  for (i = 0; i < FFT_LEN; ++i) {
    sma[g_counter][i] = dst[i];
    float sum = 0;
    for (j = 0; j < SMA_WINDOW; ++j) {
      sum += sma[j][i];
    }
    dst[i] = sum / SMA_WINDOW;
  }
  ++g_counter;
}
#endif

static void audioReadFrames() {
  int err, ret;
  uint32_t read_size;
  static const unsigned long wait_usec =  (double)(FFT_LEN)/sampling_rate*1000000;
  // Serial.println(wait_usec);
  
  while(1) {
    err = theAudio->readFrames(buff, buffer_size, &read_size);
    if (err != AUDIOLIB_ECODE_OK && err != AUDIOLIB_ECODE_INSUFFICIENT_BUFFER_AREA) {
      Serial.println("Error err = " + String(err));
      theAudio->stopRecorder();
      exit(1);
    }

    if (read_size < buffer_size) {
      usleep(wait_usec);
      continue;
    }
    
    FFT.put((q15_t*)buff, FFT_LEN);

    // Using mutex to protect pDst array
    if (pthread_mutex_lock(&m) != 0) Serial.println("Mutex Lock Error");
    FFT.get(pDst, 0);
#ifdef SMA_ENABLE
    applySMA(pSMA, pDst);
#endif 
    if (pthread_mutex_unlock(&m) != 0) Serial.println("Mutex UnLock Error");
  }
}

void setup() {
  int8_t sndid; 
  Serial.begin(115200);
  theSD.begin();
  memset(addr, 0x00, sizeof(addr));

  setupLCD();
#ifdef DNN_ENABLE
  File nnbfile = theSD.open("model.nnb");
  int ret = dnnrt.begin(nnbfile);
  if (ret < 0) {
    Serial.println("Runtime initialization failure: " + String(ret));
    while(1);
  }
#endif

  FFT.begin(WindowRectangle, CHANNEL_NUM, (FFT_LEN/2));
  
  theAudio->begin();
  theAudio->setRecorderMode(AS_SETRECDR_STS_INPUTDEVICE_MIC, 50);
  int err = theAudio->initRecorder(AS_CODECTYPE_PCM ,"/mnt/sd0/BIN" 
                           ,sampling_rate ,AS_CHANNEL_MONO);                             
  if (err != AUDIOLIB_ECODE_OK) {
    Serial.println("Recorder initialize error");
    while(1);
  }

  theAudio->startRecorder(); 
  Serial.println("Start Recording");
  task_create("audio recording", 120, 1024, audioReadFrames, NULL);
  sleep(1);
}

#define AVERAGE_NUM (8)
//#define SAVE_DEBUGIMG

static int file_num = 0;
static int average_counter = 0;
static int launch_counter = 0;
static int shutter_counter = 0;
static bool trigger = false;

uint16_t average[AVERAGE_NUM];
uint16_t sp_map[DATA_HEIGHT];

#ifdef DNN_ENABLE
static DNNVariable input(DATA_HEIGHT * DATA_WIDTH);
#endif

void loop() {
  int err, ret;
  int8_t sndid;

  // Using mutex to protect pDst array
  if (pthread_mutex_lock(&m) != 0) Serial.println("Mutex Lock Error");
  memcpy(pOut, pDst, FFT_LEN*sizeof(float));
  if (pthread_mutex_unlock(&m) != 0) Serial.println("Mutex UnLock Error");

  for (int i = 1; i < DATA_HEIGHT; ++i) {
    for (int j = 0; j < DATA_WIDTH; ++j) {
      addr[i-1][j] = addr[i][j];
    }
  }

  float f_max = -1000.0;
  float f_min =  1000.0;
  for (int i = 0; i < FFT_LEN/2; ++i) {
    uint8_t val;
    float f_val = abs(pOut[i]) * 255.0; 
    if (f_val > 255.0) val = 255;
    else val = (uint8_t)f_val;
    pOut[i] = val;
    if (i < DATA_WIDTH) addr[DATA_HEIGHT-1][i] = val;
  }  
  //clearResult();
  DispLCD(pOut);

  
  /* calculate power */
  for (int i = 0; i < DATA_HEIGHT; ++i) {
    uint16_t sum = 0;  
    for (int j = 0; j < DATA_WIDTH; ++j) {
      sum += addr[i][j];
    }
    sum /= DATA_WIDTH;
    sp_map[i] = sum;
  }

  
  int16_t sigmon = 0;
  for (int i = 6; i < 11; ++i) {
    sigmon += sp_map[i];
  }
  sigmon /= 5;

  int16_t ave = 0;
  for (int i = 0; i < AVERAGE_NUM; ++i) {
    ave += average[i];
  }
  ave /= AVERAGE_NUM;


  int16_t diff = sigmon - ave;
  // Serial.println(diff);

  average[average_counter] = sigmon;
  ++average_counter;
  if (average_counter >= AVERAGE_NUM) average_counter = 0;

  if (launch_counter < 50) {
    ++launch_counter;
    return;
  }

  // auto detection
  if (diff > 10 && trigger == false) {
    trigger = true;
    return;
  } else if (diff <= 10) {
    trigger = false;
    shutter_counter = 0;
    return;
  } else if (trigger == true && diff >= 10) {
    ++shutter_counter;
    if (shutter_counter < 4) {
      return;
    }
  }
  
  shutter_counter = 0;
  trigger = false; 


  Serial.println("###### Start dnn ######");

#ifdef DNN_ENABLE
  float *dnnbuf = input.data();
  int n = 0;
  for (int j = DATA_WIDTH-1; j >= 0; --j) {
    for (int i = 0; i < DATA_HEIGHT; ++i) {
      dnnbuf[n] = (float)addr[i][j]/255.0;
      ++n;
    }
  }
#endif

#ifdef SAVE_DEBUGIMG
  char filename[16] = {0};
  memset(filename, 0, sizeof(filename));
  sprintf(filename, "S%03d.pgm", file_num);
  Serial.println(filename);
  ++file_num;
  if (theSD.exists(filename)) theSD.remove(filename);
  File myFile = theSD.open(filename, FILE_WRITE);
  myFile.println("P2");
  myFile.println(String(DATA_HEIGHT) + " " + String(DATA_WIDTH));

  myFile.println("255");  
  for (int j = DATA_WIDTH-1; j >= 0; --j) {
    for (int i = 0; i < DATA_HEIGHT; ++i) {
      uint8_t val = addr[i][j];
      myFile.print(String(val) + " ");
    }
    myFile.println();
  }
  myFile.close();
#else
  usleep(10000);
#endif

#ifdef DNN_ENABLE
  dnnrt.inputVariable(input, 0);
  dnnrt.forward();
  DNNVariable output = dnnrt.outputVariable(0); 
  int index = output.maxIndex();
  Serial.println("Recognition :" + String(index));
  Serial.println("value " + String(output[index]));
  printResult(index, output[index]);
#endif
}
