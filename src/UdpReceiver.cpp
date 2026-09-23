#include "UdpReceiver.h"

#include <esp_heap_caps.h>

namespace sastle {

UdpReceiver::~UdpReceiver() {
    end();
}

bool UdpReceiver::begin(uint16_t port, uint16_t queueLen) {
    if (_listening) {
        return true;
    }
    if (queueLen < 4) queueLen = 4;
    _port = port;

    if (!_queue) {
        const size_t bytes = (size_t)queueLen * sizeof(Datagram);
        _queueStorage = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
        if (!_queueStorage) {
            _queueStorage = (uint8_t*)malloc(bytes);  // PSRAM が無ければ内部ヒープ
        }
        if (!_queueStorage) {
            Serial.println("[UDP] ERROR: rx queue storage allocation failed");
            return false;
        }
        _queue = xQueueCreateStatic(queueLen, sizeof(Datagram), _queueStorage, &_queueCtrl);
        if (!_queue) {
            Serial.println("[UDP] ERROR: xQueueCreateStatic failed");
            free(_queueStorage);
            _queueStorage = nullptr;
            return false;
        }
        Serial.printf("[UDP] rx queue %u x %u B (%s)\n", (unsigned)queueLen, (unsigned)sizeof(Datagram),
                      heap_caps_get_allocated_size(_queueStorage) && esp_ptr_external_ram(_queueStorage)
                          ? "PSRAM" : "internal");
    }

    if (!_udp.listen(port)) {
        Serial.printf("[UDP] ERROR: listen on %u failed\n", (unsigned)port);
        return false;
    }
    // コールバックは AsyncUDP タスク文脈で直列に呼ばれる。キュー満杯時はドロップ
    // (最新を捨てる。再構成側は frame_id が進めば未完フレームを捨てて追従する)。
    _udp.onPacket([this](AsyncUDPPacket packet) {
        const size_t n = packet.length();
        if (n == 0 || n > kMaxDatagram) {
            return;
        }
        _cb.len = (uint16_t)n;
        memcpy(_cb.data, packet.data(), n);
        _remoteIP = packet.remoteIP();
        if (xQueueSend(_queue, &_cb, 0) == pdTRUE) {
            _received++;
        } else {
            _dropped++;
        }
    });
    _listening = true;
    Serial.printf("[UDP] listening on port %u (AsyncUDP, bound to ANY)\n", (unsigned)port);
    return true;
}

void UdpReceiver::end() {
    if (_listening) {
        _udp.close();
        _listening = false;
        Serial.println("[UDP] stopped");
    }
    // キューは保持する (再 listen で再利用)。
}

bool UdpReceiver::recv(Datagram& out, TickType_t wait) {
    if (!_queue) {
        return false;
    }
    return xQueueReceive(_queue, &out, wait) == pdTRUE;
}

}  // namespace sastle
