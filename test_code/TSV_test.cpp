#include <Arduino.h>

String tsvToFieldString(const String &tsvLine)
{
  String out;
  int start = 0, fieldNo = 1;

  while (start < tsvLine.length()) {
    int end = tsvLine.indexOf('\t', start);    // next TAB
    if (end == -1) end = tsvLine.length();     // last value

    out += "field";
    out += fieldNo++;
    out += '=';
    out += tsvLine.substring(start, end);

    if (end < tsvLine.length()) out += '&';    // no '&' after last field
    start = end + 1;                           // jump past TAB
  }
  return out;
}

void setup() {
  SerialUSB.begin(115200);
  while (!SerialUSB);

  String line = "27.3\t41.8\t500.12";          // ← example TSV record
  String qs   = tsvToFieldString(line);

  SerialUSB.println("{"+qs+"}");
  // → field1=27.3&field2=41.8&field3=500.12
}

void loop() {}
