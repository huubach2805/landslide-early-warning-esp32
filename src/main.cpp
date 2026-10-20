/* Thong tin Blynk va WiFi nam trong include/secrets.h (khong dua len GitHub) */
#define BLYNK_PRINT Serial
#include "secrets.h"

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

/* Chan theo so do breadboard */
#define SDA_PIN     8
#define SCL_PIN     9
#define SOIL_PIN    4
#define RAIN_PIN    5
#define BUZZER_PIN  6

#define MPU_ADDR    0x68
#define OLED_ADDR   0x3C

/* Nguong canh bao */
#define NGUONG_LECH_GOC  10.0f
#define NGUONG_AM_DAT    60
#define NGUONG_MUA       50
#define COOLDOWN_MS      600000UL   /* 10 phut */
#define TAN_SO_COI       2000       /* Hz */
#define CHU_KY_BLYNK_MS  2000UL     /* gui len Blynk moi 2 giay */

/* Hieu chuan: thay bang gia tri tho do duoc (kho / uot) */
#define DAT_KHO  4095
#define DAT_UOT  1200
#define MUA_KHO  4095
#define MUA_UOT  1200

Adafruit_SSD1306 oled(128, 64, &Wire, -1);

/* Bien toan cuc */
static float gocChuanX, gocChuanY, gocChuanZ;   /* huong trong luc luc lap dat */
static float lechGoc = 0;
static int   amDat = 0;
static int   mua = 0;
static bool  nguyHiem = false;
static bool  dangCanhBao = false;
static bool  daCanhBao = false;
static bool  coiBat = false;
static unsigned long lanCanhBaoTruoc = 0;
static unsigned long lanGuiBlynk = 0;

/* ---------- MPU6050 ---------- */
static void mpuGhi(uint8_t thanhGhi, uint8_t giaTri) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(thanhGhi);
  Wire.write(giaTri);
  Wire.endTransmission();
}

static int16_t docInt16(void) {
  uint8_t cao  = Wire.read();     /* byte cao doc truoc */
  uint8_t thap = Wire.read();     /* byte thap doc sau  */
  return (int16_t)((cao << 8) | thap);
}

/* Doc gia toc 10 lan roi lay trung binh, tra ket qua qua con tro */
static void docGiaToc(float *ax, float *ay, float *az) {
  float sx = 0, sy = 0, sz = 0;
  for (int i = 0; i < 10; i++) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6);
    sx += docInt16() / 16384.0f;
    sy += docInt16() / 16384.0f;
    sz += docInt16() / 16384.0f;
    delay(2);
  }
  *ax = sx / 10;
  *ay = sy / 10;
  *az = sz / 10;
}

/* Goc giua huong trong luc bay gio va luc dau (0 - 90 do) */
static float tinhGocNghieng(float ax, float ay, float az) {
  float tich  = ax * gocChuanX + ay * gocChuanY + az * gocChuanZ;
  float doDai = sqrtf(ax * ax + ay * ay + az * az) *
                sqrtf(gocChuanX * gocChuanX + gocChuanY * gocChuanY + gocChuanZ * gocChuanZ);
  if (doDai == 0) return 0;

  float cosGoc = tich / doDai;
  if (cosGoc > 1)  cosGoc = 1;
  if (cosGoc < -1) cosGoc = -1;

  float goc = acosf(cosGoc) * 180.0f / PI;
  if (goc > 90) goc = 90;
  return goc;
}

/* ---------- Cam bien analog ---------- */
static int phanTram(int raw, int rawKho, int rawUot) {
  long p = map(raw, rawKho, rawUot, 0, 100);
  if (p < 0)   p = 0;
  if (p > 100) p = 100;
  return (int)p;
}

/* ---------- Cac buoc trong loop ---------- */
static void docCamBien(void) {
  float ax, ay, az;
  docGiaToc(&ax, &ay, &az);
  lechGoc = tinhGocNghieng(ax, ay, az);

  amDat = phanTram(analogRead(SOIL_PIN), DAT_KHO, DAT_UOT);
  mua   = phanTram(analogRead(RAIN_PIN), MUA_KHO, MUA_UOT);

  nguyHiem = (lechGoc >= NGUONG_LECH_GOC) && (amDat >= NGUONG_AM_DAT) && (mua >= NGUONG_MUA);
}

static void dieuKhienCoi(void) {
  if (nguyHiem) {
    coiBat = !coiBat;           /* dao trang thai moi vong = bip-bip */
  } else {
    coiBat = false;
  }
  if (coiBat) {
    tone(BUZZER_PIN, TAN_SO_COI);
  } else {
    noTone(BUZZER_PIN);
  }
}

static void hienThongBao(const char *dong1, const char *dong2) {
  oled.clearDisplay();
  oled.setCursor(0, 20);
  oled.println(dong1);
  oled.println(dong2);
  oled.display();
}

static void hienThiOLED(void) {
  char dong[24];

  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.println("GIAM SAT SAT LO DAT");

  oled.setCursor(0, 16);
  snprintf(dong, sizeof(dong), "Nghieng : %.1f do", lechGoc);
  oled.println(dong);
  snprintf(dong, sizeof(dong), "Am dat  : %d %%", amDat);
  oled.println(dong);
  snprintf(dong, sizeof(dong), "Mua     : %d %%", mua);
  oled.println(dong);

  oled.setCursor(0, 50);
  oled.println(nguyHiem ? ">> CANH BAO SAT LO <<" : "TRANG THAI: AN TOAN");
  oled.display();
}

static void guiBlynk(void) {
  if (millis() - lanGuiBlynk < CHU_KY_BLYNK_MS) return;
  lanGuiBlynk = millis();

  Blynk.virtualWrite(V0, lechGoc);
  Blynk.virtualWrite(V1, amDat);
  Blynk.virtualWrite(V2, mua);
  Blynk.virtualWrite(V3, nguyHiem ? 1 : 0);
}

/* Gui canh bao 1 lan khi vua chuyen sang nguy hiem, cach nhau it nhat 10 phut */
static void guiCanhBao(void) {
  if (nguyHiem && !dangCanhBao) {
    if (!daCanhBao || millis() - lanCanhBaoTruoc > COOLDOWN_MS) {
      Serial.println(">>> PHAT CANH BAO KHAN CAP <<<");
      Blynk.logEvent("canh_bao_sat_lo", "Phat hien nguy co sat lo!");
      lanCanhBaoTruoc = millis();
      daCanhBao = true;
    }
  }
  dangCanhBao = nguyHiem;
}

/* ---------- Chuong trinh chinh ---------- */
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  mpuGhi(0x6B, 0x00);   /* danh thuc MPU6050 */
  mpuGhi(0x1A, 0x04);   /* bat bo loc thong thap co san */

  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  hienThongBao("Dang ket noi", "WiFi + Blynk...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  hienThongBao("Dang lay goc chuan", "Giu coc dung yen");
  delay(1000);
  docGiaToc(&gocChuanX, &gocChuanY, &gocChuanZ);
}

void loop() {
  Blynk.run();

  docCamBien();
  dieuKhienCoi();
  hienThiOLED();

  Serial.printf("Nghieng: %.1f do | Am dat: %d%% | Mua: %d%% | %s\n",
                lechGoc, amDat, mua, nguyHiem ? "CANH BAO" : "AN TOAN");

  guiBlynk();
  guiCanhBao();

  delay(300);
}
