Host simulation of wifi_mgr.cpp against a mock of the Arduino-ESP32 3.x Wi-Fi core behaviour.
Build & run from the LongRangeRemote folder:
  g++ -std=gnu++17 -Itools/wifi_sim/stub -I. -o wifisim tools/wifi_sim/sim.cpp wifi_mgr.cpp && ./wifisim
