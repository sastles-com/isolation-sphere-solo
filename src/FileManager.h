/**
 * @file FileManager.h
 * @brief LittleFSファイルシステム管理クラス
 * @author sastle-com
 * @date 2025-12-01
 */

#ifndef __FILE_MANAGER_H__
#define __FILE_MANAGER_H__

#include "common.h"
#include <Arduino.h>

#ifdef ESP32
#include <LittleFS.h>
#include <FS.h>
#endif

namespace sastle {
    /**
     * @class FileManager
     * @brief LittleFSファイルシステムの操作を提供するユーティリティクラス
     * 
     * ファイルの読み書き、ディレクトリ一覧表示、容量確認などの
     * 基本的なファイルシステム操作を静的メソッドで提供します。
     */
    class FileManager {
    public:
        FileManager() {}
        virtual ~FileManager() {}

        /**
         * @brief LittleFSを初期化してマウント
         * @return true 初期化成功, false 初期化失敗
         */
        static bool begin();
        
        /**
         * @brief LittleFSをアンマウント
         */
        static void end();

        /**
         * @brief ファイルの内容をStringに読み込む
         * @param path ファイルパス
         * @param output 読み込んだ内容を格納するString参照
         * @return true 読み込み成功, false 読み込み失敗
         */
        static bool readFile(const char* path, String &output);
        
        /**
         * @brief Stringの内容をファイルに書き込む
         * @param path ファイルパス
         * @param content 書き込む内容
         * @return true 書き込み成功, false 書き込み失敗
         */
        static bool writeFile(const char* path, const String &content);
        
        /**
         * @brief ディレクトリの内容を一覧表示 (lsコマンド相当)
         * @param dirname ディレクトリパス (デフォルト: "/")
         * @param levels 再帰表示の深さ (デフォルト: 0)
         */
        static void ls(const char* dirname = "/", uint8_t levels = 0);
        
        /**
         * @brief ファイルまたはディレクトリの存在を確認
         * @param path 確認するパス
         * @return true 存在する, false 存在しない
         */
        static bool exists(const char* path);
        
        /**
         * @brief ファイルを削除
         * @param path 削除するファイルのパス
         * @return true 削除成功, false 削除失敗
         */
        static bool remove(const char* path);
        
        /**
         * @brief ファイルシステムの総容量を取得
         * @return 総容量 (バイト)
         */
        static size_t totalBytes();
        
        /**
         * @brief ファイルシステムの使用済み容量を取得
         * @return 使用済み容量 (バイト)
         */
        static size_t usedBytes();
        
        /**
         * @brief ファイルシステム情報を表示
         */
        static void printInfo();
    };
}

#endif // __FILE_MANAGER_H__

