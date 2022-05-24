void setup() {
  pinMode(2, OUTPUT);
}

void loop() {
  digitalWrite(2, LOW);
  delay(10);
  digitalWrite(2, HIGH);
  delay(200);
  digitalWrite(2, LOW);
  delay(10);
  digitalWrite(2, HIGH);
  delay(1000);

}
