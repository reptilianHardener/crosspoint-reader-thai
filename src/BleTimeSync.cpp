#include "BleTimeSync.h"

#include <Arduino.h>
#include <Logging.h>

// The phone writes the time to a GATT server here, which needs NimBLE's peripheral role.
// The page-turner build turns that role off to save RAM, so sync() is then a no-op.
#if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED)

bool BleTimeSync::sync(uint32_t /*timeoutMs*/) {
  LOG_DBG("BLE", "Time sync unavailable: NimBLE peripheral role disabled");
  return false;
}

bool BleTimeSync::isSynced() { return false; }

#else

#include <NimBLEDevice.h>
#include <sys/time.h>

#include <cstring>

namespace {

// Custom service & characteristic UUIDs
constexpr char TIME_SERVICE_UUID[] = "cfa8a100-7b98-4044-a656-580f213d8cf0";
constexpr char TIME_CHAR_UUID[] = "cfa8a101-7b98-4044-a656-580f213d8cf0";

// Written from the NimBLE host task, polled from the caller's task.
volatile bool sTimeReceived = false;
bool sSynced = false;

// GATT callback: the phone writes a 4-byte little-endian Unix timestamp.
class TimeWriteCallback final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& /*connInfo*/) override {
    const NimBLEAttValue value = characteristic->getValue();
    if (value.size() < sizeof(uint32_t)) {
      return;
    }
    // memcpy, not a cast: the attribute buffer carries no alignment guarantee and the
    // C3 faults on unaligned 32-bit loads.
    uint32_t timestamp = 0;
    memcpy(&timestamp, value.data(), sizeof(timestamp));

    struct timeval tv = {};
    tv.tv_sec = static_cast<time_t>(timestamp);
    settimeofday(&tv, nullptr);

    sTimeReceived = true;
    sSynced = true;
    LOG_INF("BLE", "Time synced: %lu", static_cast<unsigned long>(timestamp));
  }
};

TimeWriteCallback sCallback;

}  // namespace

bool BleTimeSync::sync(const uint32_t timeoutMs) {
  sTimeReceived = false;

  LOG_DBG("BLE", "Starting BLE time sync (timeout %lu ms)", static_cast<unsigned long>(timeoutMs));

  if (!NimBLEDevice::init("CrossPoint")) {
    LOG_ERR("BLE", "NimBLE init failed");
    return false;
  }

  // A nearby phone is the only peer: +3 dBm is plenty and keeps the radio's draw down.
  NimBLEDevice::setPower(3);

  NimBLEServer* server = NimBLEDevice::createServer();
  NimBLEService* service = server ? server->createService(TIME_SERVICE_UUID) : nullptr;
  NimBLECharacteristic* characteristic =
      service ? service->createCharacteristic(TIME_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)
              : nullptr;
  if (!characteristic) {
    LOG_ERR("BLE", "Failed to create time sync service");
    NimBLEDevice::deinit(true);
    return false;
  }
  characteristic->setCallbacks(&sCallback);
  // No service->start(): NimBLE 2.x starts services with the server, on first advertise.

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(TIME_SERVICE_UUID);
  advertising->enableScanResponse(true);
  advertising->start();

  LOG_DBG("BLE", "Advertising as 'CrossPoint', waiting for time...");

  const unsigned long start = millis();
  while (!sTimeReceived && (millis() - start) < timeoutMs) {
    delay(50);
  }

  // Fully shut BLE down again: the controller and host hold tens of KB of heap.
  advertising->stop();
  NimBLEDevice::deinit(true);

  if (sTimeReceived) {
    LOG_INF("BLE", "Time sync successful");
  } else {
    LOG_DBG("BLE", "Time sync timed out");
  }

  return sTimeReceived;
}

bool BleTimeSync::isSynced() { return sSynced; }

#endif  // CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED
