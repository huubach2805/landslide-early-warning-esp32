// Thong tin Blynk va WiFi nam trong include/secrets.h (khong dua len GitHub)
#define BLYNK_PRINT Serial
#include "secrets.h"

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Chan theo so do breadboard
#define SDA_PIN     8
#define SCL_PIN     9
#define SOIL_PIN    4
#define RAIN_PIN    5
#define BUZZER_PIN  6

#define MPU_ADDR    0x68
#define OLED_ADDR   0x3C

// Nguong canh bao
#define NGUONG_LECH_GOC  10.0
#define NGUONG_AM_DAT    60
#define NGUONG_MUA       50
#define COOLDOWN_MS      600000   // 10 phut
#define TAN_SO_COI       2000     // Hz
#define CHU_KY_BLYNK_MS  2000     // gui du lieu len Blynk moi 2 giay

Adafruit_SSD1306 oled(128, 64, &Wire, -1);

float gx, gy, gz;          // huong trong luc luc moi lap dat (goc chuan)
bool dangCanhBao = false;
bool daCanhBao = false;
bool coiBat = false;
unsigned long lanCanhBaoTruoc = 0;
unsigned long lanGuiBlynk = 0;

// Doc gia toc 10 lan roi lay trung binh de giam nhieu
void docGiaToc(float &ax, float &ay, float &az) {
  ax = 0; ay = 0; az = 0;
  for (int i = 0; i < 10; i++) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6);

    int16_t rawX = (Wire.read() << 8) | Wire.read();
    int16_t rawY = (Wire.read() << 8) | Wire.read();
    int16_t rawZ = (Wire.read() << 8) | Wire.read();

    ax += rawX / 16384.0;
    ay += rawY / 16384.0;
    az += rawZ / 16384.0;
    delay(2);
  }
  ax /= 10; ay /= 10; az /= 10;
}

// Goc giua huong trong luc bay gio va huong trong luc luc dau (0 - 90 do)
float tinhGocNghieng(float ax, float ay, float az) {
  float tich  = ax * gx + ay * gy + az * gz;
  float doDai = sqrt(ax * ax + ay * ay + az * az) * sqrt(gx * gx + gy * gy + gz * gz);
  if (doDai == 0) return 0;

  float cosGoc = constrain(tich / doDai, -1.0, 1.0);
  float goc = acos(cosGoc) * 180 / PI;
  if (goc > 90) goc = 90;
  return goc;
}

int phanTram(int raw) {
  int p = map(raw, 4095, 1200, 0, 100);
  return constrain(p, 0, 100);
}

void hienThongBao(const char *dong1, const char *dong2) {
  oled.clearDisplay();
  oled.setCursor(0, 20);
  oled.println(dong1);
  oled.println(dong2);
  oled.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  // Danh thuc MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission();

  // Bat bo loc thong thap co san trong MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1A);
  Wire.write(0x04);
  Wire.endTransmission();

  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Ket noi WiFi va Blynk (cho den khi ket noi duoc)
  hienThongBao("Dang ket noi", "WiFi + Blynk...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Lay goc chuan: luc nay coc phai dung yen
  hienThongBao("Dang lay goc chuan", "Giu coc dung yen");
  delay(1000);
  docGiaToc(gx, gy, gz);
}

void loop() {
  Blynk.run();

  // 1. Doc goc nghieng
  float ax, ay, az;
  docGiaToc(ax, ay, az);
  float lechGoc = tinhGocNghieng(ax, ay, az);

  // 2. Doc do am dat va mua
  int amDat = phanTram(analogRead(SOIL_PIN));
  int mua   = phanTram(analogRead(RAIN_PIN));

  // 3. Kiem tra 3 dieu kien
  bool nguyHiem = (lechGoc >= NGUONG_LECH_GOC) && (amDat >= NGUONG_AM_DAT) && (mua >= NGUONG_MUA);

  // 4. Coi keu bip-bip khi nguy hiem
  if (nguyHiem) {
    coiBat = !coiBat;
  } else {
    coiBat = false;
  }
  if (coiBat) {
    tone(BUZZER_PIN, TAN_SO_COI);
  } else {
    noTone(BUZZER_PIN);
  }

  // 5. In ra Serial
  Serial.printf("Nghieng: %.1f do | Am dat: %d%% | Mua: %d%% | %s\n",
                lechGoc, amDat, mua, nguyHiem ? "CANH BAO" : "AN TOAN");

  // 6. Hien thi OLED
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.println("GIAM SAT SAT LO DAT");
  oled.setCursor(0, 16);
  oled.print("Nghieng : "); oled.print(lechGoc, 1); oled.println(" do");
  oled.print("Am dat  : "); oled.print(amDat); oled.println(" %");
  oled.print("Mua     : "); oled.print(mua); oled.println(" %");
  oled.setCursor(0, 50);
  oled.println(nguyHiem ? ">> CANH BAO SAT LO <<" : "TRANG THAI: AN TOAN");
  oled.display();

  // 7. Gui du lieu len Blynk moi 2 giay
  if (millis() - lanGuiBlynk >= CHU_KY_BLYNK_MS) {
    lanGuiBlynk = millis();
    Blynk.virtualWrite(V0, lechGoc);
    Blynk.virtualWrite(V1, amDat);
    Blynk.virtualWrite(V2, mua);
    Blynk.virtualWrite(V3, nguyHiem ? 1 : 0);
  }

  // 8. Gui canh bao 1 lan khi vua chuyen sang nguy hiem, cach nhau 10 phut
  if (nguyHiem && !dangCanhBao) {
    if (!daCanhBao || millis() - lanCanhBaoTruoc > COOLDOWN_MS) {
      Serial.println(">>> PHAT CANH BAO KHAN CAP <<<");
      Blynk.logEvent("canh_bao_sat_lo", "Phat hien nguy co sat lo!");
      lanCanhBaoTruoc = millis();
      daCanhBao = true;
    }
  }
  dangCanhBao = nguyHiem;

  delay(300);
}
