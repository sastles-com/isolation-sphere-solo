/**
 * @file jpeg_synth.h
 * @brief テスト用の合成 JPEG バイト列 (実デコードはしない。マーカー構造だけ正しい)
 */
#ifndef __JPEG_SYNTH_H__
#define __JPEG_SYNTH_H__

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace jpeg_synth {

typedef std::vector<uint8_t> Bytes;

inline void append(Bytes& b, const uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); }

inline void marker(Bytes& b, uint8_t m) {
    b.push_back(0xFF);
    b.push_back(m);
}

/// [FF m][len hi][len lo][payload...]  (len はペイロード+2)
inline void segment(Bytes& b, uint8_t m, const Bytes& payload) {
    marker(b, m);
    const size_t len = payload.size() + 2;
    b.push_back((uint8_t)(len >> 8));
    b.push_back((uint8_t)(len & 0xFF));
    append(b, payload.data(), payload.size());
}

inline Bytes sofPayload(uint16_t w, uint16_t h) {
    // precision, height, width, ncomp=1, (id, sampling, qtable)
    Bytes p;
    p.push_back(8);
    p.push_back((uint8_t)(h >> 8)); p.push_back((uint8_t)(h & 0xFF));
    p.push_back((uint8_t)(w >> 8)); p.push_back((uint8_t)(w & 0xFF));
    p.push_back(1);
    p.push_back(1); p.push_back(0x11); p.push_back(0);
    return p;
}

inline Bytes sosPayload() {
    Bytes p;
    p.push_back(1);                       // ncomp
    p.push_back(1); p.push_back(0x00);    // comp id, tables
    p.push_back(0); p.push_back(63); p.push_back(0);  // Ss, Se, Ah/Al
    return p;
}

/// 標準的な1フレーム: SOI, APP0, DQT, SOF0, DHT, SOS, entropy, EOI
inline Bytes makeFrame(uint16_t w, uint16_t h, const Bytes& entropy, uint8_t sofMarker = 0xC0,
                       const Bytes* app1 = nullptr) {
    Bytes f;
    marker(f, 0xD8);
    segment(f, 0xE0, Bytes{'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0});
    if (app1) {
        segment(f, 0xE1, *app1);
    }
    segment(f, 0xDB, Bytes(65, 0x01));           // DQT (dummy)
    segment(f, sofMarker, sofPayload(w, h));
    segment(f, 0xC4, Bytes(17, 0x00));           // DHT (dummy)
    segment(f, 0xDA, sosPayload());
    append(f, entropy.data(), entropy.size());
    marker(f, 0xD9);
    return f;
}

/// 0x00..0x7F の疑似乱数列 (実エントロピーデータに 0xFF 単独は現れない)
inline Bytes entropy(size_t n, uint8_t seed) {
    Bytes e;
    for (size_t i = 0; i < n; i++) e.push_back((uint8_t)((i * 7 + seed) & 0x7F));
    return e;
}

}  // namespace jpeg_synth

#endif  // __JPEG_SYNTH_H__
