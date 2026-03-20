#include <Wire.h>

const int LUX_PIN = A0;
const int ITG_ADDR = 0x68; 

float pitch = 0;
float roll = 0;
unsigned long lastTime;

int lastLux = 700; // 이전 조도 값을 저장

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  Wire.beginTransmission(ITG_ADDR);
  Wire.write(0x3E);  
  Wire.write(0x00);  
  Wire.endTransmission();
  
  Wire.beginTransmission(ITG_ADDR);
  Wire.write(0x16);  
  Wire.write(0x18);  
  Wire.endTransmission();

  lastTime = millis();
  delay(100);
}

void loop() {
  unsigned long currentTime = millis();
  float dt = (currentTime - lastTime) / 1000.0;
  lastTime = currentTime;

  int rawLux = analogRead(LUX_PIN);
  int finalLux;

  // 💡 스파이크 킬러 로직: 
  // 이전 값(lastLux)보다 갑자기 200 이상 훅 떨어지면 노이즈로 간주하고 이전 값을 유지함
  if (lastLux > 500 && rawLux < 200) {
      finalLux = lastLux; 
  } else {
      finalLux = rawLux;
      lastLux = rawLux;
  }

  Wire.beginTransmission(ITG_ADDR);
  Wire.write(0x1D);  
  Wire.endTransmission();
  
  Wire.requestFrom(ITG_ADDR, 6);

  if (Wire.available() >= 6) {
    int16_t GyX = Wire.read() << 8 | Wire.read();  
    int16_t GyY = Wire.read() << 8 | Wire.read();  
    int16_t GyZ = Wire.read() << 8 | Wire.read();  

    float rateX = GyX / 14.375;
    float rateY = GyY / 14.375;

    // 최종 판정된 finalLux를 기준으로 제어
    if (finalLux < 300) {
        pitch += rateX * dt;
        roll += rateY * dt;
    } else {
        pitch = 0;
        roll = 0;
    }

    Serial.print(pitch);
    Serial.print(",");
    Serial.print(roll);
    Serial.print(",");
    Serial.println(finalLux); 
  }
  
  delay(50);
}
