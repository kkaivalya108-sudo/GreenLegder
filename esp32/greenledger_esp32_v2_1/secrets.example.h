#pragma once

// Copy this file to secrets.h and fill in your own credentials.
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* THINGSPEAK_WRITE_API_KEY = "YOUR_THINGSPEAK_WRITE_API_KEY";
const char* AI_ESTIMATE_URL = "https://YOUR-RENDER-SERVICE.onrender.com/api/ai/estimate";

// Farm inputs required by the ML model. Replace with values for this farm.
const float FARM_NDVI = -1.0f;                 // valid range 0.0–1.0
const float FARM_AREA_HECTARES = -1.0f;        // must be greater than 0
const float FARM_SOIL_ORGANIC_CARBON = -1.0f;  // measured SOC percentage
const int FARM_MONITORING_MONTHS = 6;
const char* FARM_PRACTICE = "conventional";
const char* FARM_CROP_TYPE = "other";
