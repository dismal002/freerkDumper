# freerkDumper

這款免費開源的 Rockchip 韌體擷取工具使用 C 語言編寫。它主要在各種通用的 Rockchip 電視盒和相框上進行了測試，但功能尚不完善，歡迎大家貢獻程式碼！

## 用法

```用法：./freerkDumper [選項]

設備操作：

-i, --info 僅列印設備快閃記憶體和晶片資訊

-a, --dump-all 轉儲所有分區

-p, --dump <part> 按名稱轉儲特定分區（例如 system）

-o, --out <dir> 輸出目錄（預設：Output/）

-y, --physical 讀取物理區塊（528 位元組）而非邏輯區塊

-m, --mode <mode> 使用指定的裝置模式：msc、loader、maskrom

-s, --scan 掃描所有已連接的 RockChip 設備

鏡像操作：

--unpack <file> 解包 RockChip 鏡像檔（KRNL/PARM/RKFP/bootloader）

--pack <file> 將檔案打包成 RockChip 鏡像格式

--encrypt 打包時使用 RC4 加密

--detect <file> 偵測鏡像格式並顯示訊息

--verify <file> 驗證鏡像簽章和校驗和

-h, --help 顯示此說明訊息

```
