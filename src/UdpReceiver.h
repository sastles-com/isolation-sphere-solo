/**
 * @file UdpReceiver.h
 * @brief UDP 映像チャンクの受信 (AsyncUDP → FreeRTOS キュー)
 *
 * 派生元 core の NetworkManager::beginUDP/recvDatagram を分離したもの。
 * 受信は AsyncUDP (lwIP udp_recv コールバック直結。WiFiUDP の BSD ソケットポーリングは
 * 本ハードで機能しなかった)。各データグラム (= 1 チャンク) をキューに積み、FramePump が
 * recv() で取り出す。キューはコールバック (AsyncUDP タスク) ⇄ FramePump 間の並行性と
 * バーストを吸収する。
 *
 * solo での変更点: キューの記憶域を PSRAM に置く (32 段 × 1.5KB = 48KB の内部 RAM を空ける)。
 * listen は ANY にバインドするので STA が繋がる前に開いてよい。
 */

#ifndef __UDP_RECEIVER_H__
#define __UDP_RECEIVER_H__

#include <Arduino.h>
#include <AsyncUDP.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace sastle {

class UdpReceiver {
public:
    static constexpr size_t kMaxDatagram = 1500;  ///< 1データグラム最大 (MTU内)
    struct Datagram {
        uint16_t len;
        uint8_t data[kMaxDatagram];
    };

    ~UdpReceiver();

    /**
     * @brief キューを確保して listen を開始する
     * @param port     受信ポート (config wifi.udp_port)
     * @param queueLen キュー段数 (config source.udp_queue_len)
     */
    bool begin(uint16_t port, uint16_t queueLen);
    void end();

    /**
     * @brief 受信キューから 1 データグラムを取り出す
     * @param out  取り出し先
     * @param wait 待ち時間 (0 = ノンブロッキング)
     * @return true 取り出せた
     */
    bool recv(Datagram& out, TickType_t wait);

    bool listening() const { return _listening; }
    uint16_t port() const { return _port; }
    uint32_t received() const { return _received; }
    uint32_t dropped() const { return _dropped; }      ///< キュー満杯で捨てた数
    IPAddress remoteIP() const { return _remoteIP; }

private:
    AsyncUDP _udp;
    QueueHandle_t _queue = nullptr;
    StaticQueue_t _queueCtrl;
    uint8_t* _queueStorage = nullptr;   ///< PSRAM
    Datagram _cb;                        ///< コールバック用作業領域 (AsyncUDP タスク専用)
    bool _listening = false;
    uint16_t _port = 0;
    volatile uint32_t _received = 0;
    volatile uint32_t _dropped = 0;
    IPAddress _remoteIP;
};

}  // namespace sastle

#endif  // __UDP_RECEIVER_H__
