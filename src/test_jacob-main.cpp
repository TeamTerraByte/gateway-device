// src/test_jacob-main.cpp

#include <Arduino.h>
#include <unity.h>

// Mock global variables as in jacob-main.cpp
String moistBuf = "";
String tempBuf  = "";
String curType  = "";
bool   assembling = false;

// Prototype for the function under test
void processChunk(const String& data);

// Test: Moist header initializes moistBuf and state
void test_processChunk_moist_header() {
    moistBuf = "";
    tempBuf = "";
    curType = "";
    assembling = false;
    processChunk("Moist,123,456,");
    TEST_ASSERT_EQUAL_STRING("Moist,123,456,", moistBuf.c_str());
    TEST_ASSERT_EQUAL_STRING("", tempBuf.c_str());
    TEST_ASSERT_EQUAL_STRING("Moist", curType.c_str());
    TEST_ASSERT_TRUE(assembling);
}

// Test: Temp header initializes tempBuf and state
void test_processChunk_temp_header() {
    moistBuf = "";
    tempBuf = "";
    curType = "";
    assembling = false;
    processChunk("Temp,22,33,");
    TEST_ASSERT_EQUAL_STRING("", moistBuf.c_str());
    TEST_ASSERT_EQUAL_STRING("Temp,22,33,", tempBuf.c_str());
    TEST_ASSERT_EQUAL_STRING("Temp", curType.c_str());
    TEST_ASSERT_TRUE(assembling);
}

// Test: Continuation chunk appends to moistBuf
void test_processChunk_moist_continuation() {
    moistBuf = "Moist,1,2,";
    tempBuf = "";
    curType = "Moist";
    assembling = true;
    processChunk("3,4,");
    TEST_ASSERT_EQUAL_STRING("Moist,1,2,3,4,", moistBuf.c_str());
    TEST_ASSERT_TRUE(assembling);
}

// Test: Continuation chunk appends to tempBuf
void test_processChunk_temp_continuation() {
    moistBuf = "";
    tempBuf = "Temp,10,20,";
    curType = "Temp";
    assembling = true;
    processChunk("30,40,");
    TEST_ASSERT_EQUAL_STRING("Temp,10,20,30,40,", tempBuf.c_str());
    TEST_ASSERT_TRUE(assembling);
}

// Test: Short chunk (<15 chars) ends assembling
void test_processChunk_short_chunk_ends_assembling() {
    moistBuf = "Moist,1,2,";
    tempBuf = "";
    curType = "Moist";
    assembling = true;
    processChunk("end");
    TEST_ASSERT_FALSE(assembling);
}

// Test: Unrelated data does not modify buffers if not assembling
void test_processChunk_unrelated_data() {
    moistBuf = "Moist,1,2,";
    tempBuf = "Temp,3,4,";
    curType = "";
    assembling = false;
    processChunk("RandomData");
    TEST_ASSERT_EQUAL_STRING("Moist,1,2,", moistBuf.c_str());
    TEST_ASSERT_EQUAL_STRING("Temp,3,4,", tempBuf.c_str());
    TEST_ASSERT_FALSE(assembling);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_processChunk_moist_header);
    RUN_TEST(test_processChunk_temp_header);
    RUN_TEST(test_processChunk_moist_continuation);
    RUN_TEST(test_processChunk_temp_continuation);
    RUN_TEST(test_processChunk_short_chunk_ends_assembling);
    RUN_TEST(test_processChunk_unrelated_data);
    UNITY_END();
}

void loop() {
    // not used
}